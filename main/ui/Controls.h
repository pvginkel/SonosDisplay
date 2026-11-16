#pragma once

// The controls the now-playing screen and the now-playing bar share, and the rules by which they
// are live: previous, play or pause, and next, each only while the speaker allows its command, and
// the volume of the speaker's group, down, the level, and up. And what both say for a while after
// a press.

#include <cstdint>
#include <string>

#include "UiHost.h"
#include "lvgl.h"

// How long a view says what came of a press.
inline constexpr uint32_t NOTICE_MS = 5000;

// What a view says for NOTICE_MS of what came of the presses made on the display: that a command,
// a change of the volume or of a group, or a request to queue an item was not taken, that the
// backend failed to queue one, in its words, or that it took an add, which nothing else on screen
// shows. Each is said once, by the view on screen when it comes; of several at once, the failure
// before the rest.
class PressNotice {
    // The model's counts that the notice has said.
    uint32_t _not_taken{};
    uint32_t _failed{};
    uint32_t _added{};
    uint32_t _since{};
    std::string _text;

public:
    // Says nothing of what the model holds now: only what comes later. Clears what is said.
    void skip(const Model& model);

    // Says what came since the model was last read, from now. Returns whether what is said
    // changed.
    bool apply(const Model& model);

    // Clears what is said once it has been said for NOTICE_MS. Returns whether it was cleared.
    bool expire();

    // What is said; empty while nothing is.
    const std::string& get_text() const { return _text; }
};

class TransportControls {
    UiHost* _host;
    // Whether the middle control is pause; it is play otherwise.
    bool _pause_shown{};
    lv_obj_t* _previous{};
    lv_obj_t* _play_pause{};
    lv_obj_t* _next{};

public:
    explicit TransportControls(UiHost* host) : _host(host) {}
    TransportControls(const TransportControls&) = delete;
    TransportControls& operator=(const TransportControls&) = delete;

    // Builds a row `width` wide in `parent` and returns it: play or pause and next, each `size`
    // across, with previous before them when asked for.
    lv_obj_t* create(lv_obj_t* parent, int32_t width, int32_t size, const lv_font_t* font, bool with_previous);

    void apply(const Playback& playback);

private:
    static void clicked(lv_event_t* event);
};

class VolumeControl {
    UiHost* _host;
    lv_obj_t* _down{};
    lv_obj_t* _up{};
    lv_obj_t* _icon{};
    lv_obj_t* _level{};

public:
    explicit VolumeControl(UiHost* host) : _host(host) {}
    VolumeControl(const VolumeControl&) = delete;
    VolumeControl& operator=(const VolumeControl&) = delete;

    // Builds a row `width` wide in `parent` and returns it: down and up, each `size` across, and
    // the level between them.
    lv_obj_t* create(lv_obj_t* parent, int32_t width, int32_t size, const lv_font_t* level_font);

    void apply(const Volume& volume);

private:
    static void clicked(lv_event_t* event);
};
