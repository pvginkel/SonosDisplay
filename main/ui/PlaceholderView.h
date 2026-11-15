#pragma once

// The app's first view until milestone 1 replaces it: says the display is up, and shows what it
// is connected to.

#include "View.h"

class PlaceholderView : public View {
    uint32_t _revision{};
    lv_obj_t* _backend{};
    lv_obj_t* _connection{};
    lv_obj_t* _firmware{};
    lv_obj_t* _display{};

public:
    explicit PlaceholderView(UiHost* host) : View(host) {}

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

private:
    lv_obj_t* create_row(lv_obj_t* parent, const char* label);
    void apply();
};
