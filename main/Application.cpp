#include "Application.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "api/Answers.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "support.h"
#include "ui/LoadingView.h"
#include "ui/Messages.h"
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

// Where the speaker choice is stored: not in esp-mdm's namespace, whose unknown keys are deleted
// when provisioning data is renewed.
static constexpr char SPEAKER_NVS_NAMESPACE[] = "sonos";
static constexpr char SPEAKER_NVS_KEY[] = "speaker";

// certs/homelab-root.pem, which main/CMakeLists.txt embeds with a NUL after its last byte.
extern const char homelab_root_pem[] asm("_binary_homelab_root_pem_start");

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

// The path of a speaker's resource. The id is the backend's own text, so what a path segment
// cannot hold as it is goes in percent-encoded.
static std::string speaker_path(const std::string& speaker_id, const char* resource) {
    static constexpr char HEX[] = "0123456789ABCDEF";

    std::string path = "/api/speakers/";
    for (const auto c : speaker_id) {
        if (isalnum(uint8_t(c)) || strchr("-._~", c)) {
            path += c;
        } else {
            path += '%';
            path += HEX[uint8_t(c) >> 4];
            path += HEX[uint8_t(c) & 0xF];
        }
    }

    return path + resource;
}

static const char* command_name(Command command) {
    switch (command) {
        case Command::Play:
            return "play";
        case Command::Pause:
            return "pause";
        case Command::Next:
            return "next";
        case Command::Previous:
            return "previous";
    }

    return "";
}

static esp_err_t read_speaker_choice(std::string& id) {
    nvs_handle_t handle;
    auto err = nvs_open(SPEAKER_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    DEFER(nvs_close(handle));

    // The length counts the NUL after the id.
    size_t length = 0;
    err = nvs_get_str(handle, SPEAKER_NVS_KEY, nullptr, &length);
    if (err != ESP_OK) {
        return err;
    }

    std::string value(length, 0);
    ESP_ERROR_RETURN(nvs_get_str(handle, SPEAKER_NVS_KEY, value.data(), &length));
    value.resize(length - 1);

    id = std::move(value);

    return ESP_OK;
}

static esp_err_t write_speaker_choice(const std::string& id) {
    nvs_handle_t handle;
    ESP_ERROR_RETURN(nvs_open(SPEAKER_NVS_NAMESPACE, NVS_READWRITE, &handle));
    DEFER(nvs_close(handle));

    ESP_ERROR_RETURN(nvs_set_str(handle, SPEAKER_NVS_KEY, id.c_str()));

    return nvs_commit(handle);
}

Application::Application(Board* board) : _board(board) {}

void Application::do_begin() {
    ESP_LOGI(TAG, "Starting the UI task");

    const auto created = xTaskCreatePinnedToCore([](void* arg) { ((Application*)arg)->run(); }, "ui",
                                                 UI_TASK_STACK_SIZE, this, UI_TASK_PRIORITY, nullptr, UI_TASK_CORE);
    ESP_ERROR_CHECK(created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

void Application::run() {
    Theme::begin(_board->get_display());

    load_speaker_choice();

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
    _shell->open();
}

void Application::load_speaker_choice() {
    const auto err = read_speaker_choice(_model.speaker_id);

    // Neither the namespace nor the key exists before the first pick.
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "This display controls speaker %s", _model.speaker_id.c_str());
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No speaker is chosen yet");
    } else {
        ESP_LOGE(TAG, "Cannot read the speaker choice: %s", esp_err_to_name(err));
    }
}

void Application::choose_speaker(const std::string& id) {
    if (id == _model.speaker_id) {
        return;
    }

    ESP_LOGI(TAG, "Speaker %s is chosen", id.c_str());

    const auto err = write_speaker_choice(id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot store the speaker choice: %s", esp_err_to_name(err));
    }

    // A flash write can shift the picture until the panel is restarted (docs/firmware-design.md,
    // section 3).
    _board->resync_panel();

    _model.speaker_id = id;
    _model.playback.speaker_changed();
    model_changed();

    refresh_playback();
}

void Application::refresh_speakers() {
    // A list that is on screen stays there while a newer one is on its way.
    if (_model.speakers.state != LoadState::Loaded) {
        _model.speakers.state = LoadState::Loading;
        model_changed();
    }

    _api_client->send(HttpRequest{.path = "/api/speakers"}, _scope,
                      [this](HttpResponse&& response) { speakers_answered(std::move(response)); });
}

void Application::speakers_answered(HttpResponse&& response) {
    auto& list = _model.speakers;
    const auto was_held = _model.needs_speaker();

    if (response.err == ESP_ERR_INVALID_SIZE) {
        // An answer over the client's limit, which HttpClient logged.
        list.state = LoadState::Failed;
        list.error.clear();
    } else if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        list.state = LoadState::Unreachable;
        list.error.clear();
    } else if (const auto problem =
                   read_speakers(response.status, (const char*)response.body.get(), response.length, list)) {
        ESP_LOGW(TAG, "GET /api/speakers answered %d, %s: %s", response.status, problem, list.error.c_str());
    } else {
        ESP_LOGI(TAG, "The backend lists %zu speakers", list.speakers.size());
    }

    model_changed();

    // The chosen speaker is back in the list: what was last answered about it is from before it
    // went.
    if (was_held && !_model.needs_speaker()) {
        refresh_playback();
    }
}

void Application::refresh_playback() {
    if (_model.speaker_id.empty()) {
        return;
    }

    // A playback that is on screen stays there while a newer one is on its way.
    if (_model.playback.state != LoadState::Loaded) {
        _model.playback.state = LoadState::Loading;
        model_changed();
    }

    // An answer about a speaker that is no longer the chosen one is dropped.
    _api_client->send(HttpRequest{.path = speaker_path(_model.speaker_id, "/playback")}, _scope,
                      [this, speaker_id = _model.speaker_id](HttpResponse&& response) {
                          if (speaker_id == _model.speaker_id) {
                              playback_answered(std::move(response));
                          }
                      });
}

void Application::playback_answered(HttpResponse&& response) {
    auto& playback = _model.playback;

    if (response.err == ESP_ERR_INVALID_SIZE) {
        // An answer over the client's limit, which HttpClient logged.
        playback.state = LoadState::Failed;
        playback.error.clear();
    } else if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        playback.state = LoadState::Unreachable;
        playback.error.clear();
    } else if (const auto problem =
                   read_playback(response.status, (const char*)response.body.get(), response.length, playback)) {
        ESP_LOGW(TAG, "GET playback answered %d, %s: %s", response.status, problem, playback.error.c_str());
    }

    model_changed();
}

void Application::send_command(Command command) {
    ESP_LOGI(TAG, "Sending %s", command_name(command));

    _model.playback.command_sent(command);
    model_changed();

    const auto path = speaker_path(_model.speaker_id, "/playback/") + command_name(command);

    // An answer about a speaker that is no longer the chosen one is dropped.
    _api_client->send(HttpRequest{.method = HTTP_METHOD_POST, .path = path}, _scope,
                      [this, command, speaker_id = _model.speaker_id](HttpResponse&& response) {
                          if (speaker_id == _model.speaker_id) {
                              command_answered(command, std::move(response));
                          }
                      });
}

void Application::command_answered(Command command, HttpResponse&& response) {
    auto& playback = _model.playback;
    std::string error;

    // The speaker reports what a command changed later, as playback_changed. A command that was
    // not taken changed nothing, so the playback is fetched again: it puts the screen right, and
    // says which commands the speaker allows now.
    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        playback.command_not_taken();
        refresh_playback();
    } else if (const auto problem =
                   read_command(response.status, (const char*)response.body.get(), response.length, error)) {
        ESP_LOGW(TAG, "POST %s answered %d, %s: %s", command_name(command), response.status, problem, error.c_str());

        playback.command_not_taken();
        refresh_playback();
    } else {
        playback.command_taken(command);
    }

    model_changed();
}

void Application::event_received(const std::string& type, const cJSON* payload) {
    if (type == "speakers_changed") {
        refresh_speakers();
    } else if (type == "playback_changed") {
        // Sent once for every speaker of a group, so the chosen speaker's own id is the whole
        // filter. An event that does not say whose it is may be the chosen speaker's.
        std::string speaker_id;
        if (const auto problem = read_speaker_event(payload, speaker_id)) {
            ESP_LOGW(TAG, "Event %s: %s", type.c_str(), problem);

            refresh_playback();
        } else if (speaker_id == _model.speaker_id) {
            refresh_playback();
        }
    }
}

void Application::begin_backend() {
    ESP_LOGI(TAG, "Starting the backend tasks");

    _model.stream_state = StreamState::Connecting;
    _model.stream_down_since = lv_tick_get();
    model_changed();

    _api_client = std::make_unique<HttpClient>(&get_queue(), "backend_api");
    ESP_ERROR_CHECK(_api_client->begin(_model.backend_url, homelab_root_pem, BACKEND_TASK_STACK_SIZE,
                                       BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _art_client = std::make_unique<HttpClient>(&get_queue(), "backend_art");
    ESP_ERROR_CHECK(_art_client->begin(_model.backend_url, homelab_root_pem, BACKEND_TASK_STACK_SIZE,
                                       BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _event_stream = std::make_unique<EventStream>(&get_queue());

    _event_stream->on_connected([this]() {
        _model.stream_state = StreamState::Connected;
        model_changed();

        // Events sent while the stream was away are gone, so what the views show is fetched again.
        refresh_speakers();
        refresh_playback();
    });

    _event_stream->on_disconnected([this]() {
        _model.stream_state = StreamState::Disconnected;
        _model.stream_down_since = lv_tick_get();
        model_changed();
    });

    // Every connection receives every event, and its payload names what changed.
    _event_stream->on_event(
        [this](const std::string& type, EventStream::Payload payload) { event_received(type, payload.get()); });

    ESP_ERROR_CHECK(_event_stream->begin(_model.backend_url, homelab_root_pem, BACKEND_TASK_STACK_SIZE,
                                         BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    refresh_speakers();
    refresh_playback();
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
