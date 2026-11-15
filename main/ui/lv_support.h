#pragma once

// Small additions to LVGL's API.

#include "lvgl.h"

void lv_obj_set_grid_cell(lv_obj_t* obj, lv_grid_align_t x_align, uint8_t col_pos, lv_grid_align_t y_align,
                          uint8_t row_pos);
void lv_obj_set_bounds(lv_obj_t* obj, int32_t x, int32_t y, int32_t width, int32_t height, lv_text_align_t align);

// A transparent object with no padding, border or scrolling that takes no presses, sized to its
// content: the building block of layouts.
lv_obj_t* lv_container_create(lv_obj_t* parent);

// Shows a text on at most `lines` lines of `width`, ending in dots when it is longer, and hides
// the label of an empty one. LVGL cuts a text to the height its label has, and a label sized by
// its content keeps the height of the text it held before: so the height is set here, from the
// text.
void lv_label_set_text_lines(lv_obj_t* label, const char* text, int32_t width, int32_t lines);
