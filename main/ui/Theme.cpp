#include "Theme.h"

void Theme::begin(lv_display_t* display) {
    auto theme = lv_theme_default_init(display, color(ACCENT), color(SURFACE), true, FONT_BODY);
    lv_display_set_theme(display, theme);

    auto screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, color(BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, color(TEXT), LV_PART_MAIN);
}
