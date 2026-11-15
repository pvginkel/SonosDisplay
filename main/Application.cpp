#include "Application.h"

#include <algorithm>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "support.h"
#include "ui/LoadingView.h"
#include "ui/Messages.h"
#include "ui/PlaceholderView.h"
#include "ui/Theme.h"

LOG_TAG(Application);

static constexpr uint32_t UI_TASK_STACK_SIZE = 16384;
static constexpr UBaseType_t UI_TASK_PRIORITY = 2;
static constexpr BaseType_t UI_TASK_CORE = 1;

static constexpr uint32_t BACKEND_TASK_STACK_SIZE = 6144;
static constexpr UBaseType_t BACKEND_TASK_PRIORITY = 1;
static constexpr BaseType_t BACKEND_TASK_CORE = 0;

static constexpr uint32_t MIN_LOOP_DELAY_MS = 1;
static constexpr uint32_t MAX_LOOP_DELAY_MS = 10;

static constexpr int64_t MEMORY_LOG_INTERVAL_MS = 60 * 1000;

static constexpr int MIN_SCREEN_OFF_SECONDS = 10;
static constexpr int MAX_SCREEN_OFF_SECONDS = 3600;

// scheme://host[:port], no path and no trailing slash: what the model's schema requires.
static bool is_valid_backend_url(const char* url) {
    const char* host;
    if (strncmp(url, "http://", 7) == 0) {
        host = url + 7;
    } else if (strncmp(url, "https://", 8) == 0) {
        host = url + 8;
    } else {
        return false;
    }

    return *host && !strchr(host, '/');
}

Application::Application(Board* board) : _board(board) { _model.firmware_version = esp_app_get_description()->version; }

void Application::do_begin() {
    ESP_LOGI(TAG, "Starting the UI task");

    const auto created = xTaskCreatePinnedToCore([](void* arg) { ((Application*)arg)->run(); }, "ui",
                                                 UI_TASK_STACK_SIZE, this, UI_TASK_PRIORITY, nullptr, UI_TASK_CORE);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void Application::run() {
    Theme::begin(_board->get_display());

    _shell = std::make_unique<Shell>(this);
    _shell->begin(_board->get_display());

    auto loading_view = std::make_unique<LoadingView>(this, MSG_STARTING);
    _loading_view = loading_view.get();
    _shell->replace(std::move(loading_view));

    lv_refr_now(nullptr);

    while (true) {
        process();

        const auto wait = lv_timer_handler();

        vTaskDelay(pdMS_TO_TICKS(std::clamp(wait, MIN_LOOP_DELAY_MS, MAX_LOOP_DELAY_MS)));
    }
}

void Application::do_configuration_loaded(cJSON* data) {
    ApplicationBase::do_configuration_loaded(data);

    // esp-mdm frees the JSON when this returns, so what is needed is copied out.

    auto device_name = cJSON_GetObjectItemCaseSensitive(data, "deviceName");
    if (cJSON_IsString(device_name) && device_name->valuestring) {
        _model.device_name = device_name->valuestring;
    }

    auto backend_url = cJSON_GetObjectItemCaseSensitive(data, "backendUrl");
    if (cJSON_IsString(backend_url) && backend_url->valuestring && is_valid_backend_url(backend_url->valuestring)) {
        _model.backend_url = backend_url->valuestring;
        ESP_LOGI(TAG, "Backend at %s", _model.backend_url.c_str());
    } else {
        ESP_LOGE(TAG, "The device configuration has no valid backendUrl");
    }

    auto screen_off_seconds = cJSON_GetObjectItemCaseSensitive(data, "screenOffSeconds");
    if (cJSON_IsNumber(screen_off_seconds)) {
        const auto seconds = screen_off_seconds->valueint;
        if (seconds >= MIN_SCREEN_OFF_SECONDS && seconds <= MAX_SCREEN_OFF_SECONDS) {
            _screen_off_seconds = uint32_t(seconds);
        } else {
            ESP_LOGW(TAG, "Ignoring screenOffSeconds %d", seconds);
        }
    }

    _board->get_screen_power().set_timeout_seconds(_screen_off_seconds);

    model_changed();

    // The update check that follows blocks this task, so the message is drawn now and stays as
    // a still frame.
    if (_loading_view) {
        _loading_view->set_message(MSG_CHECKING_FOR_UPDATE);
        lv_refr_now(nullptr);
    }
}

void Application::do_ready() {
    ApplicationBase::do_ready();

    _board->log_state();

    // Startup reads and writes flash (WiFi's and the PHY's data in NVS, the update and core dump
    // checks), and each access can shift the picture until the panel is restarted. Startup is
    // over here, so the picture is put right once now rather than at the next wake.
    _board->resync_panel();

    if (_model.backend_url.empty()) {
        // No crash and no restart: the display says what is missing.
        if (_loading_view) {
            _loading_view->set_error(MSG_NO_BACKEND);
        }
        return;
    }

    begin_backend();

    _loading_view = nullptr;
    _shell->replace(std::make_unique<PlaceholderView>(this));
}

void Application::begin_backend() {
    ESP_LOGI(TAG, "Starting the backend tasks");

    _model.stream_state = StreamState::Connecting;
    _model.stream_down_since = lv_tick_get();
    model_changed();

    _api_client = std::make_unique<HttpClient>(&get_queue(), "backend_api");
    ESP_ERROR_CHECK(
        _api_client->begin(_model.backend_url, BACKEND_TASK_STACK_SIZE, BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _art_client = std::make_unique<HttpClient>(&get_queue(), "backend_art");
    ESP_ERROR_CHECK(
        _art_client->begin(_model.backend_url, BACKEND_TASK_STACK_SIZE, BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _event_stream = std::make_unique<EventStream>(&get_queue());

    _event_stream->on_connected([this]() {
        // Events sent while the stream was away are gone: from milestone 1, this is where the
        // subscriptions are renewed and the state the views show is fetched again.
        _model.stream_state = StreamState::Connected;
        model_changed();
    });

    _event_stream->on_disconnected([this]() {
        _model.stream_state = StreamState::Disconnected;
        _model.stream_down_since = lv_tick_get();
        model_changed();
    });

    _event_stream->on_event(
        [](const std::string& type, EventStream::Payload) { ESP_LOGI(TAG, "Event %s", type.c_str()); });

    ESP_ERROR_CHECK(
        _event_stream->begin(_model.backend_url, BACKEND_TASK_STACK_SIZE, BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));
}

void Application::do_process() {
    _board->process();

    if (_shell) {
        _shell->update();
    }

    log_memory();
}

void Application::log_memory() {
    const auto now = esp_get_millis();
    if (_last_memory_log_ms && now - _last_memory_log_ms < MEMORY_LOG_INTERVAL_MS) {
        return;
    }

    _last_memory_log_ms = now;

    ESP_LOGI(TAG, "Free memory: internal %zu (minimum %zu), PSRAM %zu (minimum %zu)",
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM), heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM));
}
