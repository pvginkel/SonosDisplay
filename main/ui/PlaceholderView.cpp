#include "PlaceholderView.h"

#include <string>

#include "Messages.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t CARD_WIDTH = 640;
static constexpr int32_t LABEL_WIDTH = 160;

void PlaceholderView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_width(column, LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_center(column);

    auto icon = lv_label_create(column);
    lv_label_set_text(icon, ICON_MUSIC);
    lv_obj_set_style_text_font(icon, Theme::FONT_ICON_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto title = lv_label_create(column);
    lv_label_set_text(title, MSG_APP_NAME);
    lv_obj_set_style_text_font(title, Theme::FONT_HEADLINE, LV_PART_MAIN);

    auto text = lv_label_create(column);
    lv_label_set_text(text, MSG_PLACEHOLDER);
    lv_obj_set_style_text_font(text, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(text, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    auto card = lv_container_create(column);
    lv_obj_set_width(card, CARD_WIDTH);
    lv_obj_set_style_bg_color(card, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(card, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, Theme::GAP + 8, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 12, LV_PART_MAIN);
    lv_obj_set_style_margin_top(card, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);

    _backend = create_row(card, MSG_BACKEND);
    _connection = create_row(card, MSG_CONNECTION);
    _firmware = create_row(card, MSG_FIRMWARE);
    _display = create_row(card, MSG_DISPLAY);

    apply();
}

lv_obj_t* PlaceholderView::create_row(lv_obj_t* parent, const char* label) {
    auto row = lv_container_create(parent);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    auto name = lv_label_create(row);
    lv_label_set_text(name, label);
    lv_obj_set_width(name, LABEL_WIDTH);
    lv_obj_set_style_text_font(name, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    auto value = lv_label_create(row);
    lv_obj_set_flex_grow(value, 1);
    lv_label_set_long_mode(value, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(value, Theme::FONT_BODY, LV_PART_MAIN);

    return value;
}

void PlaceholderView::do_update() {
    if (_host->get_model().revision != _revision) {
        apply();
    }
}

void PlaceholderView::apply() {
    const auto& model = _host->get_model();
    _revision = model.revision;

    const char* state = MSG_STREAM_CONNECTING;
    switch (model.stream_state) {
        case StreamState::Connecting:
            state = MSG_STREAM_CONNECTING;
            break;
        case StreamState::Connected:
            state = MSG_STREAM_CONNECTED;
            break;
        case StreamState::Disconnected:
            state = MSG_STREAM_DISCONNECTED;
            break;
    }

    lv_label_set_text(_backend, model.backend_url.c_str());
    lv_label_set_text(_connection, state);
    lv_label_set_text(_firmware, model.firmware_version.c_str());
    lv_label_set_text(_display, model.device_name.c_str());
}
