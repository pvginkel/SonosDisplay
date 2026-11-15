#pragma once

// What the chosen speaker plays: the cover's place, the title, artist and album, and previous,
// play or pause, and next, each live only while the speaker allows its command. Under them the
// volume of the speaker's group: down, the level, and up. The speaker button at the top opens the
// speaker picker. While the playback is on its way, nothing is loaded or the backend gave none,
// it says so instead.

#include "View.h"

class Shell;

class NowPlayingView : public View {
    Shell* _shell;
    // What is on screen, so it is drawn again only when the model changed.
    uint32_t _revision{};
    // The model's count of commands and volume changes not taken that this view has said.
    uint32_t _not_taken_said{};
    uint32_t _notice_since{};
    // Whether the middle control is pause; it is play otherwise.
    bool _pause_shown{};
    lv_obj_t* _title{};
    lv_obj_t* _artist{};
    lv_obj_t* _detail{};
    lv_obj_t* _controls{};
    lv_obj_t* _previous{};
    lv_obj_t* _play_pause{};
    lv_obj_t* _next{};
    lv_obj_t* _retry{};
    lv_obj_t* _volume_down{};
    lv_obj_t* _volume_up{};
    lv_obj_t* _volume_icon{};
    lv_obj_t* _volume_level{};
    lv_obj_t* _notice{};

public:
    NowPlayingView(UiHost* host, Shell* shell) : View(host), _shell(shell) {}

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    void create_cover(lv_obj_t* parent);
    void create_speaker_button(lv_obj_t* parent);
    void create_texts(lv_obj_t* parent);
    void create_controls(lv_obj_t* parent);
    lv_obj_t* create_control(lv_obj_t* parent, int32_t size, const lv_font_t* font, lv_event_cb_t clicked);
    void create_retry(lv_obj_t* parent);
    void create_volume(lv_obj_t* parent);
    void apply();
    void apply_texts(const Playback& playback);
    void apply_volume(const Volume& volume);
    void apply_control(lv_obj_t* control, const char* icon, bool live, bool primary, bool sending);
    static void speaker_clicked(lv_event_t* event);
    static void control_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
    static void volume_clicked(lv_event_t* event);
};
