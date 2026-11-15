#include "SimHost.h"

#include <cstdio>
#include <cstring>

#include "api/Answers.h"

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
    const auto delta = _model.volume.next_change();
    if (!delta) {
        return;
    }

    _volume_changes.push_back(*delta);

    if (_volume_change_answer) {
        answer_volume_change(_volume_change_answer->status, _volume_change_answer->body.c_str());
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

void SimHost::answer_speakers(int status, const char* body) {
    const auto problem = read_speakers(status, body, strlen(body), _model.speakers);
    if (problem) {
        printf("W (SimHost) GET /api/speakers answered %d, %s: %s\n", status, problem, _model.speakers.error.c_str());
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
