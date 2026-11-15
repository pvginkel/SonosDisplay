#include "NowPlayingView.h"

#include <algorithm>

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

// The cover is 400 px of the panel's 480, centred in its height.
static constexpr int32_t COVER_SIZE = 400;
static constexpr int32_t COVER_TOP = 40;
static constexpr int32_t COLUMN_WIDTH = 312;
static constexpr int32_t COLUMN_PADDING = 24;
static constexpr int32_t SPEAKER_SIZE = 64;
static constexpr int32_t TEXT_GAP = 4;
static constexpr int32_t TITLE_LINES = 2;
static constexpr int32_t ERROR_LINES = 2;
static constexpr int32_t CONTROL_SIZE = 88;
static constexpr int32_t BUTTON_HEIGHT = 72;
static constexpr int32_t VOLUME_SIZE = 72;
static constexpr int32_t VOLUME_LEVEL_GAP = 8;
static constexpr int32_t NOTICE_PADDING_HOR = 20;
static constexpr int32_t NOTICE_PADDING_VER = 12;
static constexpr uint32_t NOTICE_MS = 5000;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

void NowPlayingView::do_render(lv_obj_t* parent) {
    create_cover(parent);

    auto column = lv_container_create(parent);
    lv_obj_set_size(column, COLUMN_WIDTH, LV_PCT(100));
    lv_obj_align(column, LV_ALIGN_TOP_RIGHT, -Theme::PADDING, 0);
    lv_obj_set_style_pad_ver(column, COLUMN_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    create_speaker_button(column);
    create_texts(column);
    create_controls(column);
    create_retry(column);
    create_volume(column);

    // A command that was not taken before this view opened is not said by it.
    _not_taken_said = _host->get_model().not_taken();

    apply();
}

void NowPlayingView::create_cover(lv_obj_t* parent) {
    auto cover = lv_container_create(parent);
    lv_obj_set_size(cover, COVER_SIZE, COVER_SIZE);
    lv_obj_set_pos(cover, Theme::PADDING, COVER_TOP);
    lv_obj_set_style_bg_color(cover, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(cover, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(cover, Theme::RADIUS, LV_PART_MAIN);

    auto placeholder = lv_label_create(cover);
    lv_label_set_text(placeholder, ICON_MUSIC);
    lv_obj_set_style_text_font(placeholder, Theme::FONT_ICON_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(placeholder, Theme::color(Theme::TEXT_UNAVAILABLE), LV_PART_MAIN);
    lv_obj_center(placeholder);

    _notice = lv_container_create(cover);
    lv_obj_set_style_bg_color(_notice, Theme::color(Theme::SURFACE_PRESSED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_notice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_notice, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_notice, NOTICE_PADDING_HOR, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_notice, NOTICE_PADDING_VER, LV_PART_MAIN);
    lv_obj_align(_notice, LV_ALIGN_BOTTOM_MID, 0, -Theme::GAP);
    lv_obj_add_flag(_notice, LV_OBJ_FLAG_HIDDEN);

    auto notice_label = lv_label_create(_notice);
    lv_label_set_text(notice_label, MSG_COMMAND_NOT_TAKEN);
    lv_obj_set_style_text_font(notice_label, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(notice_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);
}

void NowPlayingView::create_speaker_button(lv_obj_t* parent) {
    // At the column's left, and no wider than its icon: the Shell's "Reconnecting" marker shows
    // over the rest of this row.
    auto button = lv_container_create(parent);
    lv_obj_set_size(button, SPEAKER_SIZE, SPEAKER_SIZE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(button, speaker_clicked, LV_EVENT_CLICKED, this);

    auto icon = lv_label_create(button);
    lv_label_set_text(icon, ICON_SPEAKER);
    lv_obj_set_style_text_font(icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);
    lv_obj_center(icon);
}

void NowPlayingView::create_texts(lv_obj_t* parent) {
    auto texts = lv_container_create(parent);
    lv_obj_set_width(texts, LV_PCT(100));
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_style_pad_row(texts, TEXT_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // What plays is any length: the title gets two lines, the artist and the album one each.
    // set_text() gives each label its height.
    _title = lv_label_create(texts);
    lv_obj_set_width(_title, LV_PCT(100));
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_title, Theme::FONT_TITLE, LV_PART_MAIN);

    _artist = lv_label_create(texts);
    lv_obj_set_width(_artist, LV_PCT(100));
    lv_label_set_long_mode(_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_artist, Theme::FONT_BODY, LV_PART_MAIN);

    _detail = lv_label_create(texts);
    lv_obj_set_width(_detail, LV_PCT(100));
    lv_label_set_long_mode(_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_detail, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(_detail, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
}

void NowPlayingView::create_controls(lv_obj_t* parent) {
    _controls = lv_container_create(parent);
    lv_obj_set_size(_controls, LV_PCT(100), CONTROL_SIZE);
    lv_obj_set_flex_flow(_controls, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_controls, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _previous = create_control(_controls, CONTROL_SIZE, Theme::FONT_ICON_LARGE, control_clicked);
    _play_pause = create_control(_controls, CONTROL_SIZE, Theme::FONT_ICON_LARGE, control_clicked);
    _next = create_control(_controls, CONTROL_SIZE, Theme::FONT_ICON_LARGE, control_clicked);
}

lv_obj_t* NowPlayingView::create_control(lv_obj_t* parent, int32_t size, const lv_font_t* font, lv_event_cb_t clicked) {
    auto control = lv_container_create(parent);
    lv_obj_set_size(control, size, size);
    lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_event_cb(control, clicked, LV_EVENT_CLICKED, this);

    auto icon = lv_label_create(control);
    lv_obj_set_style_text_font(icon, font, LV_PART_MAIN);
    lv_obj_center(icon);

    return control;
}

void NowPlayingView::create_retry(lv_obj_t* parent) {
    _retry = lv_button_create(parent);
    lv_obj_set_size(_retry, LV_PCT(100), BUTTON_HEIGHT);
    lv_obj_set_style_radius(_retry, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_retry, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_retry, retry_clicked, LV_EVENT_CLICKED, this);

    auto label = lv_label_create(_retry);
    lv_label_set_text(label, MSG_TRY_AGAIN);
    lv_obj_set_style_text_font(label, Theme::FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_center(label);
}

void NowPlayingView::create_volume(lv_obj_t* parent) {
    auto row = lv_container_create(parent);
    lv_obj_set_size(row, LV_PCT(100), VOLUME_SIZE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _volume_down = create_control(row, VOLUME_SIZE, Theme::FONT_ICON, volume_clicked);

    auto level = lv_container_create(row);
    lv_obj_set_style_pad_column(level, VOLUME_LEVEL_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(level, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(level, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _volume_icon = lv_label_create(level);
    lv_obj_set_style_text_font(_volume_icon, Theme::FONT_ICON, LV_PART_MAIN);

    _volume_level = lv_label_create(level);
    lv_obj_set_style_text_font(_volume_level, Theme::FONT_TITLE, LV_PART_MAIN);

    _volume_up = create_control(row, VOLUME_SIZE, Theme::FONT_ICON, volume_clicked);
}

void NowPlayingView::do_update() {
    if (_host->get_model().revision != _revision) {
        apply();
    }

    if (!lv_obj_has_flag(_notice, LV_OBJ_FLAG_HIDDEN) && lv_tick_elaps(_notice_since) >= NOTICE_MS) {
        lv_obj_add_flag(_notice, LV_OBJ_FLAG_HIDDEN);
    }
}

// Shows a text on at most `lines` lines of the column's width, ending in dots when it is longer,
// and hides the label of an empty one. LVGL cuts a text to the height its label has, and a label
// sized by its content keeps the height of the text it held before: so the height is set here,
// from the text.
static void set_text(lv_obj_t* label, const char* text, const lv_font_t* font, int32_t lines) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, COLUMN_WIDTH, LV_TEXT_FLAG_NONE);

    lv_obj_set_height(label, std::min(size.y, lines * lv_font_get_line_height(font)));
    lv_label_set_text(label, text);

    if (*text) {
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void NowPlayingView::apply() {
    const auto& model = _host->get_model();
    const auto& playback = model.playback;
    _revision = model.revision;

    apply_texts(playback);
    apply_volume(model.volume);

    // Without an answer the commands the speaker allows are not known: asking again is all there
    // is to do.
    const auto failed = playback.state == LoadState::Unreachable || playback.state == LoadState::Failed;
    set_hidden(_controls, failed);
    set_hidden(_retry, !failed);

    const auto loaded = playback.state == LoadState::Loaded;
    _pause_shown = playback.is_playing();

    apply_control(_previous, ICON_SKIP_PREVIOUS, loaded && playback.can_previous, false,
                  playback.sending == Command::Previous);
    apply_control(_play_pause, _pause_shown ? ICON_PAUSE : ICON_PLAY,
                  loaded && playback.allows(_pause_shown ? Command::Pause : Command::Play), true, false);
    apply_control(_next, ICON_SKIP_NEXT, loaded && playback.can_next, false, playback.sending == Command::Next);

    if (model.not_taken() != _not_taken_said) {
        _not_taken_said = model.not_taken();
        _notice_since = lv_tick_get();
        lv_obj_remove_flag(_notice, LV_OBJ_FLAG_HIDDEN);
    }
}

void NowPlayingView::apply_texts(const Playback& playback) {
    const char* title = "";
    const char* artist = "";
    const char* detail = "";
    auto title_color = Theme::TEXT_SECONDARY;
    int32_t detail_lines = 1;

    switch (playback.state) {
        case LoadState::Loading:
            title = MSG_PLAYBACK_LOADING;
            break;
        case LoadState::Loaded:
            if (playback.track) {
                title = playback.track->title.c_str();
                artist = playback.track->artist.c_str();
                detail = playback.track->album.c_str();
                title_color = Theme::TEXT;
            } else {
                title = MSG_NOTHING_PLAYING;
            }
            break;
        case LoadState::Unreachable:
            title = MSG_BACKEND_UNREACHABLE;
            title_color = Theme::TEXT;
            break;
        case LoadState::Failed:
            // The backend's own words, which may be any length: they get two lines.
            title = MSG_BACKEND_FAILED;
            detail = playback.error.c_str();
            title_color = Theme::TEXT;
            detail_lines = ERROR_LINES;
            break;
    }

    set_text(_title, title, Theme::FONT_TITLE, TITLE_LINES);
    lv_obj_set_style_text_color(_title, Theme::color(title_color), LV_PART_MAIN);

    set_text(_artist, artist, Theme::FONT_BODY, 1);
    set_text(_detail, detail, Theme::FONT_BODY, detail_lines);
}

void NowPlayingView::apply_volume(const Volume& volume) {
    apply_control(_volume_down, ICON_MINUS, volume.allows(-Volume::STEP), false, false);
    apply_control(_volume_up, ICON_PLUS, volume.allows(Volume::STEP), false, false);

    const auto known = volume.is_known();
    const auto muted = known && volume.muted;

    // A press shows in the level at once, and in its colour until the backend has answered.
    auto icon_color = Theme::TEXT_SECONDARY;
    auto level_color = Theme::TEXT;
    if (!known) {
        icon_color = Theme::TEXT_UNAVAILABLE;
        level_color = Theme::TEXT_UNAVAILABLE;
    } else if (muted) {
        icon_color = Theme::ACCENT;
        level_color = Theme::TEXT_UNAVAILABLE;
    }
    if (volume.is_changing()) {
        level_color = Theme::ACCENT;
    }

    lv_label_set_text(_volume_icon, muted ? ICON_VOLUME_OFF : ICON_VOLUME_HIGH);
    lv_obj_set_style_text_color(_volume_icon, Theme::color(icon_color), LV_PART_MAIN);

    if (known) {
        lv_label_set_text_fmt(_volume_level, "%d", volume.shown());
    } else {
        lv_label_set_text(_volume_level, MSG_VOLUME_UNKNOWN);
    }
    lv_obj_set_style_text_color(_volume_level, Theme::color(level_color), LV_PART_MAIN);
}

void NowPlayingView::apply_control(lv_obj_t* control, const char* icon, bool live, bool primary, bool sending) {
    // A control that is not live takes no press: LVGL sends a disabled object no click.
    lv_obj_set_state(control, LV_STATE_DISABLED, !live);

    const auto filled = primary && live;
    lv_obj_set_style_bg_color(control, Theme::color(filled ? Theme::ACCENT : Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, Theme::color(filled ? Theme::ACCENT_PRESSED : Theme::SURFACE_PRESSED), PRESSED);

    auto color = Theme::TEXT;
    if (!live) {
        color = Theme::TEXT_UNAVAILABLE;
    } else if (primary) {
        color = Theme::BACKGROUND;
    } else if (sending) {
        color = Theme::ACCENT;
    }

    auto label = lv_obj_get_child(control, 0);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_color(label, Theme::color(color), LV_PART_MAIN);
}

void NowPlayingView::speaker_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);

    self->_shell->open_speaker_picker();
}

void NowPlayingView::control_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);
    const auto control = lv_event_get_current_target_obj(event);

    if (control == self->_previous) {
        self->_host->send_command(Command::Previous);
    } else if (control == self->_next) {
        self->_host->send_command(Command::Next);
    } else {
        self->_host->send_command(self->_pause_shown ? Command::Pause : Command::Play);
    }
}

void NowPlayingView::retry_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);

    self->_host->refresh_playback();
    self->_host->refresh_volume();
}

void NowPlayingView::volume_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);
    const auto control = lv_event_get_current_target_obj(event);

    self->_host->change_volume(control == self->_volume_up ? Volume::STEP : -Volume::STEP);
}
