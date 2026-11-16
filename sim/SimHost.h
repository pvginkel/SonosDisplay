#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "UiHost.h"
#include "api/Artwork.h"
#include "lvgl.h"

// A piece of artwork the views had fetched from the backend.
struct ArtworkFetch {
    std::string artwork_id;
    int32_t size;

    bool operator==(const ArtworkFetch&) const = default;
};

// A change of a group the views had sent to the backend: `speaker_id` joins the group of
// `group_of`, or leaves it.
struct GroupChangeRequest {
    std::string speaker_id;
    std::string group_of;
    bool adds;

    bool operator==(const GroupChangeRequest&) const = default;
};

class SimHost : public UiHost {
    struct Answer {
        int status;
        std::string body;
    };

    // A change of a group sent to the backend and not answered yet.
    struct UnansweredGroupChange {
        uint32_t id;
        bool adds;
    };

    struct ArtworkAnswer {
        int status;
        std::vector<uint8_t> body;
    };

    Model _model;
    std::vector<std::optional<lv_point_t>> _taps;
    // What answers on LVGL's clock, and from when.
    std::vector<std::pair<uint32_t, std::function<void()>>> _later;
    std::vector<Command> _commands;
    std::optional<Answer> _command_answer;
    std::optional<Answer> _next_playback;
    std::vector<int> _volume_changes;
    std::vector<std::string> _volume_change_bodies;
    std::vector<uint32_t> _unanswered_volume_changes;
    std::optional<Answer> _volume_change_answer;
    std::optional<Answer> _next_volume;
    std::optional<Answer> _next_speakers;
    std::vector<GroupChangeRequest> _group_changes;
    std::vector<UnansweredGroupChange> _unanswered_group_changes;
    std::optional<Answer> _group_change_answer;
    ArtworkLoader _artwork{[this](ArtworkRequest&& request) { fetch_artwork(std::move(request)); }};
    std::vector<ArtworkFetch> _artwork_fetches;
    std::optional<ArtworkAnswer> _artwork_answer;
    bool _artwork_held{};
    bool _artwork_reachable{true};
    std::vector<ArtworkRequest> _held_artwork;

public:
    // From one of a scene's touches to the next: what the first one changed is on screen by then.
    static constexpr uint32_t TAP_INTERVAL_MS = 200;

    SimHost();

    Model& model() { return _model; }
    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void refresh_speakers() override;
    void refresh_playback() override;
    void refresh_volume() override;
    void change_volume(int step) override;
    void send_command(Command command) override;
    void add_to_group(const std::string& speaker_id) override;
    void remove_from_group(const std::string& speaker_id) override;
    void request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                         std::function<void(Artwork)> done) override;

    // What the device does on every turn of its loop: the answers due by now come, and a change
    // of a group or of the volume past its time limit is settled, and the speakers or the volume
    // are asked for again.
    void process();

    // Runs `answer` in process() once LVGL's clock has moved on by `ms` from now, as an answer that
    // comes that late.
    void answer_after(uint32_t ms, std::function<void()> answer);

    // The backend's answer to GET /api/speakers, read as the device reads it.
    void answer_speakers(int status, const char* body);

    // What the backend answers the next time the speakers are asked for. Without it the request
    // stays unanswered.
    void answer_next_speakers(int status, const char* body) { _next_speakers = Answer{status, body}; }

    // What the backend answers every change of a group from here on, read as the device reads it.
    // Without it a change stays on its way.
    void answer_group_changes(int status, const char* body) { _group_change_answer = Answer{status, body}; }

    // The backend's answer to the oldest change of a group that answer_group_changes() did not
    // answer, read as the device reads it: an answer to a change that is settled by now changes
    // nothing.
    void answer_group_change(int status, const char* body);

    // How many changes of groups are not answered yet, settled ones among them.
    size_t count_unanswered_group_changes() const { return _unanswered_group_changes.size(); }

    // The changes of groups the views have sent, in order.
    const std::vector<GroupChangeRequest>& get_group_changes() const { return _group_changes; }

    // The backend's answer to GET /api/speakers/{speaker_id}/playback, read as the device reads
    // it.
    void answer_playback(int status, const char* body);

    // What the backend answers the next time the playback is asked for. Without it the request
    // stays unanswered.
    void answer_next_playback(int status, const char* body) { _next_playback = Answer{status, body}; }

    // What the backend answers every command from here on, read as the device reads it. Without
    // it a command stays on its way.
    void answer_commands(int status, const char* body) { _command_answer = Answer{status, body}; }

    // The commands the views have sent, in order.
    const std::vector<Command>& get_commands() const { return _commands; }

    // The backend's answer to GET /api/speakers/{speaker_id}, read as the device reads it.
    void answer_volume(int status, const char* body);

    // What the backend answers the next time the volume is asked for. Without it the request
    // stays unanswered.
    void answer_next_volume(int status, const char* body) { _next_volume = Answer{status, body}; }

    // The backend's answer to the oldest change of the volume that answer_volume_changes() did not
    // answer, read as the device reads it: an answer to a change that is settled by now changes
    // nothing. The steps pressed since go as the next change.
    void answer_volume_change(int status, const char* body);

    // How many changes of the volume are not answered yet, settled ones among them.
    size_t count_unanswered_volume_changes() const { return _unanswered_volume_changes.size(); }

    // What the backend answers every change of the volume from here on. Without it a change
    // stays on its way.
    void answer_volume_changes(int status, const char* body) { _volume_change_answer = Answer{status, body}; }

    // The steps of the changes of the volume sent to the backend, in order.
    const std::vector<int>& get_volume_changes() const { return _volume_changes; }

    // The bodies of the changes of the volume sent to the backend, in order, as the device writes
    // them.
    const std::vector<std::string>& get_volume_change_bodies() const { return _volume_change_bodies; }

    // What the backend answers a request for artwork with: LVGL 9's binary image, a deterministic
    // pattern of two colours that the id decides, `size` by `size` pixels.
    static std::vector<uint8_t> make_artwork(const std::string& artwork_id, int32_t size);

    // What the backend answers every request for artwork from here on, read as the device reads
    // it. Without it the answer is make_artwork()'s.
    void answer_artwork_with(int status, std::vector<uint8_t> body) {
        _artwork_answer = ArtworkAnswer{status, std::move(body)};
    }

    // From here on a request for artwork is answered with make_artwork()'s again.
    void answer_artwork_as_made() { _artwork_answer.reset(); }

    // From here on the backend does not answer a request for artwork, or answers it again.
    void set_artwork_reachable(bool reachable) { _artwork_reachable = reachable; }

    // From here on a request for artwork stays on its way until answer_artwork().
    void hold_artwork() { _artwork_held = true; }

    // Answers the requests for artwork that are on their way. As on the device, one whose asker
    // is gone or wants another picture by now is dropped unanswered.
    void answer_artwork();

    // Switches the screen off, or on. The device fetches no artwork while it is dark.
    void set_screen_lit(bool lit) { _artwork.set_screen_lit(lit); }

    // How many requests for artwork wait for the screen to wake.
    size_t count_artwork_waiting() const { return _artwork.count_waiting(); }

    // The artwork fetched from the backend, in order: what the cache had is not in it.
    const std::vector<ArtworkFetch>& get_artwork_fetches() const { return _artwork_fetches; }

    // Whether a view holds this image, which it does for as long as it draws it.
    bool is_artwork_shown(const std::string& artwork_id, int32_t size) const {
        return _artwork.get_cache().is_pinned(artwork_id, size);
    }

    // Touches the screen at a point, and lifts 100 ms later. The first touch is made as the scene
    // starts, and each one after it TAP_INTERVAL_MS after the one before.
    void tap(int32_t x, int32_t y) { _taps.push_back(lv_point_t{x, y}); }

    // Touches nothing for `ms`, rounded up to whole TAP_INTERVAL_MS: the next touch comes that
    // much later.
    void wait(uint32_t ms) { _taps.resize(_taps.size() + (ms + TAP_INTERVAL_MS - 1) / TAP_INTERVAL_MS); }

    // A scene's touches, in order; an empty one touches nothing.
    const std::vector<std::optional<lv_point_t>>& get_taps() const { return _taps; }

private:
    void send_volume_change();
    void volume_change_answered(uint32_t change, int status, const char* body);
    void send_group_change(const std::string& speaker_id, bool adds);
    void group_change_answered(const UnansweredGroupChange& change, int status, const char* body);
    void fetch_artwork(ArtworkRequest&& request);
    void answer_artwork(const ArtworkRequest& request);
};
