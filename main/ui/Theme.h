#pragma once

// The dark theme: LVGL's default theme in dark mode, and the palette and type scale as named
// constants. The milestones' slices own the look and change these freely.

#include <cstdint>

#include "fonts/fonts.h"
#include "lvgl.h"

class Theme {
public:
    static constexpr uint32_t BACKGROUND = 0x101214;
    static constexpr uint32_t SURFACE = 0x1C1F24;
    static constexpr uint32_t SURFACE_PRESSED = 0x2B3038;
    static constexpr uint32_t TEXT = 0xF2F2F2;
    static constexpr uint32_t TEXT_SECONDARY = 0xA0A6AD;
    static constexpr uint32_t ACCENT = 0xE8A33D;

    static constexpr int32_t PADDING = 32;
    static constexpr int32_t GAP = 16;
    static constexpr int32_t RADIUS = 16;

    static inline const lv_font_t* const FONT_BODY = &font_regular_24;
    static inline const lv_font_t* const FONT_BODY_LARGE = &font_regular_32;
    static inline const lv_font_t* const FONT_TITLE = &font_medium_32;
    static inline const lv_font_t* const FONT_HEADLINE = &font_medium_40;
    static inline const lv_font_t* const FONT_ICON = &icons_40;
    static inline const lv_font_t* const FONT_ICON_LARGE = &icons_64;

    // Sets up the theme on the display, once, before anything is drawn.
    static void begin(lv_display_t* display);

    static lv_color_t color(uint32_t hex) { return lv_color_hex(hex); }
};
