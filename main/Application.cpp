#include "Application.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>

#include "api/Answers.h"
#include "board/RunMark.h"
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

// A 400 px cover is 320,012 bytes, over the client's limit for an API answer.
static constexpr size_t ARTWORK_MAX_RESPONSE = 512 * 1024;

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

// An id as a path segment. It is the backend's own text, so what a path segment cannot hold as it
// is goes in percent-encoded.
static std::string encode_segment(const std::string& id) {
    static constexpr char HEX[] = "0123456789ABCDEF";

    std::string segment;
    for (const auto c : id) {
        if (isalnum(uint8_t(c)) || strchr("-._~", c)) {
            segment += c;
        } else {
            segment += '%';
            segment += HEX[uint8_t(c) >> 4];
            segment += HEX[uint8_t(c) & 0xF];
        }
    }

    return segment;
}

// The path of a speaker's resource.
static std::string speaker_path(const std::string& speaker_id, const char* resource) {
    return "/api/speakers/" + encode_segment(speaker_id) + resource;
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

// The body of a join: the speaker whose group is joined. The id is the backend's own text, so
// cJSON writes it as a JSON string. Nothing when the heap cannot hold it.
static std::optional<std::string> join_body(const std::string& speaker_id) {
    const auto id = cJSON_CreateString(speaker_id.c_str());
    DEFER(cJSON_Delete(id));

    const auto text = cJSON_PrintUnformatted(id);
    if (!text) {
        return std::nullopt;
    }
    DEFER(cJSON_free(text));

    return std::string("{\"speaker_id\":") + text + "}";
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
    RunMark::reached(RunMark::Stage::Configured);

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
    RunMark::reached(RunMark::Stage::Ready);

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
    _model.volume.speaker_changed();
    model_changed();

    refresh_playback();
    refresh_volume();
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

        _model.grouping.listed(list);
    }

    model_changed();

    // The chosen speaker is back in the list: what was last answered about it is from before it
    // went.
    if (was_held && !_model.needs_speaker()) {
        refresh_playback();
        refresh_volume();
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

void Application::refresh_volume() {
    if (_model.speaker_id.empty()) {
        return;
    }

    // A level that is on screen stays there while a newer one is on its way.
    if (_model.volume.state != LoadState::Loaded) {
        _model.volume.state = LoadState::Loading;
        model_changed();
    }

    // An answer about a speaker that is no longer the chosen one is dropped.
    _api_client->send(HttpRequest{.path = speaker_path(_model.speaker_id, "")}, _scope,
                      [this, speaker_id = _model.speaker_id](HttpResponse&& response) {
                          if (speaker_id == _model.speaker_id) {
                              volume_answered(std::move(response));
                          }
                      });
}

void Application::volume_answered(HttpResponse&& response) {
    auto& volume = _model.volume;
    std::string error;

    if (response.err == ESP_ERR_INVALID_SIZE) {
        // An answer over the client's limit, which HttpClient logged.
        volume.state = LoadState::Failed;
    } else if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        volume.state = LoadState::Unreachable;
    } else if (const auto problem = read_speaker_volume(response.status, (const char*)response.body.get(),
                                                        response.length, volume, error)) {
        ESP_LOGW(TAG, "GET speaker answered %d, %s: %s", response.status, problem, error.c_str());
    }

    model_changed();
}

void Application::change_volume(int step) {
    _model.volume.pressed(step);
    model_changed();

    send_volume_change();
}

// backend_api runs one request at a time, and its queue drops a request when it is full. So at
// most one change of the volume is in it: the presses made while one is on its way go as one
// change when it is answered, and a transport command waits for that one change at most.
void Application::send_volume_change() {
    const auto delta = _model.volume.next_change();
    if (!delta) {
        return;
    }

    ESP_LOGI(TAG, "Sending a volume step of %d", *delta);

    // A step, not a level: the speaker takes it from the volume it has, which may not be the one
    // on screen. An answer about a speaker that is no longer the chosen one is dropped.
    _api_client->send(HttpRequest{.method = HTTP_METHOD_POST,
                                  .path = speaker_path(_model.speaker_id, "/group/volume"),
                                  .body = "{\"delta\":" + std::to_string(*delta) + "}"},
                      _scope, [this, speaker_id = _model.speaker_id](HttpResponse&& response) {
                          if (speaker_id == _model.speaker_id) {
                              volume_change_answered(std::move(response));
                          }
                      });
}

void Application::volume_change_answered(HttpResponse&& response) {
    auto& volume = _model.volume;
    std::string error;

    // Whether the speaker moved for a change that was not taken is not known, so the level is
    // fetched again.
    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        volume.change_not_taken();
        refresh_volume();
    } else if (const auto problem = read_volume_change(response.status, (const char*)response.body.get(),
                                                       response.length, volume, error)) {
        ESP_LOGW(TAG, "POST group/volume answered %d, %s: %s", response.status, problem, error.c_str());

        volume.change_not_taken();
        refresh_volume();
    } else {
        volume.change_taken();
        send_volume_change();
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

void Application::add_to_group(const std::string& speaker_id) { send_group_change(speaker_id, true); }

void Application::remove_from_group(const std::string& speaker_id) { send_group_change(speaker_id, false); }

void Application::send_group_change(const std::string& speaker_id, bool adds) {
    ESP_LOGI(TAG, "Sending speaker %s %s the group of %s", speaker_id.c_str(), adds ? "into" : "out of",
             _model.speaker_id.c_str());

    const auto change = _model.grouping.sent(speaker_id, _model.speaker_id, adds);
    model_changed();

    // A join goes to the speaker that joins, and names a speaker of the group: the group keeps
    // its coordinator, so it stays what the chosen speaker plays. A leave goes to the speaker
    // that leaves.
    HttpRequest request{.method = adds ? HTTP_METHOD_PUT : HTTP_METHOD_DELETE,
                        .path = speaker_path(speaker_id, "/group")};
    if (adds) {
        auto body = join_body(_model.speaker_id);

        // A body the heap cannot hold is a change not taken, as HttpClient answers a request it
        // cannot take.
        if (!body) {
            ESP_LOGE(TAG, "Cannot build the body of the join of %s", speaker_id.c_str());
            group_change_answered(change, adds, HttpResponse{.err = ESP_ERR_NO_MEM});
            return;
        }

        request.body = std::move(*body);
    }

    // The answer is the change's own, whichever speaker is the chosen one by then.
    _api_client->send(std::move(request), _scope, [this, change, adds](HttpResponse&& response) {
        group_change_answered(change, adds, std::move(response));
    });
}

void Application::group_change_answered(uint32_t change, bool adds, HttpResponse&& response) {
    auto& grouping = _model.grouping;
    std::string error;

    // A 204 says that the speaker took the change. The groups change later, and speakers_changed
    // then fetches the list that settles it. Whether the speaker moved for a change that was not
    // taken is not known, so the speakers are fetched again.
    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        grouping.not_taken(change);
        refresh_speakers();
    } else if (const auto problem =
                   read_command(response.status, (const char*)response.body.get(), response.length, error)) {
        ESP_LOGW(TAG, "%s group answered %d, %s: %s", adds ? "PUT" : "DELETE", response.status, problem, error.c_str());

        grouping.not_taken(change);
        refresh_speakers();
    } else {
        grouping.taken(change, lv_tick_get());
    }

    model_changed();
}

void Application::request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                                  std::function<void(Artwork)> done) {
    _artwork.request(ArtworkRequest{
        .artwork_id = artwork_id,
        .size = size,
        .scope = std::move(scope),
        .done = std::move(done),
    });
}

// Over backend_art, so a command is never queued behind a cover. The client drops the request,
// and its answer, once the view that asked is gone or wants another cover.
void Application::fetch_artwork(ArtworkRequest&& request) {
    auto path =
        "/api/artwork/" + encode_segment(request.artwork_id) + "?size=" + std::to_string(request.size) + "&format=lvgl";
    auto scope = request.scope;

    _art_client->send(HttpRequest{.path = std::move(path), .max_response = ARTWORK_MAX_RESPONSE}, std::move(scope),
                      [this, request = std::move(request)](HttpResponse&& response) {
                          artwork_answered(request, std::move(response));
                      });
}

void Application::artwork_answered(const ArtworkRequest& request, HttpResponse&& response) {
    // HttpClient logged a transport error, and an answer over the limit.
    if (response.err != ESP_OK) {
        _artwork.unanswered(request);
        return;
    }

    const auto problem =
        _artwork.answered(request, response.status, ArtworkBody(std::move(response.body)), response.length);
    if (problem) {
        ESP_LOGW(TAG, "GET artwork at %d px answered %d, %s", int(request.size), response.status, problem);
    } else {
        ESP_LOGI(TAG, "Artwork at %d px is in: %zu bytes, %zu in the cache", int(request.size), response.length,
                 _artwork.get_cache().get_bytes());
    }
}

void Application::event_received(const std::string& type, const cJSON* payload) {
    const auto about_playback = type == "playback_changed";

    if (type == "speakers_changed") {
        refresh_speakers();

        // A speaker that joined or left the chosen speaker's group changed the group's volume.
        refresh_volume();
    } else if (about_playback || type == "volume_changed") {
        // Sent once for every speaker of a group, so the chosen speaker's own id is the whole
        // filter. An event that does not say whose it is may be the chosen speaker's.
        std::string speaker_id;
        const auto problem = read_speaker_event(payload, speaker_id);
        if (problem) {
            ESP_LOGW(TAG, "Event %s: %s", type.c_str(), problem);
        }

        if (problem || speaker_id == _model.speaker_id) {
            if (about_playback) {
                refresh_playback();
            } else {
                refresh_volume();
            }
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
        refresh_volume();
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
    refresh_volume();
}

void Application::do_process() {
    _board->process();

    // Nothing is fetched for a screen nobody sees: a cover asked for in the dark comes once the
    // screen wakes.
    _artwork.set_screen_lit(_board->get_screen_power().is_lit());

    // A group change the backend took, which no list has shown within its time limit: the list
    // fetched now shows the groups as they are.
    if (_model.grouping.timed_out(lv_tick_get())) {
        model_changed();
        refresh_speakers();
    }

    if (_shell) {
        _shell->update();
    }

    // After the Shell's update, so a cover it asked for counts as a request under way. do_ready
    // restarted the panel before the backend's first connections were opened, and the first view
    // opened while they were; once they are made, the panel is restarted again.
    if (!_settled && has_settled()) {
        _settled = true;

        ESP_LOGI(TAG, "Startup has settled; restarting the panel");

        RunMark::reached(RunMark::Stage::Settled);
        _board->resync_panel();
    }

    log_memory();
}

// The first view is up and the backend's first connections are made: the event stream is
// connected, and neither client holds a request it has not answered. do_ready opens the first
// view as it starts the backend.
bool Application::has_settled() const {
    return _event_stream && _model.stream_state == StreamState::Connected && _api_client->is_idle() &&
           _art_client->is_idle();
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
