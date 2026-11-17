#pragma once

// The round button of a press that sends a request for an item of the library to the backend, a
// target of SIZE across: the queue's buttons and the favourite toggle at the end of a row. At rest
// it shows its icon in the accent. While its press is on its way, from the press until the backend answers it, does not
// take it, or ANSWER_TIME_LIMIT_MS settles it, a turning spinner shows in the icon's place, as on
// the speakers screen's group control while a change of a group is pending. It then takes a press
// and shows none, and the view that built it sends nothing for that press.

#include <cstdint>

#include "lvgl.h"

class PressButton {
public:
    static constexpr int32_t SIZE = 64;

    // Builds a button at rest in `parent`, with this icon. A press on it runs `clicked` with
    // `user_data`, also while it shows its press on its way.
    static lv_obj_t* create(lv_obj_t* parent, const char* icon, lv_event_cb_t clicked, void* user_data);

    // Shows this icon on the button in the place of the one it shows, also while its press is on
    // its way, which hides it.
    static void set_icon(lv_obj_t* button, const char* icon);

    // Shows the button with its press on its way, or at rest. Nothing changes where it shows that
    // already, so a spinner that turns turns on.
    static void show_on_its_way(lv_obj_t* button, bool on_its_way);

    // Whether the button shows its press on its way.
    static bool is_on_its_way(const lv_obj_t* button);

    // Builds in `parent` the spinner a button shows while its press is on its way, SIZE across, for
    // a press on what is no button: a track's line (ItemPresses).
    static lv_obj_t* create_spinner(lv_obj_t* parent);
};
