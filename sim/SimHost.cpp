#include "SimHost.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "api/Answers.h"
#include "api/Requests.h"

SimHost::SimHost() {
    _model.backend_url = "http://sonos-controller.example:5000";
    _model.stream_state = StreamState::Connected;
}

void SimHost::choose_speaker(const std::string& id) {
    if (id == _model.speaker_id) {
        return;
    }

    _model.speaker_id = id;
    _model.playback.speaker_changed();
    _model.volume.speaker_changed();
    _model.revision++;

    refresh_playback();
    refresh_volume();
}

void SimHost::refresh_speakers() {
    if (_model.speakers.state != LoadState::Loaded) {
        _model.speakers.state = LoadState::Loading;
        _model.revision++;
    }

    if (_next_speakers) {
        const auto answer = std::move(*_next_speakers);
        _next_speakers.reset();

        answer_speakers(answer.status, answer.body.c_str());
    }
}

void SimHost::refresh_playback() {
    if (_model.playback.state != LoadState::Loaded) {
        _model.playback.state = LoadState::Loading;
        _model.revision++;
    }

    if (_next_playback) {
        const auto answer = std::move(*_next_playback);
        _next_playback.reset();

        answer_playback(answer.status, answer.body.c_str());
    }
}

void SimHost::refresh_volume() {
    if (_model.volume.state != LoadState::Loaded) {
        _model.volume.state = LoadState::Loading;
        _model.revision++;
    }

    if (_next_volume) {
        const auto answer = std::move(*_next_volume);
        _next_volume.reset();

        answer_volume(answer.status, answer.body.c_str());
    }
}

void SimHost::change_volume(int step) {
    _model.volume.pressed(step);
    _model.revision++;

    send_volume_change();
}

void SimHost::send_volume_change() {
    const auto change = _model.volume.next_change(lv_tick_get());
    if (!change) {
        return;
    }

    _volume_changes.push_back(change->step);
    _volume_change_bodies.push_back(write_volume_change(*change));

    if (_volume_change_answer) {
        volume_change_answered(change->id, _volume_change_answer->status, _volume_change_answer->body.c_str());
    } else {
        _unanswered_volume_changes.push_back(change->id);
    }
}

void SimHost::send_command(Command command) {
    _commands.push_back(command);

    _model.playback.command_sent(command);
    _model.revision++;

    if (!_command_answer) {
        return;
    }

    std::string error;
    const auto problem =
        read_command(_command_answer->status, _command_answer->body.c_str(), _command_answer->body.length(), error);
    if (problem) {
        printf("W (SimHost) POST command answered %d, %s: %s\n", _command_answer->status, problem, error.c_str());

        _model.playback.command_not_taken();
        refresh_playback();
    } else {
        _model.playback.command_taken(command);
    }

    _model.revision++;
}

void SimHost::add_to_group(const std::string& speaker_id) { send_group_change(speaker_id, true); }

void SimHost::remove_from_group(const std::string& speaker_id) { send_group_change(speaker_id, false); }

void SimHost::send_group_change(const std::string& speaker_id, bool adds) {
    _group_changes.push_back(GroupChangeRequest{speaker_id, _model.speaker_id, adds});

    const auto change = _model.grouping.sent(speaker_id, _model.speaker_id, adds, lv_tick_get());
    _model.revision++;

    if (_group_change_answer) {
        group_change_answered(UnansweredGroupChange{change, adds}, _group_change_answer->status,
                              _group_change_answer->body.c_str());
    } else {
        _unanswered_group_changes.push_back(UnansweredGroupChange{change, adds});
    }
}

void SimHost::answer_group_change(int status, const char* body) {
    if (_unanswered_group_changes.empty()) {
        fprintf(stderr, "render: a change of a group was answered, and none was sent\n");
        exit(1);
    }

    const auto change = _unanswered_group_changes.front();
    _unanswered_group_changes.erase(_unanswered_group_changes.begin());

    group_change_answered(change, status, body);
}

void SimHost::group_change_answered(const UnansweredGroupChange& change, int status, const char* body) {
    if (!_model.grouping.is_pending(change.id)) {
        return;
    }

    std::string error;
    const auto problem = read_command(status, body, strlen(body), error);
    if (problem) {
        printf("W (SimHost) %s group answered %d, %s: %s\n", change.adds ? "PUT" : "DELETE", status, problem,
               error.c_str());

        _model.grouping.not_taken(change.id);
        refresh_speakers();
    } else {
        _model.grouping.taken(change.id, lv_tick_get());
    }

    _model.revision++;
}

void SimHost::answer_after(uint32_t ms, std::function<void()> answer) {
    _later.emplace_back(lv_tick_get() + ms, std::move(answer));
}

void SimHost::process() {
    const auto now = lv_tick_get();

    // One at a time, since an answer may bring another.
    while (true) {
        const auto due = std::find_if(_later.begin(), _later.end(),
                                      [now](const auto& later) { return int32_t(now - later.first) >= 0; });
        if (due == _later.end()) {
            break;
        }

        const auto answer = std::move(due->second);
        _later.erase(due);

        answer();
    }

    if (_model.grouping.timed_out(now)) {
        _model.revision++;
        refresh_speakers();
    }

    if (_model.volume.timed_out(now)) {
        _model.revision++;
        refresh_volume();
    }
}

void SimHost::request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                              std::function<void(Artwork)> done) {
    _artwork.request(ArtworkRequest{
        .artwork_id = artwork_id,
        .size = size,
        .scope = std::move(scope),
        .done = std::move(done),
    });
}

void SimHost::fetch_artwork(ArtworkRequest&& request) {
    _artwork_fetches.push_back(ArtworkFetch{request.artwork_id, request.size});

    if (_artwork_held) {
        _held_artwork.push_back(std::move(request));
    } else {
        answer_artwork(request);
    }
}

void SimHost::answer_artwork() {
    const auto held = std::move(_held_artwork);
    _held_artwork.clear();

    for (const auto& request : held) {
        if (!request.scope.expired()) {
            answer_artwork(request);
        }
    }
}

void SimHost::answer_artwork(const ArtworkRequest& request) {
    if (!_artwork_reachable) {
        _artwork.unanswered(request);
        return;
    }

    const auto status = _artwork_answer ? _artwork_answer->status : 200;
    const auto bytes = std::make_shared<std::vector<uint8_t>>(
        _artwork_answer ? _artwork_answer->body : make_artwork(request.artwork_id, request.size));

    const auto problem = _artwork.answered(request, status, ArtworkBody(bytes, bytes->data()), bytes->size());
    if (problem) {
        printf("W (SimHost) GET artwork at %d px answered %d, %s\n", int(request.size), status, problem);
    }
}

std::vector<uint8_t> SimHost::make_artwork(const std::string& artwork_id, int32_t size) {
    static constexpr int32_t STRIPES = 8;

    // FNV-1a over the id: one id, one picture.
    uint32_t hash = 2166136261u;
    for (const auto c : artwork_id) {
        hash = (hash ^ uint8_t(c)) * 16777619u;
    }

    // A colour of middling brightness, and a darker shade of it.
    const auto rgb565 = [](uint32_t r, uint32_t g, uint32_t b) {
        return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    };
    const auto r = 64 + (hash & 0x7F);
    const auto g = 64 + ((hash >> 8) & 0x7F);
    const auto b = 64 + ((hash >> 16) & 0x7F);
    const uint16_t colors[] = {rgb565(r, g, b), rgb565(r / 3, g / 3, b / 3)};

    const auto stride = size * 2;

    std::vector<uint8_t> image = {
        0x19,
        0x12,
        0,
        0,
        uint8_t(size),
        uint8_t(size >> 8),
        uint8_t(size),
        uint8_t(size >> 8),
        uint8_t(stride),
        uint8_t(stride >> 8),
        0,
        0,
    };
    image.reserve(image.size() + size_t(size) * size_t(stride));

    // Diagonal stripes.
    for (int32_t y = 0; y < size; y++) {
        for (int32_t x = 0; x < size; x++) {
            const auto color = colors[(x + y) * STRIPES / size % 2];

            image.push_back(uint8_t(color));
            image.push_back(uint8_t(color >> 8));
        }
    }

    return image;
}

void SimHost::answer_speakers(int status, const char* body) {
    const auto problem = read_speakers(status, body, strlen(body), _model.speakers);
    if (problem) {
        printf("W (SimHost) GET /api/speakers answered %d, %s: %s\n", status, problem, _model.speakers.error.c_str());
    } else {
        _model.grouping.listed(_model.speakers);
    }

    _model.revision++;
}

void SimHost::answer_playback(int status, const char* body) {
    const auto problem = read_playback(status, body, strlen(body), _model.playback);
    if (problem) {
        printf("W (SimHost) GET playback answered %d, %s: %s\n", status, problem, _model.playback.error.c_str());
    }

    _model.revision++;
}

void SimHost::answer_volume(int status, const char* body) {
    std::string error;
    const auto problem = read_speaker_volume(status, body, strlen(body), _model.volume, error);
    if (problem) {
        printf("W (SimHost) GET speaker answered %d, %s: %s\n", status, problem, error.c_str());
    }

    _model.revision++;
}

void SimHost::answer_volume_change(int status, const char* body) {
    if (_unanswered_volume_changes.empty()) {
        fprintf(stderr, "render: a change of the volume was answered, and none was sent\n");
        exit(1);
    }

    const auto change = _unanswered_volume_changes.front();
    _unanswered_volume_changes.erase(_unanswered_volume_changes.begin());

    volume_change_answered(change, status, body);
}

void SimHost::volume_change_answered(uint32_t change, int status, const char* body) {
    if (!_model.volume.is_sending(change)) {
        return;
    }

    std::string error;
    const auto problem = read_volume_change(status, body, strlen(body), _model.volume, error);
    if (problem) {
        printf("W (SimHost) POST group/volume answered %d, %s: %s\n", status, problem, error.c_str());

        _model.volume.change_not_taken();
        refresh_volume();
    } else {
        _model.volume.change_taken();
        send_volume_change();
    }

    _model.revision++;
}
