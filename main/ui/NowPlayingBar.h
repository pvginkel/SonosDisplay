#pragma once

// The now-playing bar, which the Shell shows under its content area: the cover of the track the
// chosen speaker plays, its title and artist, play or pause, next, and the volume of the
// speaker's group. Its controls are the now-playing screen's, under the same rules. A command or
// a change of the volume that was not taken is said in the place of the title. A touch on the
// cover or the texts opens the now-playing screen.

#include <cstdint>

#include "Controls.h"
#include "Cover.h"
#include "UiHost.h"
#include "lvgl.h"

class Shell;

class NowPlayingBar {
public:
    static constexpr int32_t HEIGHT = 96;

private:
    UiHost* _host;
    Shell* _shell;
    bool _shown{};
    // What is on screen, so it is drawn again only when the model changed.
    uint32_t _revision{};
    // The model's count of commands and volume changes not taken that the bar has said.
    uint32_t _not_taken_said{};
    uint32_t _notice_since{};
    bool _notice{};
    Cover _cover;
    TransportControls _transport;
    VolumeControl _volume;
    lv_obj_t* _bar{};
    lv_obj_t* _texts{};
    lv_obj_t* _title{};
    lv_obj_t* _artist{};

public:
    NowPlayingBar(UiHost* host, Shell* shell);
    NowPlayingBar(const NowPlayingBar&) = delete;
    NowPlayingBar& operator=(const NowPlayingBar&) = delete;
    ~NowPlayingBar();

    // Builds the bar at the bottom of `parent`, hidden.
    void create(lv_obj_t* parent);

    // Shows or hides the bar. Hidden, it asks for no cover and holds none.
    void set_shown(bool shown);

    bool is_shown() const { return _shown; }

    // Called from the UI loop.
    void update();

private:
    void apply();
    void apply_texts(const Playback& playback);
    static void opened(lv_event_t* event);
};
