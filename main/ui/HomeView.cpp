#include "HomeView.h"

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

// The heading's place and height are the library screens'.
static constexpr int32_t HEADER_TOP = 16;
static constexpr int32_t HEADER_HEIGHT = 64;
static constexpr int32_t ENTRY_PADDING = 24;
static constexpr int32_t ENTRY_GAP = 12;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

void HomeView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(column, Theme::PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_top(column, HEADER_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    // Its buttons at the right, where the Shell's "Reconnecting" marker shows.
    _heading = lv_container_create(column);
    lv_obj_set_size(_heading, LV_PCT(100), HEADER_HEIGHT);
    lv_obj_set_style_pad_column(_heading, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(_heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_heading, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    fit_heading();

    auto library = lv_container_create(_heading);
    lv_obj_set_height(library, HEADER_HEIGHT);
    lv_obj_set_style_pad_hor(library, ENTRY_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_column(library, ENTRY_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(library, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(library, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(library, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(library, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(library, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(library, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(library, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(library, library_clicked, LV_EVENT_CLICKED, this);

    auto library_icon = lv_label_create(library);
    lv_label_set_text(library_icon, ICON_MUSIC_BOX_MULTIPLE);
    lv_obj_set_style_text_font(library_icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(library_icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto library_label = lv_label_create(library);
    lv_label_set_text(library_label, MSG_LIBRARY);
    lv_obj_set_style_text_font(library_label, Theme::FONT_TITLE, LV_PART_MAIN);
}

void HomeView::do_update() { fit_heading(); }

// The heading ends a gap before the marker, whose reach is from the screen's edge, where the
// heading's is from the view's padding.
void HomeView::fit_heading() {
    const auto reach = _shell->get_marker_reach();
    const auto room = reach ? reach + Theme::GAP - Theme::PADDING : 0;

    if (room != lv_obj_get_style_pad_right(_heading, LV_PART_MAIN)) {
        lv_obj_set_style_pad_right(_heading, room, LV_PART_MAIN);
    }
}

void HomeView::library_clicked(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->_shell->open_library();
}
