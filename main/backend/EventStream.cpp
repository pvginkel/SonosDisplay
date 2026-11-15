#include "EventStream.h"

#include <algorithm>

#include "esp_random.h"
#include "strformat.h"
#include "support.h"

LOG_TAG(EventStream);

// Reconnect after 1, 2, 4, 8, then 15 seconds; reset by `ready`.
static constexpr uint32_t RECONNECT_DELAYS_MS[] = {1000, 2000, 4000, 8000, 15000};

EventStream::EventStream(Queue* ui_queue) : _ui_queue(ui_queue) {}

esp_err_t EventStream::begin(const std::string& base_url, const char* root_certificate, uint32_t stack_size,
                             UBaseType_t priority, BaseType_t core) {
    _root_certificate = root_certificate;

    // Made once per boot and reused on every reconnect: the server closes an older stream with
    // the same id.
    std::string request_id;
    for (int i = 0; i < 4; i++) {
        request_id += strformat("%08" PRIx32, esp_random());
    }

    _url = base_url + "/api/sse/stream?request_id=" + request_id;

    const auto created = xTaskCreatePinnedToCore([](void* arg) { ((EventStream*)arg)->run(); }, "backend_events",
                                                 stack_size, this, priority, nullptr, core);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "Cannot create the event stream task");

    return ESP_OK;
}

void EventStream::run() {
    size_t attempt = 0;

    while (true) {
        ESP_LOGI(TAG, "Connecting to %s", _url.c_str());

        _ready = false;
        _rejected = false;

        connect_and_read();

        if (_ready) {
            ESP_LOGW(TAG, "Event stream lost");

            attempt = 0;
            if (_disconnected) {
                _ui_queue->enqueue(_disconnected);
            }
        }

        uint32_t delay_ms;
        if (_rejected) {
            delay_ms = REJECTED_DELAY_MS;
        } else {
            delay_ms = RECONNECT_DELAYS_MS[std::min(attempt, std::size(RECONNECT_DELAYS_MS) - 1)];
            attempt++;
        }

        ESP_LOGI(TAG, "Reconnecting in %" PRIu32 " ms", delay_ms);

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

void EventStream::connect_and_read() {
    esp_http_client_config_t config{};
    config.url = _url.c_str();
    config.timeout_ms = CONNECT_TIMEOUT_MS;
    config.cert_pem = _root_certificate;

    _client = esp_http_client_init(&config);
    if (!_client) {
        ESP_LOGE(TAG, "Cannot create an HTTP client");
        return;
    }
    DEFER({
        esp_http_client_cleanup(_client);
        _client = nullptr;
    });

    esp_http_client_set_header(_client, "Accept", "text/event-stream");

    auto err = esp_http_client_open(_client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Cannot connect: %s", esp_err_to_name(err));
        return;
    }

    const auto content_length = esp_http_client_fetch_headers(_client);
    if (content_length < 0) {
        ESP_LOGW(TAG, "Cannot read the response headers: %s", esp_err_to_name(esp_err_t(-content_length)));
        return;
    }

    // A refused stream also opens with 200; the stream counts as connected from `ready` on.
    const auto status = esp_http_client_get_status_code(_client);
    if (status != 200) {
        ESP_LOGW(TAG, "Event stream answered %d", status);
        return;
    }

    esp_http_client_set_timeout_ms(_client, READ_TIMEOUT_MS);

    _line.clear();
    _frame_event.clear();
    _frame_data.clear();
    _frame_dropped = false;
    _last_data_ms = esp_get_millis();

    char buffer[READ_SIZE];

    while (!_rejected) {
        const auto read = esp_http_client_read(_client, buffer, sizeof(buffer));

        if (read > 0) {
            _last_data_ms = esp_get_millis();
            parse(buffer, size_t(read));
        } else if (read == -ESP_ERR_HTTP_EAGAIN) {
            // No data within the read timeout, which is normal.
        } else {
            ESP_LOGW(TAG, "Event stream ended (%d)", read);
            return;
        }

        if (esp_get_millis() - _last_data_ms > IDLE_LIMIT_MS) {
            ESP_LOGW(TAG, "No data for %lld ms; closing the event stream", IDLE_LIMIT_MS);
            return;
        }
    }
}

void EventStream::parse(const char* data, size_t length) {
    for (size_t i = 0; i < length; i++) {
        const auto c = data[i];

        if (c == '\n') {
            if (!_line.empty() && _line.back() == '\r') {
                _line.pop_back();
            }

            parse_line();
            _line.clear();
        } else if (_frame_dropped) {
            // The rest of an oversized frame is skipped up to its end.
        } else if (_line.length() + _frame_data.length() >= MAX_FRAME_SIZE) {
            ESP_LOGW(TAG, "Dropping a frame over %zu bytes", MAX_FRAME_SIZE);

            _frame_dropped = true;
            _line.clear();
            _frame_data.clear();
        } else {
            _line += c;
        }
    }
}

void EventStream::parse_line() {
    if (_line.empty()) {
        if (_frame_dropped) {
            _frame_dropped = false;
            _frame_event.clear();
            _frame_data.clear();
        } else {
            dispatch_frame();
        }
        return;
    }

    if (_frame_dropped || _line[0] == ':') {
        // A comment, such as the server's heartbeat.
        return;
    }

    const auto colon = _line.find(':');
    const auto field = _line.substr(0, colon);
    std::string value;
    if (colon != std::string::npos) {
        value = _line.substr(colon + 1);
        if (!value.empty() && value[0] == ' ') {
            value.erase(0, 1);
        }
    }

    if (field == "event") {
        _frame_event = value;
    } else if (field == "data") {
        if (!_frame_data.empty()) {
            _frame_data += '\n';
        }
        _frame_data += value;
    }
}

void EventStream::dispatch_frame() {
    const auto event = std::move(_frame_event);
    _frame_event.clear();

    if (event.empty() && _frame_data.empty()) {
        return;
    }

    if (event == "ready") {
        ESP_LOGI(TAG, "Event stream ready");

        _ready = true;
        if (_connected) {
            _ui_queue->enqueue(_connected);
        }
    } else if (event == "rejected") {
        ESP_LOGW(TAG, "Event stream rejected: %s", _frame_data.c_str());

        _rejected = true;
    } else if (event.empty()) {
        dispatch_domain_event();
    } else if (event != "version") {
        ESP_LOGD(TAG, "Ignoring event %s", event.c_str());
    }

    _frame_data.clear();
}

void EventStream::dispatch_domain_event() {
    // A domain event is {"type": "<name>", "payload": {...}}.
    auto root = Payload(cJSON_ParseWithLength(_frame_data.c_str(), _frame_data.length()), cJSON_Delete);
    if (!root) {
        ESP_LOGW(TAG, "Cannot parse an event: %s", _frame_data.c_str());
        return;
    }

    const auto type = cJSON_GetObjectItemCaseSensitive(root.get(), "type");
    if (!cJSON_IsString(type) || !type->valuestring) {
        ESP_LOGW(TAG, "An event without a type: %s", _frame_data.c_str());
        return;
    }

    // The payload stays a part of its root, which the shared pointer keeps alive.
    const auto payload_item = cJSON_GetObjectItemCaseSensitive(root.get(), "payload");
    auto payload = Payload(root, payload_item);
    auto type_name = std::string(type->valuestring);

    ESP_LOGD(TAG, "Event %s", type_name.c_str());

    if (_event) {
        _ui_queue->enqueue([this, type_name, payload]() { _event(type_name, payload); });
    }
}
