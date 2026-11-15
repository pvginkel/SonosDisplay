#include "SpeakerPickerView.h"

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t HEADER_TOP = 24;
static constexpr int32_t ROW_HEIGHT = 88;
static constexpr int32_t ROW_GAP = 12;
static constexpr int32_t ROW_PADDING = 24;
static constexpr int32_t ROW_BORDER = 3;
static constexpr lv_style_selector_t ROW_PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);
static constexpr int32_t NOTICE_WIDTH = 640;
static constexpr int32_t NOTICE_DETAIL_LINES = 2;
static constexpr int32_t BUTTON_HEIGHT = 72;
static constexpr int32_t BUTTON_PADDING = 40;
static constexpr int32_t SPINNER_SIZE = 56;
static constexpr int32_t SPINNER_ARC_WIDTH = 6;

void SpeakerPickerView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(column, Theme::PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_top(column, HEADER_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    auto title = lv_label_create(column);
    lv_label_set_text(title, MSG_SPEAKERS);
    lv_obj_set_style_text_font(title, Theme::FONT_HEADLINE, LV_PART_MAIN);

    auto hint = lv_label_create(column);
    lv_label_set_text(hint, MSG_SPEAKERS_PICK);
    lv_obj_set_style_text_font(hint, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(hint, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    _body = lv_container_create(column);
    lv_obj_set_width(_body, LV_PCT(100));
    lv_obj_set_flex_grow(_body, 1);
    lv_obj_set_style_margin_top(_body, Theme::GAP, LV_PART_MAIN);

    build();
}

void SpeakerPickerView::do_update() {
    const auto& model = _host->get_model();
    if (model.revision == _revision) {
        return;
    }

    _revision = model.revision;

    if (model.speakers != _shown || model.speaker_id != _shown_choice) {
        build();
    }
}

void SpeakerPickerView::build() {
    const auto& model = _host->get_model();
    _revision = model.revision;
    _shown = model.speakers;
    _shown_choice = model.speaker_id;

    lv_obj_clean(_body);

    switch (_shown.state) {
        case LoadState::Loading:
            build_loading();
            break;
        case LoadState::Loaded:
            if (_shown.speakers.empty()) {
                build_notice(ICON_SPEAKER, MSG_SPEAKERS_EMPTY, "", false);
            } else {
                build_list();
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
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON_LARGE, LV_PART_MAIN);
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
        lv_obj_set_style_margin_top(button, Theme::GAP, LV_PART_MAIN);
        lv_obj_add_event_cb(button, retry_clicked, LV_EVENT_CLICKED, this);

        auto button_label = lv_label_create(button);
        lv_label_set_text(button_label, MSG_TRY_AGAIN);
        lv_obj_set_style_text_font(button_label, Theme::FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_text_color(button_label, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
        lv_obj_center(button_label);
    }
}

void SpeakerPickerView::build_list() {
    auto list = lv_container_create(_body);
    lv_obj_set_size(list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, ROW_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(list, Theme::GAP, LV_PART_MAIN);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    // One row per speaker, in the list's order: row_clicked finds the speaker by the row's index.
    for (const auto& speaker : _shown.speakers) {
        const auto chosen = speaker.id == _shown_choice;

        auto row = lv_container_create(list);
        lv_obj_set_size(row, LV_PCT(100), ROW_HEIGHT);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE_PRESSED), ROW_PRESSED);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, Theme::RADIUS, LV_PART_MAIN);
        // Every row has the border, so the chosen one's content sits where the others' does.
        lv_obj_set_style_border_width(row, ROW_BORDER, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, Theme::color(chosen ? Theme::ACCENT : Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_border_color(row, Theme::color(chosen ? Theme::ACCENT : Theme::SURFACE_PRESSED), ROW_PRESSED);
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
            lv_obj_set_style_text_font(check, Theme::FONT_ICON, LV_PART_MAIN);
            lv_obj_set_style_text_color(check, Theme::color(Theme::ACCENT), LV_PART_MAIN);
        }
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

void SpeakerPickerView::retry_clicked(lv_event_t* event) {
    auto self = (SpeakerPickerView*)lv_event_get_user_data(event);

    self->_host->refresh_speakers();
}
