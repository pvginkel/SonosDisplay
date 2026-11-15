#pragma once

// Small additions to LVGL's API.

#include "lvgl.h"

void lv_obj_set_grid_cell(lv_obj_t* obj, lv_grid_align_t x_align, uint8_t col_pos, lv_grid_align_t y_align,
                          uint8_t row_pos);
void lv_obj_set_bounds(lv_obj_t* obj, int32_t x, int32_t y, int32_t width, int32_t height, lv_text_align_t align);

// A transparent object with no padding, border or scrolling that takes no presses, sized to its
// content: the building block of layouts.
lv_obj_t* lv_container_create(lv_obj_t* parent);
