#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include <cstdint>
#include <optional>
#include <string>
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

class SimHost : public UiHost {
    struct Answer {
        int status;
        std::string body;
    };

    struct ArtworkAnswer {
        int status;
        std::vector<uint8_t> body;
    };

    Model _model;
    std::vector<lv_point_t> _taps;
    std::vector<Command> _commands;
    std::optional<Answer> _command_answer;
    std::optional<Answer> _next_playback;
    std::vector<int> _volume_changes;
    std::optional<Answer> _volume_change_answer;
    std::optional<Answer> _next_volume;
    ArtworkLoader _artwork{[this](ArtworkRequest&& request) { fetch_artwork(std::move(request)); }};
    std::vector<ArtworkFetch> _artwork_fetches;
    std::optional<ArtworkAnswer> _artwork_answer;
    bool _artwork_held{};
    bool _artwork_reachable{true};
    std::vector<ArtworkRequest> _held_artwork;

public:
    SimHost();

    Model& model() { return _model; }
    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void refresh_speakers() override;
    void refresh_playback() override;
    void refresh_volume() override;
    void change_volume(int step) override;
    void send_command(Command command) override;
    void request_artwork(const std::string& artwork_id, int32_t size, std::weak_ptr<void> scope,
                         std::function<void(Artwork)> done) override;

    // The backend's answer to GET /api/speakers, read as the device reads it.
    void answer_speakers(int status, const char* body);

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

    // The backend's answer to the change of the volume that is on its way, read as the device
    // reads it. The steps pressed since go as the next change.
    void answer_volume_change(int status, const char* body);

    // What the backend answers every change of the volume from here on. Without it a change
    // stays on its way.
    void answer_volume_changes(int status, const char* body) { _volume_change_answer = Answer{status, body}; }

    // The steps of the changes of the volume sent to the backend, in order.
    const std::vector<int>& get_volume_changes() const { return _volume_changes; }

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
    // starts, and each one after it 200 ms after the one before.
    void tap(int32_t x, int32_t y) { _taps.push_back(lv_point_t{x, y}); }
    const std::vector<lv_point_t>& get_taps() const { return _taps; }

private:
    void send_volume_change();
    void fetch_artwork(ArtworkRequest&& request);
    void answer_artwork(const ArtworkRequest& request);
};
