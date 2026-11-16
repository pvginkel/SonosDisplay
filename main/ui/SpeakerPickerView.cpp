#include "SpeakerPickerView.h"

#include <algorithm>

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t HEADER_TOP = 24;
static constexpr int32_t ROW_HEIGHT = 88;
static constexpr int32_t ROW_GAP = 12;
static constexpr int32_t ROW_PADDING = 24;
static constexpr int32_t ROW_BORDER = 3;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);
static constexpr int32_t NOTICE_WIDTH = 640;
static constexpr int32_t NOTICE_DETAIL_LINES = 2;
static constexpr int32_t BUTTON_HEIGHT = 72;
static constexpr int32_t BUTTON_PADDING = 40;
static constexpr int32_t SPINNER_SIZE = 56;
static constexpr int32_t SPINNER_ARC_WIDTH = 6;
// A row's group control, and the chosen row's tick, which sits where the controls do. A press as
// far above or below the control as the row reaches is the control's.
static constexpr int32_t GROUP_CONTROL_SIZE = 64;
static constexpr int32_t GROUP_CONTROL_TOUCH_MARGIN = (ROW_HEIGHT - GROUP_CONTROL_SIZE) / 2;
static constexpr int32_t GROUP_CONTROL_BORDER = 4;

static std::vector<std::string> get_pending_speakers(const Grouping& grouping) {
    std::vector<std::string> speakers;
    for (const auto& change : grouping.pending) {
        speakers.push_back(change.speaker_id);
    }

    return speakers;
}

void SpeakerPickerView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(column, Theme::PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_top(column, HEADER_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    auto title = lv_label_create(column);
    lv_label_set_text(title, MSG_SPEAKERS);
    lv_obj_set_style_text_font(title, Theme::FONT_HEADLINE, LV_PART_MAIN);

    _hint = lv_label_create(column);
    lv_obj_set_style_text_font(_hint, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(_hint, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    _body = lv_container_create(column);
    lv_obj_set_width(_body, LV_PCT(100));
    lv_obj_set_flex_grow(_body, 1);
    // Padding, not a margin: LVGL's flex gives a growing item the room its margin takes as well,
    // which puts the body's end under the view's.
    lv_obj_set_style_pad_top(_body, Theme::GAP, LV_PART_MAIN);

    build();
}

void SpeakerPickerView::do_update() {
    const auto& model = _host->get_model();
    if (model.revision == _revision) {
        return;
    }

    _revision = model.revision;

    // Of the pending changes a row shows only whose they are. The backend's taking one changes
    // the model and builds nothing, so the row's spinner keeps turning and does not start over.
    if (model.speakers != _shown || model.speaker_id != _shown_choice ||
        get_pending_speakers(model.grouping) != _shown_pending) {
        build();
    }
}

void SpeakerPickerView::build() {
    const auto& model = _host->get_model();
    _revision = model.revision;
    _shown = model.speakers;
    _shown_choice = model.speaker_id;
    _shown_pending = get_pending_speakers(model.grouping);

    // A list built again stays where it was scrolled to: a press on a row's group control builds
    // it again under the finger.
    const auto scrolled = _list ? lv_obj_get_scroll_y(_list) : 0;

    lv_obj_clean(_body);
    _list = nullptr;

    // The group controls are on screen exactly while the list has the chosen speaker.
    const auto groups = _shown.state == LoadState::Loaded && _shown.find(_shown_choice);
    lv_label_set_text(_hint, groups ? MSG_SPEAKERS_PICK_OR_GROUP : MSG_SPEAKERS_PICK);

    switch (_shown.state) {
        case LoadState::Loading:
            build_loading();
            break;
        case LoadState::Loaded:
            if (_shown.speakers.empty()) {
                build_notice(ICON_SPEAKER, MSG_SPEAKERS_EMPTY, "", false);
            } else {
                build_list();
                lv_obj_scroll_to_y(_list, scrolled, LV_ANIM_OFF);
            }
            break;
        case LoadState::Unreachable:
            build_notice(ICON_CLOUD_OFF, MSG_BACKEND_UNREACHABLE, "", true);
            break;
        case LoadState::Failed:
            build_notice(ICON_ALERT, MSG_BACKEND_FAILED, _shown.error, true);
            break;
    }
}

static lv_obj_t* create_centered_column(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_width(column, LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_center(column);

    return column;
}

void SpeakerPickerView::build_loading() {
    auto column = create_centered_column(_body);

    auto spinner = lv_spinner_create(column);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);

    auto label = lv_label_create(column);
    lv_label_set_text(label, MSG_SPEAKERS_LOADING);
    lv_obj_set_style_text_font(label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
}

void SpeakerPickerView::build_notice(const char* icon, const char* message, const std::string& detail, bool retry) {
    auto column = create_centered_column(_body);

    auto icon_label = lv_label_create(column);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto message_label = lv_label_create(column);
    lv_label_set_text(message_label, message);
    lv_obj_set_width(message_label, NOTICE_WIDTH);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(message_label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);

    if (!detail.empty()) {
        // The backend's own words, which may be any length: they get two lines.
        auto detail_label = lv_label_create(column);
        lv_obj_set_width(detail_label, NOTICE_WIDTH);
        lv_obj_set_style_max_height(detail_label, NOTICE_DETAIL_LINES * lv_font_get_line_height(Theme::FONT_BODY),
                                    LV_PART_MAIN);
        lv_label_set_long_mode(detail_label, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(detail_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_set_style_text_font(detail_label, Theme::FONT_BODY, LV_PART_MAIN);
        lv_obj_set_style_text_color(detail_label, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
        lv_label_set_text(detail_label, detail.c_str());
    }

    if (retry) {
        auto button = lv_button_create(column);
        lv_obj_set_height(button, BUTTON_HEIGHT);
        lv_obj_set_style_pad_hor(button, BUTTON_PADDING, LV_PART_MAIN);
        lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
        lv_obj_add_event_cb(button, retry_clicked, LV_EVENT_CLICKED, this);

        auto button_label = lv_label_create(button);
        lv_label_set_text(button_label, MSG_TRY_AGAIN);
        lv_obj_set_style_text_font(button_label, Theme::FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_text_color(button_label, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
        lv_obj_center(button_label);
    }
}

void SpeakerPickerView::build_list() {
    _list = lv_container_create(_body);
    lv_obj_set_size(_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_list, ROW_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_list, Theme::GAP, LV_PART_MAIN);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);

    // The chosen speaker as the list has it: the others play in its group, or do not.
    const auto chosen_speaker = _shown.find(_shown_choice);

    // One row per speaker, in the list's order: row_clicked and group_clicked find the speaker by
    // the row's index.
    for (const auto& speaker : _shown.speakers) {
        const auto chosen = speaker.id == _shown_choice;

        auto row = lv_container_create(_list);
        lv_obj_set_size(row, LV_PCT(100), ROW_HEIGHT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, Theme::RADIUS, LV_PART_MAIN);
        // Every row has the border, so the chosen one's content sits where the others' does.
        lv_obj_set_style_border_width(row, ROW_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, Theme::color(chosen ? Theme::ACCENT : Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_border_color(row, Theme::color(chosen ? Theme::ACCENT : Theme::SURFACE_PRESSED), PRESSED);
        lv_obj_set_style_pad_hor(row, ROW_PADDING, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, Theme::GAP, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, this);

        auto icon = lv_label_create(row);
        lv_label_set_text(icon, ICON_SPEAKER);
        lv_obj_set_style_text_font(icon, Theme::FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(icon, Theme::color(chosen ? Theme::ACCENT : Theme::TEXT_SECONDARY), LV_PART_MAIN);

        auto name = lv_label_create(row);
        lv_obj_set_flex_grow(name, 1);
        lv_obj_set_height(name, lv_font_get_line_height(Theme::FONT_BODY_LARGE));
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_font(name, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
        lv_label_set_text(name, speaker.name.c_str());

        if (chosen) {
            auto check = lv_label_create(row);
            lv_label_set_text(check, ICON_CHECK);
            lv_obj_set_width(check, GROUP_CONTROL_SIZE);
            lv_obj_set_style_text_align(check, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
            lv_obj_set_style_text_font(check, Theme::FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, Theme::color(Theme::ACCENT), LV_PART_MAIN);
        } else if (chosen_speaker) {
            build_group_control(row, speaker, chosen_speaker->plays_with(speaker.id));
        }
    }
}

void SpeakerPickerView::build_group_control(lv_obj_t* row, const Speaker& speaker, bool member) {
    // The control takes the presses on it whatever it shows, so none of them picks the speaker:
    // LVGL sends a press to the object it is on, and not to the row under it.
    auto control = lv_container_create(row);
    lv_obj_set_size(control, GROUP_CONTROL_SIZE, GROUP_CONTROL_SIZE);
    lv_obj_add_flag(control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(control, GROUP_CONTROL_TOUCH_MARGIN);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_MAIN);

    // A pending change shows on its row, whichever group it was sent for, and its control sends
    // nothing.
    if (std::find(_shown_pending.begin(), _shown_pending.end(), speaker.id) != _shown_pending.end()) {
        auto spinner = lv_spinner_create(control);
        lv_obj_set_size(spinner, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_MAIN);
        lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_INDICATOR);
        lv_obj_set_style_arc_color(spinner, Theme::color(Theme::SURFACE_PRESSED), LV_PART_MAIN);
        lv_obj_set_style_arc_color(spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);

        return;
    }

    lv_obj_add_event_cb(control, group_clicked, LV_EVENT_CLICKED, this);
    lv_obj_set_style_border_width(control, GROUP_CONTROL_BORDER, LV_PART_MAIN);

    if (member) {
        lv_obj_set_style_bg_color(control, Theme::color(Theme::ACCENT), LV_PART_MAIN);
        lv_obj_set_style_bg_color(control, Theme::color(Theme::ACCENT_PRESSED), PRESSED);
        lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(control, Theme::color(Theme::ACCENT), LV_PART_MAIN);
        lv_obj_set_style_border_color(control, Theme::color(Theme::ACCENT_PRESSED), PRESSED);

        auto check = lv_label_create(control);
        lv_label_set_text(check, ICON_CHECK);
        lv_obj_set_style_text_font(check, Theme::FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(check, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
        lv_obj_center(check);
    } else {
        lv_obj_set_style_bg_color(control, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
        lv_obj_set_style_bg_opa(control, LV_OPA_COVER, PRESSED);
        lv_obj_set_style_border_color(control, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
    }
}

void SpeakerPickerView::row_clicked(lv_event_t* event) {
    auto self = (SpeakerPickerView*)lv_event_get_user_data(event);
    const auto index = lv_obj_get_index(lv_event_get_current_target_obj(event));

    // The host changes the model, and the next do_update() builds the list again: not here, inside
    // the row's own event. A pick of the chosen speaker changes nothing, and goes back as well.
    self->_host->choose_speaker(self->_shown.speakers[size_t(index)].id);
    self->_shell->back();
}

void SpeakerPickerView::group_clicked(lv_event_t* event) {
    auto self = (SpeakerPickerView*)lv_event_get_user_data(event);
    const auto row = lv_obj_get_parent(lv_event_get_current_target_obj(event));
    const auto& speaker_id = self->_shown.speakers[size_t(lv_obj_get_index(row))].id;

    // The speaker leaves the group when the list has it there, and joins it otherwise. The host
    // makes the change pending, and the next do_update() builds the list again.
    if (self->_shown.find(self->_shown_choice)->plays_with(speaker_id)) {
        self->_host->remove_from_group(speaker_id);
    } else {
        self->_host->add_to_group(speaker_id);
    }
}

void SpeakerPickerView::retry_clicked(lv_event_t* event) {
    auto self = (SpeakerPickerView*)lv_event_get_user_data(event);

    self->_host->refresh_speakers();
}
