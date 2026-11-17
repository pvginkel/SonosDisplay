#include "Application.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstring>
#include <optional>

#include "api/Answers.h"
#include "api/Requests.h"
#include "board/RgbPanel.h"
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

// The UI task presents and restarts the panel, whose waits for its interrupts hold only on their
// core.
static_assert(UI_TASK_CORE == RgbPanel::INTERRUPT_CORE);

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

// Where the speaker choice and whether motion wakes the screen are stored: not in esp-mdm's
// namespace, whose unknown keys are deleted when provisioning data is renewed.
static constexpr char NVS_NAMESPACE[] = "sonos";
static constexpr char SPEAKER_NVS_KEY[] = "speaker";
static constexpr char MOTION_NVS_KEY[] = "motion_wakes";

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
    auto err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
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
    ESP_ERROR_RETURN(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    DEFER(nvs_close(handle));

    ESP_ERROR_RETURN(nvs_set_str(handle, SPEAKER_NVS_KEY, id.c_str()));

    return nvs_commit(handle);
}

static esp_err_t read_motion_setting(bool& wakes) {
    nvs_handle_t handle;
    auto err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }
    DEFER(nvs_close(handle));

    uint8_t value;
    err = nvs_get_u8(handle, MOTION_NVS_KEY, &value);
    if (err != ESP_OK) {
        return err;
    }

    wakes = value != 0;

    return ESP_OK;
}

static esp_err_t write_motion_setting(bool wakes) {
    nvs_handle_t handle;
    ESP_ERROR_RETURN(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    DEFER(nvs_close(handle));

    ESP_ERROR_RETURN(nvs_set_u8(handle, MOTION_NVS_KEY, wakes ? 1 : 0));

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
    load_motion_setting();

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

// The key is not found until the setting is first changed, and motion wakes the screen until then.
void Application::load_motion_setting() {
    const auto err = read_motion_setting(_model.motion_wakes);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Cannot read whether motion wakes the screen: %s", esp_err_to_name(err));
    }

    _board->get_screen_power().set_motion_wakes(_model.motion_wakes);
}

void Application::set_motion_wakes(bool wakes) {
    if (wakes == _model.motion_wakes) {
        return;
    }

    const auto err = write_motion_setting(wakes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot store whether motion wakes the screen: %s", esp_err_to_name(err));
    }

    _board->get_screen_power().set_motion_wakes(wakes);
    _model.motion_wakes = wakes;
    model_changed();
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

    // A burst of events asks for the speakers once more, not once per event.
    _api_client->send(HttpRequest{.path = "/api/speakers", .merges = true}, _scope,
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

    // An answer about a speaker that is no longer the chosen one is dropped. A burst of events
    // asks for the playback once more, not once per event.
    _api_client->send(HttpRequest{.path = speaker_path(_model.speaker_id, "/playback"), .merges = true}, _scope,
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

    // An answer about a speaker that is no longer the chosen one is dropped. volume_changed comes
    // once for every speaker of a group, and asks for the volume once more.
    _api_client->send(HttpRequest{.path = speaker_path(_model.speaker_id, ""), .merges = true}, _scope,
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

// backend_press runs every press, one request at a time and in the order sent; its queue drops a
// request when it is full. The presses made while a change of the volume is on its way go as one
// change when it is answered. A change its time limit settled may still be queued or under way, so
// a second change can wait behind it, as any press waits behind the presses sent before it.
void Application::send_volume_change() {
    const auto change = _model.volume.next_change(lv_tick_get());
    if (!change) {
        return;
    }

    ESP_LOGI(TAG, "Sending a volume step of %d%s", change->step, change->unmutes ? " with an unmute" : "");

    _press_client->send(
        HttpRequest{.method = HTTP_METHOD_POST,
                    .path = speaker_path(_model.speaker_id, "/group/volume"),
                    .body = write_volume_change(*change)},
        _scope, [this, id = change->id](HttpResponse&& response) { volume_change_answered(id, std::move(response)); });
}

void Application::volume_change_answered(uint32_t change, HttpResponse&& response) {
    auto& volume = _model.volume;
    std::string error;

    // The change was settled by its time limit, or another speaker was chosen: the level fetched
    // then is the one shown.
    if (!volume.is_sending(change)) {
        ESP_LOGW(TAG, "Ignoring the answer to a volume change that is settled");
        return;
    }

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
    _press_client->send(HttpRequest{.method = HTTP_METHOD_POST, .path = path}, _scope,
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

    const auto change = _model.grouping.sent(speaker_id, _model.speaker_id, adds, lv_tick_get());
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
    _press_client->send(std::move(request), _scope, [this, change, adds](HttpResponse&& response) {
        group_change_answered(change, adds, std::move(response));
    });
}

void Application::group_change_answered(uint32_t change, bool adds, HttpResponse&& response) {
    auto& grouping = _model.grouping;
    std::string error;

    // A list or the time limit settled the change, and the speakers fetched then show the groups.
    if (!grouping.is_pending(change)) {
        ESP_LOGW(TAG, "Ignoring the answer to a group change that is settled");
        return;
    }

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

void Application::queue_item(const QueueRequest& request) {
    if (request.start_track_id) {
        ESP_LOGI(TAG, "Queueing item %s from track %s on %s", request.item_id.c_str(), request.start_track_id->c_str(),
                 _model.speaker_id.c_str());
    } else {
        ESP_LOGI(TAG, "Queueing item %s on %s", request.item_id.c_str(), _model.speaker_id.c_str());
    }

    const auto id = _model.queueing.sent(request, lv_tick_get());
    model_changed();

    // A body the heap cannot hold is a request not taken, as HttpClient answers a request it
    // cannot take.
    auto body = write_queue_request(request);
    if (!body) {
        ESP_LOGE(TAG, "Cannot build the body of the queueing of %s", request.item_id.c_str());
        queue_answered(id, HttpResponse{.err = ESP_ERR_NO_MEM});
        return;
    }

    // The answer is the request's own, whichever speaker is the chosen one by then.
    _press_client->send(
        HttpRequest{
            .method = HTTP_METHOD_POST, .path = speaker_path(_model.speaker_id, "/queue"), .body = std::move(*body)},
        _scope, [this, id](HttpResponse&& response) { queue_answered(id, std::move(response)); });
}

void Application::queue_answered(uint32_t request, HttpResponse&& response) {
    auto& queueing = _model.queueing;
    std::string task_id;
    std::string error;

    // The time limit settled the request.
    if (!queueing.is_sending(request)) {
        ESP_LOGW(TAG, "Ignoring the answer to a queueing that is settled");
        return;
    }

    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        queueing.not_taken(request);
    } else if (const auto problem = read_queue_answer(response.status, (const char*)response.body.get(),
                                                      response.length, task_id, error)) {
        ESP_LOGW(TAG, "POST queue answered %d, %s: %s", response.status, problem, error.c_str());

        queueing.not_taken(request);
    } else {
        ESP_LOGI(TAG, "Task %s queues the item", task_id.c_str());

        queueing.taken(request, task_id);
    }

    model_changed();
}

void Application::change_favourite(const FavouriteRequest& request) {
    const auto marks = !request.favourite_id;
    if (marks) {
        ESP_LOGI(TAG, "Marking item %s as a favourite", request.item_id.c_str());
    } else {
        ESP_LOGI(TAG, "Unmarking favourite %" PRId64 " of item %s", *request.favourite_id, request.item_id.c_str());
    }

    const auto id = _model.favouring.sent(request, lv_tick_get());
    model_changed();

    HttpRequest http;
    if (marks) {
        auto body = write_favourite_mark(request);

        // A body the heap cannot hold is a change not taken, as HttpClient answers a request it
        // cannot take.
        if (!body) {
            ESP_LOGE(TAG, "Cannot build the body of the mark of %s", request.item_id.c_str());
            favourite_answered(id, marks, HttpResponse{.err = ESP_ERR_NO_MEM});
            return;
        }

        http = HttpRequest{.method = HTTP_METHOD_POST, .path = "/api/favourites", .body = std::move(*body)};
    } else {
        http = HttpRequest{.method = HTTP_METHOD_DELETE,
                           .path = "/api/favourites/" + std::to_string(*request.favourite_id)};
    }

    _press_client->send(std::move(http), _scope, [this, id, marks](HttpResponse&& response) {
        favourite_answered(id, marks, std::move(response));
    });
}

void Application::favourite_answered(uint32_t change, bool marks, HttpResponse&& response) {
    auto& favouring = _model.favouring;
    int64_t favourite_id = 0;
    std::string error;

    // The time limit settled the change.
    if (!favouring.is_sending(change)) {
        ESP_LOGW(TAG, "Ignoring the answer to a change of a favourite that is settled");
        return;
    }

    const auto body = (const char*)response.body.get();
    const auto problem = response.err != ESP_OK ? nullptr
                         : marks ? read_favourite_mark(response.status, body, response.length, favourite_id, error)
                                 : read_favourite_unmark(response.status, body, response.length, error);

    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        favouring.not_taken(change);
    } else if (problem) {
        ESP_LOGW(TAG, "%s favourites answered %d, %s: %s", marks ? "POST" : "DELETE", response.status, problem,
                 error.c_str());

        favouring.not_taken(change);
    } else if (marks) {
        ESP_LOGI(TAG, "Favourite %" PRId64 " marks the item", favourite_id);

        favouring.marked(change, favourite_id);
    } else {
        favouring.unmarked(change);
    }

    model_changed();
}

// Reads a page of a list the backend gives in pages, a Page or one that holds more of the answer,
// as `read` reads its body.
template <typename PageOf>
using PageReader = const char* (*)(int status, const char* body, size_t length, PageOf& page);

// Asks for page `number` of the list at `path` on `client`, which may hold a query of its own,
// PAGE_SIZE to a page, and hands `done` the page as `read` reads it, or why it did not come. The
// request goes as one with one for the same page that still waits, a press on Try again among
// them.
template <typename PageOf>
static void request_page(HttpClient& client, std::string path, uint32_t number, std::weak_ptr<void> scope,
                         std::function<void(PageOf)> done, PageReader<PageOf> read) {
    const auto query = path + (path.find('?') == std::string::npos ? "?" : "&") + "page=" + std::to_string(number) +
                       "&per_page=" + std::to_string(PAGE_SIZE);

    client.send(HttpRequest{.path = query, .merges = true}, std::move(scope),
                [path = std::move(path), number, done = std::move(done), read](HttpResponse&& response) {
                    PageOf page;

                    if (response.err == ESP_ERR_INVALID_SIZE) {
                        // An answer over the client's limit, which HttpClient logged.
                        page.state = LoadState::Failed;
                    } else if (response.err != ESP_OK) {
                        // HttpClient logged the transport error.
                        page.state = LoadState::Unreachable;
                    } else if (const auto problem =
                                   read(response.status, (const char*)response.body.get(), response.length, page)) {
                        ESP_LOGW(TAG, "GET %s page %" PRIu32 " answered %d, %s: %s", path.c_str(), number,
                                 response.status, problem, page.error.c_str());
                    } else {
                        ESP_LOGI(TAG, "GET %s page %" PRIu32 ": %zu of %" PRIu32, path.c_str(), number,
                                 page.items.size(), page.total);
                    }

                    done(std::move(page));
                });
}

void Application::request_artists(uint32_t number, std::weak_ptr<void> scope, std::function<void(Page<Artist>)> done) {
    request_page(*_api_client, "/api/library/artists", number, std::move(scope), std::move(done), read_artists);
}

// Without `sort_by` and `sort_dir`: newest first, by release date.
void Application::request_albums(const std::string& artist_id, uint32_t number, std::weak_ptr<void> scope,
                                 std::function<void(Page<Album>)> done) {
    request_page(*_api_client, "/api/library/artists/" + encode_segment(artist_id) + "/albums", number,
                 std::move(scope), std::move(done), read_albums);
}

void Application::request_tracks(const std::string& album_id, uint32_t number, std::weak_ptr<void> scope,
                                 std::function<void(Page<AlbumTrack>)> done) {
    request_page(*_api_client, "/api/library/albums/" + encode_segment(album_id) + "/tracks", number, std::move(scope),
                 std::move(done), read_tracks);
}

// Reads an item of the library, which the backend gives on its own, as `read` reads its body.
template <typename Item>
using ItemReader = const char* (*)(int status, const char* body, size_t length, Item& item, std::string& error);

// Asks for the item of the library at `path` on `client`, and hands `done` the item as `read` reads
// it, or none when it did not come. The request goes as one with one for the same item that still
// waits.
template <typename Item>
static void request_item(HttpClient& client, std::string path, std::weak_ptr<void> scope,
                         std::function<void(std::optional<Item>)> done, ItemReader<Item> read) {
    client.send(
        HttpRequest{.path = path, .merges = true}, std::move(scope),
        [path, done = std::move(done), read](HttpResponse&& response) {
            Item item;
            std::string error;

            if (response.err != ESP_OK) {
                // HttpClient logged the transport error, and an answer over the client's limit.
                done(std::nullopt);
            } else if (const auto problem =
                           read(response.status, (const char*)response.body.get(), response.length, item, error)) {
                ESP_LOGW(TAG, "GET %s answered %d, %s: %s", path.c_str(), response.status, problem, error.c_str());

                done(std::nullopt);
            } else {
                ESP_LOGI(TAG, "GET %s: %s", path.c_str(), item.favourite_id ? "marked" : "not marked");

                done(std::move(item));
            }
        });
}

void Application::request_artist(const std::string& artist_id, std::weak_ptr<void> scope,
                                 std::function<void(std::optional<Artist>)> done) {
    request_item(*_api_client, "/api/library/artists/" + encode_segment(artist_id), std::move(scope), std::move(done),
                 read_one_artist);
}

void Application::request_album(const std::string& album_id, std::weak_ptr<void> scope,
                                std::function<void(std::optional<Album>)> done) {
    request_item(*_api_client, "/api/library/albums/" + encode_segment(album_id), std::move(scope), std::move(done),
                 read_one_album);
}

void Application::request_playlists(uint32_t number, std::weak_ptr<void> scope,
                                    std::function<void(Page<Playlist>)> done) {
    request_page(*_api_client, "/api/library/playlists", number, std::move(scope), std::move(done), read_playlists);
}

void Application::request_favourites(uint32_t number, std::weak_ptr<void> scope,
                                     std::function<void(Page<Favourite>)> done) {
    request_page(*_api_client, "/api/favourites?sort_by=title", number, std::move(scope), std::move(done),
                 read_favourites);
}

void Application::request_queue(uint32_t number, bool locate, std::weak_ptr<void> scope,
                                std::function<void(QueuePage)> done) {
    request_page(*_api_client, speaker_path(_model.speaker_id, locate ? "/queue?locate=current" : "/queue"), number,
                 std::move(scope), std::move(done), read_queue);
}

void Application::clear_queue() {
    ESP_LOGI(TAG, "Clearing the queue of %s", _model.speaker_id.c_str());

    _press_client->send(HttpRequest{.method = HTTP_METHOD_DELETE, .path = speaker_path(_model.speaker_id, "/queue")},
                        _scope, [this](HttpResponse&& response) { clear_answered(std::move(response)); });
}

// The queue's events say what a Clear the backend took changed. One that was not taken changed
// nothing, and nothing waits on it: an answer that never comes leaves nothing on its way.
void Application::clear_answered(HttpResponse&& response) {
    std::string error;

    if (response.err != ESP_OK) {
        // HttpClient logged the transport error.
        _model.clears_not_taken++;
    } else if (const auto problem =
                   read_command(response.status, (const char*)response.body.get(), response.length, error)) {
        ESP_LOGW(TAG, "DELETE queue answered %d, %s: %s", response.status, problem, error.c_str());

        _model.clears_not_taken++;
    } else {
        return;
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

// Over backend_art, so a command is never queued behind a cover. In the app's own scope: the loader
// starts a download only while a view wants it, and keeps its answer whether or not one still does.
void Application::fetch_artwork(ArtworkDownload&& download) {
    auto path = "/api/artwork/" + encode_segment(download.artwork_id) + "?size=" + std::to_string(download.size) +
                "&format=lvgl";

    _art_client->send(HttpRequest{.path = std::move(path), .max_response = ARTWORK_MAX_RESPONSE}, _scope,
                      [this, download = std::move(download)](HttpResponse&& response) {
                          artwork_answered(download, std::move(response));
                      });
}

void Application::artwork_answered(const ArtworkDownload& download, HttpResponse&& response) {
    // HttpClient logged a transport error, and an answer over the limit.
    if (response.err != ESP_OK) {
        _artwork.unanswered();
        return;
    }

    const auto outcome = _artwork.answered(response.status, ArtworkBody(std::move(response.body)), response.length);
    if (outcome.problem) {
        ESP_LOGW(TAG, "GET artwork at %d px answered %d, %s", int(download.size), response.status, outcome.problem);
    } else if (!outcome.handed) {
        ESP_LOGI(TAG, "Artwork at %d px is in with no view waiting for it, kept: %zu bytes, %zu in the cache",
                 int(download.size), response.length, _artwork.get_cache().get_bytes());
    } else {
        ESP_LOGI(TAG, "Artwork at %d px is in: %zu bytes, %zu in the cache", int(download.size), response.length,
                 _artwork.get_cache().get_bytes());
    }
}

void Application::event_received(const std::string& type, const cJSON* payload) {
    const auto about_playback = type == "playback_changed";
    const auto about_queue = type == "queue_changed";

    if (type == "speakers_changed") {
        refresh_speakers();

        // A speaker that joined or left the chosen speaker's group changed the group's volume.
        refresh_volume();
    } else if (about_playback || about_queue || type == "volume_changed") {
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
            } else if (!about_queue) {
                refresh_volume();
            }

            // The view that shows the queue fetches it again: the queue changed, or the track it
            // has loaded.
            if (about_playback || about_queue) {
                _model.queue_changes++;
                model_changed();
            }
        }
    } else if (type == "favourites_changed") {
        _model.favourites_changes++;
        model_changed();
    } else if (type == "task_event") {
        task_event_received(payload);
    }
}

// Every connection receives the events of every task, so the display watches for its own.
void Application::task_event_received(const cJSON* payload) {
    std::optional<TaskEnd> end;
    if (const auto problem = read_task_event(payload, end)) {
        ESP_LOGW(TAG, "Event task_event: %s", problem);
        return;
    }

    if (!end) {
        return;
    }

    if (end->error) {
        ESP_LOGI(TAG, "Task %s failed: %s", end->task_id.c_str(), end->error->c_str());
    }

    if (_model.queueing.task_ended(std::move(*end))) {
        model_changed();
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

    _press_client = std::make_unique<HttpClient>(&get_queue(), "backend_press");
    ESP_ERROR_CHECK(_press_client->begin(_model.backend_url, homelab_root_pem, BACKEND_TASK_STACK_SIZE,
                                         BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _art_client = std::make_unique<HttpClient>(&get_queue(), "backend_art");
    ESP_ERROR_CHECK(_art_client->begin(_model.backend_url, homelab_root_pem, BACKEND_TASK_STACK_SIZE,
                                       BACKEND_TASK_PRIORITY, BACKEND_TASK_CORE));

    _event_stream = std::make_unique<EventStream>(&get_queue());

    _event_stream->on_connected([this]() {
        // Events sent while the stream was away are gone, so what the views show is fetched again.
        _model.stream_state = StreamState::Connected;
        _model.favourites_changes++;
        model_changed();

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
    // screen wakes. On every pass, which is also where a download whose answer HttpClient dropped
    // on its way is found.
    _artwork.set_screen_lit(_board->get_screen_power().is_lit());

    const auto now = lv_tick_get();

    // A group change the backend took, which no list has shown within its time limit, or one
    // whose answer has not come within its own: the list fetched now shows the groups as they are.
    if (_model.grouping.timed_out(now)) {
        ESP_LOGI(TAG, "A group change is settled by its time limit");

        model_changed();
        refresh_speakers();
    }

    // A change of the volume whose answer has not come within its time limit: the level fetched
    // now is the one the speaker has.
    if (_model.volume.timed_out(now)) {
        ESP_LOGW(TAG, "A volume change had no answer within %d s", int(ANSWER_TIME_LIMIT_MS / 1000));

        model_changed();
        refresh_volume();
    }

    if (_model.queueing.timed_out(now)) {
        ESP_LOGW(TAG, "A queueing had no answer within %d s", int(ANSWER_TIME_LIMIT_MS / 1000));

        model_changed();
    }

    if (_model.favouring.timed_out(now)) {
        ESP_LOGW(TAG, "A change of a favourite had no answer within %d s", int(ANSWER_TIME_LIMIT_MS / 1000));

        model_changed();
    }

    if (_shell) {
        _shell->update();
    }

    // Marks the run's Settled stage once, for the next boot's log (RunMark). After the Shell's
    // update, so a cover it asked for counts as a request under way.
    if (!_settled && has_settled()) {
        _settled = true;

        ESP_LOGI(TAG, "Startup has settled");

        RunMark::reached(RunMark::Stage::Settled);
    }

    log_memory();
}

// The first view is up and the backend's first connections are made: the event stream is
// connected, and no client holds a request it has not answered. do_ready opens the first view as
// it starts the backend.
bool Application::has_settled() const {
    return _event_stream && _model.stream_state == StreamState::Connected && _api_client->is_idle() &&
           _press_client->is_idle() && _art_client->is_idle();
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
