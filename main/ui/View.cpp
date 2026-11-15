#include "View.h"

#include "lv_support.h"

View::View(UiHost* host) : _scope(std::make_shared<int>(0)), _host(host) {}

View::~View() {
    if (_root) {
        lv_obj_delete(_root);
    }
}

void View::render(lv_obj_t* parent) {
    if (_root) {
        lv_obj_delete(_root);
    }

    _root = lv_container_create(parent);
    lv_obj_set_size(_root, LV_PCT(100), LV_PCT(100));

    do_render(_root);
}

void View::update() { do_update(); }

void View::set_visible(bool visible) {
    if (visible) {
        lv_obj_remove_flag(_root, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
    }
}
