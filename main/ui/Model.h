#pragma once

// What the views draw: plain structs, filled by the host. One of the two doors between ui/ and
// the rest of the firmware; UiHost.h is the other.

#include <cstdint>
#include <string>

enum class StreamState {
    // Not connected yet since boot.
    Connecting,
    Connected,
    // Was connected, and lost it.
    Disconnected,
};

struct Model {
    // Bumped by the host on every change, so a view redraws only when something changed.
    uint32_t revision{};

    std::string firmware_version;
    std::string device_name;

    // The backend's base URL from IoT Support's device configuration; empty when the
    // configuration has none, or none that is valid.
    std::string backend_url;

    StreamState stream_state{StreamState::Connecting};

    // LVGL's tick when the event stream was last lost, or when the app started connecting.
    uint32_t stream_down_since{};
};
