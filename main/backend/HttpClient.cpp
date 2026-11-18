#include "HttpClient.h"

#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <cstring>

#include "esp_heap_caps.h"
#include "support.h"

LOG_TAG(HttpClient);

static constexpr size_t INITIAL_BODY_CAPACITY = 4 * 1024;

// The answer to keep_open()'s GET, which is read and dropped.
static constexpr size_t KEEP_OPEN_MAX_RESPONSE = 1024;

static const char* method_name(esp_http_client_method_t method) {
    switch (method) {
        case HTTP_METHOD_GET:
            return "GET";
        case HTTP_METHOD_POST:
            return "POST";
        case HTTP_METHOD_PUT:
            return "PUT";
        case HTTP_METHOD_DELETE:
            return "DELETE";
        default:
            return "request";
    }
}

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

void HttpClient::keep_open(const char* path) {
    auto lock = _lock.take();

    const auto now = esp_get_millis();

    // A request that waits or is under way opens the connection, or keeps it open, itself.
    if (!_waiting.empty() || _under_way) {
        return;
    }
    if (_connected && now - _last_used_ms < KEEP_OPEN_AFTER_MS) {
        return;
    }
    if (_last_keep_open_ms && now - _last_keep_open_ms < KEEP_OPEN_RETRY_MS) {
        return;
    }

    _last_keep_open_ms = now;

    auto job = new Job{.request = {.path = path, .max_response = KEEP_OPEN_MAX_RESPONSE}, .keeps_open = true};

    _waiting.push_back(job);
    xSemaphoreGive(_jobs_waiting);
}

void HttpClient::run() {
    while (true) {
        auto job = next_job();
        if (!job) {
            continue;
        }

        if (job->keeps_open) {
            // Not retried: keep_open() asks again while it is called.
            (void)perform_once(job->request);
            delete job;
        } else if (job->scope.expired()) {
            // A closed view leaves no work behind it.
            delete job;
            _unanswered--;
        } else {
            deliver(job, perform(job->request));
        }

        auto lock = _lock.take();
        _under_way = false;
    }
}

// Waits for the next job, and closes the connection once it has been idle for KEEP_ALIVE_IDLE_MS
// meanwhile. Returns nullptr when it closed it.
HttpClient::Job* HttpClient::next_job() {
    auto wait = portMAX_DELAY;
    if (_connected) {
        const auto idle_ms = esp_get_millis() - _last_used_ms;
        wait = idle_ms >= KEEP_ALIVE_IDLE_MS ? 0 : pdMS_TO_TICKS(KEEP_ALIVE_IDLE_MS - idle_ms) + 1;
    }

    if (xSemaphoreTake(_jobs_waiting, wait) != pdTRUE) {
        const auto idle_ms = esp_get_millis() - _last_used_ms;
        if (_connected && idle_ms >= KEEP_ALIVE_IDLE_MS) {
            ESP_LOGI(TAG, "%s: closing the connection after %" PRId64 " s idle", _name, idle_ms / 1000);

            close_connection();
        }
        return nullptr;
    }

    auto lock = _lock.take();

    auto job = _waiting.front();
    _waiting.pop_front();
    _under_way = true;

    return job;
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
        ESP_LOGW(TAG, "%s: retrying GET %s", _name, request.path.c_str());

        response = perform_once(request);
    }

    return response;
}

HttpResponse HttpClient::perform_once(const HttpRequest& request) {
    HttpResponse response;

    const auto started = esp_get_millis();
    const auto idle_ms = started - _last_used_ms;

    // The task closes a connection at KEEP_ALIVE_IDLE_MS, but a request can be taken just as the
    // time passes. One the server closed sooner, as nginx does with its idle connections when it
    // reloads, is found before anything is sent on it.
    if (_connected && idle_ms >= KEEP_ALIVE_IDLE_MS) {
        close_connection();
    } else if (_connected && server_closed()) {
        ESP_LOGI(TAG, "%s: the server closed the connection after %" PRId64 " s idle", _name, idle_ms / 1000);

        close_connection();
    }

    const bool reused = _connected;

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

    const auto fail = [&](esp_err_t err) {
        ESP_LOGW(TAG, "%s: %s %s failed after %" PRId64 " ms, on a %s connection: %s", _name,
                 method_name(request.method), request.path.c_str(), esp_get_millis() - started,
                 reused ? "reused" : "new", esp_err_to_name(err));

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

    const auto opened = esp_get_millis();

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

    const auto now = esp_get_millis();

    if (reused) {
        ESP_LOGI(TAG, "%s: %s %s: %d in %" PRId64 " ms, on the connection idle %" PRId64 " s", _name,
                 method_name(request.method), request.path.c_str(), response.status, now - started, idle_ms / 1000);
    } else {
        ESP_LOGI(TAG, "%s: %s %s: %d in %" PRId64 " ms, on a new connection opened in %" PRId64 " ms", _name,
                 method_name(request.method), request.path.c_str(), response.status, now - started, opened - started);
    }

    // A response that was not read to its end leaves the connection unusable, and one the server
    // answered with "Connection: close" is about to be closed.
    if (!esp_http_client_is_complete_data_received(_client) || !esp_http_client_is_persistent_connection(_client)) {
        close_connection();
    } else {
        used(now);
    }

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
    // The TLS session is kept with the handle, and a new connection resumes it where the server
    // still holds it: the abbreviated handshake has no certificate to check and no key exchange,
    // and takes one round trip fewer. It needs CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS.
    config.save_client_session = true;

    _client = esp_http_client_init(&config);
    ESP_RETURN_ON_FALSE(_client, ESP_FAIL, TAG, "Cannot create an HTTP client");

    return ESP_OK;
}

// Whether the server closed the kept-alive connection. In HTTP/1.1 nothing comes on an idle
// connection but the server's close, its TLS alert and its FIN, so any byte, an end of stream or
// an error says so. Peeks, so nothing is read.
bool HttpClient::server_closed() {
    const auto socket = esp_http_client_get_socket(_client);
    if (socket < 0) {
        return true;
    }

    uint8_t byte;
    const auto received = recv(socket, &byte, 1, MSG_PEEK | MSG_DONTWAIT);

    return received >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK);
}

// Closes the connection and keeps the handle, with the TLS session it can resume.
void HttpClient::close_connection() {
    if (_client) {
        esp_http_client_close(_client);
    }

    _connected = false;
}

void HttpClient::reset_client() {
    if (_client) {
        esp_http_client_cleanup(_client);
        _client = nullptr;
    }

    _connected = false;
}

void HttpClient::used(int64_t now) {
    auto lock = _lock.take();

    _last_used_ms = now;
    _connected = true;
}
