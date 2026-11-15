#include "SimHost.h"

#include <cstdio>
#include <cstring>

#include "api/Answers.h"

SimHost::SimHost() {
    _model.backend_url = "http://sonos-controller.example:5000";
    _model.stream_state = StreamState::Connected;
}

void SimHost::choose_speaker(const std::string& id) {
    _model.speaker_id = id;
    _model.revision++;
}

void SimHost::refresh_speakers() {
    if (_model.speakers.state != LoadState::Loaded) {
        _model.speakers.state = LoadState::Loading;
        _model.revision++;
    }
}

void SimHost::answer_speakers(int status, const char* body) {
    const auto problem = read_speakers(status, body, strlen(body), _model.speakers);
    if (problem) {
        printf("W (SimHost) GET /api/speakers answered %d, %s: %s\n", status, problem, _model.speakers.error.c_str());
    }

    _model.revision++;
}
