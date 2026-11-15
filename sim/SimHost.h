#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include <optional>
#include <string>
#include <vector>

#include "UiHost.h"
#include "lvgl.h"

class SimHost : public UiHost {
    struct Answer {
        int status;
        std::string body;
    };

    Model _model;
    std::vector<lv_point_t> _taps;
    std::vector<Command> _commands;
    std::optional<Answer> _command_answer;
    std::optional<Answer> _next_playback;
    std::vector<int> _volume_changes;
    std::optional<Answer> _volume_change_answer;
    std::optional<Answer> _next_volume;

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

    // Touches the screen at a point, and lifts 100 ms later. The first touch is made as the scene
    // starts, and each one after it 200 ms after the one before.
    void tap(int32_t x, int32_t y) { _taps.push_back(lv_point_t{x, y}); }
    const std::vector<lv_point_t>& get_taps() const { return _taps; }

private:
    void send_volume_change();
};
