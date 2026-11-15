#include "LoadingView.h"

#include "Messages.h"
#include "Theme.h"
#include "log.h"
#include "lv_support.h"

LOG_TAG(LoadingView);

static constexpr int32_t SPINNER_SIZE = 56;
static constexpr int32_t SPINNER_ARC_WIDTH = 6;
static constexpr int32_t MESSAGE_WIDTH = 640;

LoadingView::LoadingView(UiHost* host, const char* message) : View(host), _message(message) {}

void LoadingView::set_message(const char* message) {
    UI_LOGI("%s", message);

    _message = message;
    _error = false;
    apply();
}

void LoadingView::set_error(const char* message) {
    UI_LOGE("%s", message);

    _message = message;
    _error = true;
    apply();
}

void LoadingView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_width(column, LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_center(column);

    _icon = lv_label_create(column);
    lv_obj_set_style_text_font(_icon, Theme::FONT_ICON_LARGE, LV_PART_MAIN);

    auto title = lv_label_create(column);
    lv_label_set_text(title, MSG_APP_NAME);
    lv_obj_set_style_text_font(title, Theme::FONT_HEADLINE, LV_PART_MAIN);

    _label = lv_label_create(column);
    lv_obj_set_width(_label, MESSAGE_WIDTH);
    lv_label_set_long_mode(_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(_label, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    _spinner = lv_spinner_create(column);
    lv_obj_set_size(_spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_set_style_arc_width(_spinner, SPINNER_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(_spinner, SPINNER_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(_spinner, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(_spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_margin_top(_spinner, Theme::GAP, LV_PART_MAIN);

    apply();
}

void LoadingView::apply() {
    if (!_label) {
        return;
    }

    lv_label_set_text(_icon, _error ? ICON_ALERT : ICON_MUSIC);
    lv_obj_set_style_text_color(_icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);
    lv_label_set_text(_label, _message.c_str());

    if (_error) {
        lv_obj_add_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(_spinner, LV_OBJ_FLAG_HIDDEN);
    }
}
