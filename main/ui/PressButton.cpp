#include "PressButton.h"

#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t SPINNER_ARC_WIDTH = 6;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

lv_obj_t* PressButton::create(lv_obj_t* parent, const char* icon, lv_event_cb_t clicked, void* user_data) {
    auto button = lv_container_create(parent);
    lv_obj_set_size(button, SIZE, SIZE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);

    auto icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);
    lv_obj_center(icon_label);

    return button;
}

void PressButton::set_icon(lv_obj_t* button, const char* icon) { lv_label_set_text(lv_obj_get_child(button, 0), icon); }

// The icon is the button's first child, and the spinner, while it shows, its second. A press that
// sends nothing does not look pressed either.
void PressButton::show_on_its_way(lv_obj_t* button, bool on_its_way) {
    if (on_its_way == is_on_its_way(button)) {
        return;
    }

    lv_obj_update_flag(lv_obj_get_child(button, 0), LV_OBJ_FLAG_HIDDEN, on_its_way);
    lv_obj_set_style_bg_color(button, Theme::color(on_its_way ? Theme::BACKGROUND : Theme::SURFACE_PRESSED), PRESSED);

    if (!on_its_way) {
        lv_obj_delete(lv_obj_get_child(button, 1));
        return;
    }

    auto spinner = lv_spinner_create(button);
    lv_obj_set_size(spinner, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::SURFACE_PRESSED), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);
}

bool PressButton::is_on_its_way(const lv_obj_t* button) { return lv_obj_get_child_count(button) > 1; }
