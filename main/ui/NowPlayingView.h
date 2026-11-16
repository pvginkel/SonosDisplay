#pragma once

// What the chosen speaker plays: the track's cover, or a drawn placeholder while there is none to
// show, the title, artist and album, and previous, play or pause, and next, each live only while
// the speaker allows its command. Under them the
// volume of the speaker's group: down, the level, and up. The speaker button at the top opens the
// speaker picker, and the library button beside it the library. While the playback is on its way,
// nothing is loaded or the backend gave none, it says so instead. What came of a press
// (PressNotice) is said over the cover's foot.

#include "Controls.h"
#include "Cover.h"
#include "View.h"

class Shell;

class NowPlayingView : public View {
    Shell* _shell;
    // What is on screen, so it is drawn again only when the model changed.
    uint32_t _revision{};
    PressNotice _press_notice;
    // Destroyed before View deletes the tile it is in.
    Cover _cover;
    TransportControls _transport;
    VolumeControl _volume;
    lv_obj_t* _title{};
    lv_obj_t* _artist{};
    lv_obj_t* _detail{};
    lv_obj_t* _controls{};
    lv_obj_t* _retry{};
    lv_obj_t* _notice{};
    lv_obj_t* _notice_label{};

public:
    NowPlayingView(UiHost* host, Shell* shell);

    // It carries the bar's controls in full.
    bool has_bar() const override { return false; }

    bool is_now_playing() const override { return true; }

    // Over the cover's corner: the top right holds the buttons.
    lv_align_t get_marker_align() const override { return LV_ALIGN_TOP_LEFT; }

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;
    void do_show() override;

private:
    void create_cover(lv_obj_t* parent);
    void create_buttons(lv_obj_t* parent);
    void create_texts(lv_obj_t* parent);
    void create_retry(lv_obj_t* parent);
    void apply();
    void apply_texts(const Playback& playback);
    void apply_notice();
    static void speaker_clicked(lv_event_t* event);
    static void library_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
};
