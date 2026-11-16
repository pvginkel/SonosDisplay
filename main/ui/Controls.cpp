#include "Controls.h"

#include "Messages.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t VOLUME_LEVEL_GAP = 8;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

static lv_obj_t* create_row(lv_obj_t* parent, int32_t width, int32_t height) {
    auto row = lv_container_create(parent);
    lv_obj_set_size(row, width, height);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    return row;
}

static lv_obj_t* create_control(lv_obj_t* parent, int32_t size, const lv_font_t* font, lv_event_cb_t clicked,
                                void* user_data) {
    auto control = lv_container_create(parent);
    lv_obj_set_size(control, size, size);
    lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_event_cb(control, clicked, LV_EVENT_CLICKED, user_data);

    auto icon = lv_label_create(control);
    lv_obj_set_style_text_font(icon, font, LV_PART_MAIN);
    lv_obj_center(icon);

    return control;
}

static void apply_control(lv_obj_t* control, const char* icon, bool live, bool primary, bool sending) {
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

lv_obj_t* TransportControls::create(lv_obj_t* parent, int32_t width, int32_t size, const lv_font_t* font,
                                    bool with_previous) {
    auto row = create_row(parent, width, size);

    if (with_previous) {
        _previous = create_control(row, size, font, clicked, this);
    }
    _play_pause = create_control(row, size, font, clicked, this);
    _next = create_control(row, size, font, clicked, this);

    return row;
}

void TransportControls::apply(const Playback& playback) {
    // Without an answer the commands the speaker allows are not known.
    const auto loaded = playback.state == LoadState::Loaded;
    _pause_shown = playback.is_playing();

    if (_previous) {
        apply_control(_previous, ICON_SKIP_PREVIOUS, loaded && playback.can_previous, false,
                      playback.sending == Command::Previous);
    }
    apply_control(_play_pause, _pause_shown ? ICON_PAUSE : ICON_PLAY,
                  loaded && playback.allows(_pause_shown ? Command::Pause : Command::Play), true, false);
    apply_control(_next, ICON_SKIP_NEXT, loaded && playback.can_next, false, playback.sending == Command::Next);
}

void TransportControls::clicked(lv_event_t* event) {
    auto self = (TransportControls*)lv_event_get_user_data(event);
    const auto control = lv_event_get_current_target_obj(event);

    if (control == self->_previous) {
        self->_host->send_command(Command::Previous);
    } else if (control == self->_next) {
        self->_host->send_command(Command::Next);
    } else {
        self->_host->send_command(self->_pause_shown ? Command::Pause : Command::Play);
    }
}

lv_obj_t* VolumeControl::create(lv_obj_t* parent, int32_t width, int32_t size, const lv_font_t* level_font) {
    auto row = create_row(parent, width, size);

    _down = create_control(row, size, Theme::FONT_ICON, clicked, this);

    auto level = lv_container_create(row);
    lv_obj_set_style_pad_column(level, VOLUME_LEVEL_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(level, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(level, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    _icon = lv_label_create(level);
    lv_obj_set_style_text_font(_icon, Theme::FONT_ICON, LV_PART_MAIN);

    _level = lv_label_create(level);
    lv_obj_set_style_text_font(_level, level_font, LV_PART_MAIN);

    _up = create_control(row, size, Theme::FONT_ICON, clicked, this);

    return row;
}

void VolumeControl::apply(const Volume& volume) {
    apply_control(_down, ICON_MINUS, volume.allows(-Volume::STEP), false, false);
    apply_control(_up, ICON_PLUS, volume.allows(Volume::STEP), false, false);

    const auto known = volume.is_known();
    const auto muted = known && volume.shown_muted();

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

    lv_label_set_text(_icon, muted ? ICON_VOLUME_OFF : ICON_VOLUME_HIGH);
    lv_obj_set_style_text_color(_icon, Theme::color(icon_color), LV_PART_MAIN);

    if (known) {
        lv_label_set_text_fmt(_level, "%d", volume.shown());
    } else {
        lv_label_set_text(_level, MSG_VOLUME_UNKNOWN);
    }
    lv_obj_set_style_text_color(_level, Theme::color(level_color), LV_PART_MAIN);
}

void VolumeControl::clicked(lv_event_t* event) {
    auto self = (VolumeControl*)lv_event_get_user_data(event);
    const auto control = lv_event_get_current_target_obj(event);

    self->_host->change_volume(control == self->_up ? Volume::STEP : -Volume::STEP);
}

void PressNotice::skip(const Model& model) {
    _not_taken = model.not_taken();
    _failed = model.queueing.tasks_failed;
    _added = model.queueing.adds_taken;
    _text.clear();
}

bool PressNotice::apply(const Model& model) {
    const auto& queueing = model.queueing;
    const char* text = nullptr;

    // A failure whose words are all characters the fonts do not draw is said as one not taken.
    if (queueing.tasks_failed != _failed) {
        text = queueing.failure.empty() ? MSG_COMMAND_NOT_TAKEN : queueing.failure.c_str();
    } else if (model.not_taken() != _not_taken) {
        text = MSG_COMMAND_NOT_TAKEN;
    } else if (queueing.adds_taken != _added) {
        text = MSG_ADDED_TO_QUEUE;
    }

    _not_taken = model.not_taken();
    _failed = queueing.tasks_failed;
    _added = queueing.adds_taken;

    if (!text) {
        return false;
    }

    _text = text;
    _since = lv_tick_get();

    return true;
}

bool PressNotice::expire() {
    if (_text.empty() || lv_tick_elaps(_since) < NOTICE_MS) {
        return false;
    }

    _text.clear();

    return true;
}
