#include "lv_support.h"

#include <algorithm>

void lv_obj_set_grid_cell(lv_obj_t* obj, lv_grid_align_t x_align, uint8_t col_pos, lv_grid_align_t y_align,
                          uint8_t row_pos) {
    lv_obj_set_grid_cell(obj, x_align, col_pos, 1, y_align, row_pos, 1);
}

void lv_obj_set_bounds(lv_obj_t* obj, int32_t x, int32_t y, int32_t width, int32_t height, lv_text_align_t align) {
    lv_obj_set_size(obj, width, height);

    switch (align) {
        case LV_TEXT_ALIGN_AUTO:
        case LV_TEXT_ALIGN_LEFT:
            lv_obj_set_x(obj, x);
            break;

        case LV_TEXT_ALIGN_CENTER:
            lv_obj_set_x(obj, x - width / 2);
            break;

        case LV_TEXT_ALIGN_RIGHT:
            lv_obj_set_x(obj, x - width);
            break;
    }

    lv_obj_set_y(obj, y - height / 2);
}

lv_obj_t* lv_container_create(lv_obj_t* parent) {
    auto obj = lv_obj_create(parent);

    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    return obj;
}

void lv_label_set_text_lines(lv_obj_t* label, const char* text, int32_t width, int32_t lines) {
    const auto font = lv_obj_get_style_text_font(label, LV_PART_MAIN);

    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, width, LV_TEXT_FLAG_NONE);

    lv_obj_set_height(label, std::min(size.y, lines * lv_font_get_line_height(font)));
    lv_label_set_text(label, text);

    lv_obj_update_flag(label, LV_OBJ_FLAG_HIDDEN, !*text);
}
