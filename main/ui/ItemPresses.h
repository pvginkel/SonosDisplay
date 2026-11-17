#pragma once

// The presses of an item of the library, PressButtons in a row of their own: Play, Shuffle unless
// the item is a track, Add to queue, and, unless it is a track, the favourite toggle, which shows
// whether a favourite marks the item. A press on one asks the host for its request, unless that
// request is on its way; a press on the toggle marks the item, or unmarks the favourite that marks
// it, unless a change of the item is on its way. A drag that starts on one still scrolls what they
// are in.

#include <cstdint>
#include <optional>
#include <string>

#include "UiHost.h"
#include "lvgl.h"

class ItemPresses {
    UiHost* _host;
    ItemKind _kind;
    std::string _item_id;
    std::optional<int64_t> _favourite_id;
    lv_obj_t* _buttons;

public:
    // Builds the buttons at rest at the end of `parent`, a flex row, the toggle showing the favourite
    // with `favourite_id` or none. They call on this while they live: it is destroyed after them.
    ItemPresses(UiHost* host, lv_obj_t* parent, ItemKind kind, std::string item_id,
                std::optional<int64_t> favourite_id);
    ItemPresses(const ItemPresses&) = delete;
    ItemPresses& operator=(const ItemPresses&) = delete;

    // Applies a change of a favourite the backend took (FavouriteChange::apply()): the toggle shows
    // the favourite it leaves marking the item, which a press on the toggle unmarks.
    void apply(const FavouriteChange& change);

    // Each button shows whether its press is on its way: a request for the item and the button's
    // action, or a change of the item. Wherever the item's presses are built, before the press or
    // after it, they show the same.
    void show_on_its_way();

private:
    static void queue_clicked(lv_event_t* event);
    static void favourite_clicked(lv_event_t* event);
};
