#include "HttpClient.h"

#include <algorithm>
#include <cstring>

#include "esp_heap_caps.h"
#include "support.h"

LOG_TAG(HttpClient);

static constexpr size_t INITIAL_BODY_CAPACITY = 4 * 1024;

void HttpBodyDeleter::operator()(uint8_t* body) const { heap_caps_free(body); }

HttpClient::HttpClient(Queue* ui_queue, const char* name) : _ui_queue(ui_queue), _name(name) {}

esp_err_t HttpClient::begin(const std::string& base_url, const char* root_certificate, uint32_t stack_size,
                            UBaseType_t priority, BaseType_t core) {
    _base_url = base_url;
    _root_certificate = root_certificate;

    _jobs_waiting = xSemaphoreCreateCounting(QUEUE_DEPTH, 0);
    ESP_RETURN_ON_FALSE(_jobs_waiting, ESP_ERR_NO_MEM, TAG, "Cannot create the request queue");

    const auto created = xTaskCreatePinnedToCore([](void* arg) { ((HttpClient*)arg)->run(); }, _name, stack_size, this,
                                                 priority, nullptr, core);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "Cannot create task %s", _name);

    return ESP_OK;
}

void HttpClient::send(HttpRequest request, std::weak_ptr<void> scope, std::function<void(HttpResponse&&)> done) {
    auto lock = _lock.take();

    if (request.merges) {
        for (const auto waiting : _waiting) {
            const auto& other = waiting->request;

            if (other.merges && other.method == request.method && other.path == request.path &&
                other.body == request.body) {
                waiting->scope = std::move(scope);
                waiting->done = std::move(done);
                return;
            }
        }
    }

    auto job = new Job{std::move(request), std::move(scope), std::move(done)};
    _unanswered++;

    if (_waiting.size() == QUEUE_DEPTH) {
        ESP_LOGW(TAG, "%s: request queue full; dropping %s", _name, job->request.path.c_str());

        deliver(job, HttpResponse{.err = ESP_ERR_NO_MEM});
        return;
    }

    _waiting.push_back(job);
    xSemaphoreGive(_jobs_waiting);
}

void HttpClient::run() {
    while (true) {
        xSemaphoreTake(_jobs_waiting, portMAX_DELAY);

        Job* job;
        {
            auto lock = _lock.take();

            job = _waiting.front();
            _waiting.pop_front();
        }

        // A closed view leaves no work behind it.
        if (job->scope.expired()) {
            delete job;
            _unanswered--;
            continue;
        }

        deliver(job, perform(job->request));
    }
}

void HttpClient::deliver(Job* job, HttpResponse&& response) {
    // std::function needs a copyable closure, so the job and its response travel shared.
    auto shared_job = std::shared_ptr<Job>(job);
    auto shared_response = std::make_shared<HttpResponse>(std::move(response));

    const auto task = [this, shared_job, shared_response]() {
        if (!shared_job->scope.expired()) {
            shared_job->done(std::move(*shared_response));
        }

        _unanswered--;
    };

    // send() runs on the UI task, which must not wait on its own queue.
    if (!_ui_queue->try_enqueue(task)) {
        ESP_LOGE(TAG, "%s: UI queue full; dropping the response to %s", _name, shared_job->request.path.c_str());

        _unanswered--;
    }
}

HttpResponse HttpClient::perform(const HttpRequest& request) {
    auto response = perform_once(request);

    // A GET is retried once after a transport error. Nothing else is: a repeated "next" is a
    // different command.
    if (response.err != ESP_OK && request.method == HTTP_METHOD_GET && response.err != ESP_ERR_INVALID_SIZE) {
        ESP_LOGW(TAG, "%s: retrying GET %s after %s", _name, request.path.c_str(), esp_err_to_name(response.err));

        response = perform_once(request);
    }

    if (response.err != ESP_OK) {
        ESP_LOGW(TAG, "%s: %s failed: %s", _name, request.path.c_str(), esp_err_to_name(response.err));
    }

    return response;
}

HttpResponse HttpClient::perform_once(const HttpRequest& request) {
    HttpResponse response;

    if (_client && esp_get_millis() - _last_used_ms > KEEP_ALIVE_IDLE_MS) {
        esp_http_client_close(_client);
    }

    response.err = ensure_client();
    if (response.err != ESP_OK) {
        return response;
    }

    const auto url = _base_url + request.path;

    esp_http_client_set_url(_client, url.c_str());
    esp_http_client_set_method(_client, request.method);
    esp_http_client_set_header(_client, "Accept", "application/json");
    if (request.body.empty()) {
        esp_http_client_delete_header(_client, "Content-Type");
    } else {
        esp_http_client_set_header(_client, "Content-Type", "application/json");
    }

    const auto fail = [this, &response](esp_err_t err) {
        // After a transport error the handle is destroyed and a new one made.
        reset_client();
        response.err = err;
        response.body.reset();
        response.length = 0;
        return std::move(response);
    };

    auto err = esp_http_client_open(_client, int(request.body.length()));
    if (err != ESP_OK) {
        return fail(err);
    }

    if (!request.body.empty()) {
        const auto written = esp_http_client_write(_client, request.body.data(), int(request.body.length()));
        if (written != int(request.body.length())) {
            return fail(ESP_FAIL);
        }
    }

    const auto content_length = esp_http_client_fetch_headers(_client);
    if (content_length < 0) {
        return fail(esp_err_t(-content_length));
    }

    response.status = esp_http_client_get_status_code(_client);

    err = read_body(request, content_length, response);
    if (err != ESP_OK) {
        return fail(err);
    }

    // A response that was not read to its end leaves the connection unusable.
    if (!esp_http_client_is_complete_data_received(_client)) {
        esp_http_client_close(_client);
    }

    _last_used_ms = esp_get_millis();

    return response;
}

esp_err_t HttpClient::read_body(const HttpRequest& request, int64_t content_length, HttpResponse& response) {
    if (content_length > int64_t(request.max_response)) {
        return ESP_ERR_INVALID_SIZE;
    }

    // Sized from Content-Length, growing when there is none.
    size_t capacity = content_length > 0 ? size_t(content_length) : INITIAL_BODY_CAPACITY;
    size_t length = 0;

    auto body = HttpBody((uint8_t*)heap_caps_malloc(capacity + 1, MALLOC_CAP_SPIRAM));
    ESP_RETURN_ON_FALSE(body, ESP_ERR_NO_MEM, TAG, "Cannot allocate %zu bytes", capacity + 1);

    while (true) {
        if (length == capacity) {
            if (capacity >= request.max_response) {
                return ESP_ERR_INVALID_SIZE;
            }

            capacity = std::min(capacity * 2, request.max_response);

            auto grown = (uint8_t*)heap_caps_realloc(body.get(), capacity + 1, MALLOC_CAP_SPIRAM);
            ESP_RETURN_ON_FALSE(grown, ESP_ERR_NO_MEM, TAG, "Cannot grow to %zu bytes", capacity + 1);

            (void)body.release();
            body.reset(grown);
        }

        const auto read = esp_http_client_read(_client, (char*)body.get() + length, int(capacity - length));
        if (read < 0) {
            return read == -ESP_ERR_HTTP_EAGAIN ? ESP_ERR_TIMEOUT : ESP_FAIL;
        }
        if (read == 0) {
            break;
        }

        length += size_t(read);

        if (content_length > 0 && length == size_t(content_length)) {
            break;
        }
    }

    body.get()[length] = 0;

    response.body = std::move(body);
    response.length = length;

    return ESP_OK;
}

esp_err_t HttpClient::ensure_client() {
    if (_client) {
        return ESP_OK;
    }

    esp_http_client_config_t config{};
    config.url = _base_url.c_str();
    config.timeout_ms = TIMEOUT_MS;
    config.keep_alive_enable = true;
    config.cert_pem = _root_certificate;

    _client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(_client, ESP_FAIL, TAG, "Cannot create an HTTP client");

    return ESP_OK;
}

void HttpClient::reset_client() {
    if (_client) {
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }
}
