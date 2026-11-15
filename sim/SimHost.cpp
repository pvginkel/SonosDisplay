#include "SimHost.h"

SimHost::SimHost() {
    _model.firmware_version = "0123abc";
    _model.device_name = "Garage display";
    _model.backend_url = "http://sonos-controller.example:5000";
    _model.stream_state = StreamState::Connected;
}
