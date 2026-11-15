#pragma once

// The controls the now-playing screen and the now-playing bar share, and the rules by which they
// are live: previous, play or pause, and next, each only while the speaker allows its command, and
// the volume of the speaker's group, down, the level, and up.

#include <cstdint>

#include "UiHost.h"
#include "lvgl.h"

// How long a view says that a command or a change of the volume was not taken.
inline constexpr uint32_t NOT_TAKEN_NOTICE_MS = 5000;

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
