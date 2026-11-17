#pragma once

// The presses of an item of the library. An artist's, an album's or a playlist's are PressButtons in
// a row of their own: Play, Shuffle, Add to queue and the favourite toggle, which shows whether a
// favourite marks the item. A track's are its line, a press on which plays its album from it
// (QueueRequest::start_track_id), and Add to queue, a PressButton at the line's end. A press on one
// asks the host for its request, unless that request is on its way; a press on the toggle marks
// the item, or unmarks the favourite that marks it, unless a change of the item is on its way. A
// drag that starts on one still scrolls what they are in.

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
    // A track's line, the album a press on it plays, and the spinner it shows while that press is on
    // its way.
    lv_obj_t* _line{};
    std::string _album_id;
    lv_obj_t* _line_spinner{};

public:
    // Builds the presses at rest of an artist, an album or a playlist at the end of `parent`, a flex
    // row, the toggle showing the favourite with `favourite_id` or none. They call on this while they
    // live: it is destroyed after them.
    ItemPresses(UiHost* host, lv_obj_t* parent, ItemKind kind, std::string item_id,
                std::optional<int64_t> favourite_id);

    // Builds the presses at rest of the track with `track_id` of the album with `album_id`: its line,
    // `line`, a flex row, and Add to queue at its end. They call on this while they live: it is
    // destroyed after them.
    ItemPresses(UiHost* host, lv_obj_t* line, std::string album_id, std::string track_id);
    ItemPresses(const ItemPresses&) = delete;
    ItemPresses& operator=(const ItemPresses&) = delete;

    // Applies a change of a favourite the backend took (FavouriteChange::apply()): the toggle shows
    // the favourite it leaves marking the item, which a press on the toggle unmarks.
    void apply(const FavouriteChange& change);

    // The toggle shows the favourite with this id, or none, as the one that marks the item: what the
    // backend holds, asked for again.
    void show_favourite(std::optional<int64_t> favourite_id);

    // The favourite the toggle shows marking the item, or none.
    std::optional<int64_t> get_favourite_id() const { return _favourite_id; }

    // Each press shows whether it is on its way: a request for the item and the button's action, a
    // track's line its album's Play from it, or a change of the item. Wherever the item's presses are
    // built, before the press or after it, they show the same.
    void show_on_its_way();

private:
    QueueRequest get_line_request() const;
    void show_line_on_its_way(bool on_its_way);
    static void queue_clicked(lv_event_t* event);
    static void line_clicked(lv_event_t* event);
    static void favourite_clicked(lv_event_t* event);
};
