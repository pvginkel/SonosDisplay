#pragma once

// The app's first view until milestone 1 replaces it: says the display is up, shows what it is
// connected to, and marks where each touch lands, so touch can be checked against the panel corner
// to corner.

#include "View.h"

class PlaceholderView : public View {
    uint32_t _revision{};
    lv_obj_t* _backend{};
    lv_obj_t* _connection{};
    lv_obj_t* _firmware{};
    lv_obj_t* _display{};
    lv_obj_t* _touch_marker{};

public:
    explicit PlaceholderView(UiHost* host) : View(host) {}

    // Draws the marker centred on a point of the screen.
    void show_touch(lv_point_t point);

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    lv_obj_t* create_row(lv_obj_t* parent, const char* label);
    static void touched(lv_event_t* event);
    void apply();
};
