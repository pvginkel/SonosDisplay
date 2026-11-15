#pragma once

// What the views draw: plain structs, filled by the host. One of the two doors between ui/ and
// the rest of the firmware; UiHost.h is the other.

#include <cstdint>
#include <string>
#include <vector>

enum class StreamState {
    // Not connected yet since boot.
    Connecting,
    Connected,
    // Was connected, and lost it.
    Disconnected,
};

// How the last request for a piece of the backend's state went.
enum class LoadState {
    // Asked for, and not answered yet.
    Loading,
    Loaded,
    // The backend could not be reached.
    Unreachable,
    // The backend answered an error, or something this display cannot read.
    Failed,
};

struct Speaker {
    // The speaker's id as the API gives it.
    std::string id;
    std::string name;

    bool operator==(const Speaker&) const = default;
};

struct SpeakerList {
    LoadState state{LoadState::Loading};

    // What the backend said went wrong, when the state is Failed and it said so.
    std::string error;

    // The last list the backend answered, which a later failure leaves in place; empty until it
    // has answered one.
    std::vector<Speaker> speakers;

    bool operator==(const SpeakerList&) const = default;
};

struct Model {
    // Bumped by the host on every change, so a view redraws only when something changed.
    uint32_t revision{};

    // The backend's base URL from IoT Support's device configuration; empty when the
    // configuration has none, or none that is valid.
    std::string backend_url;

    StreamState stream_state{StreamState::Connecting};

    // LVGL's tick when the event stream was last lost, or when the app started connecting.
    uint32_t stream_down_since{};

    // The household's speakers.
    SpeakerList speakers;

    // The id of the speaker this display controls, as stored on the display; empty while none is
    // chosen. Only a pick changes it: the list may lack it.
    std::string speaker_id;
};
