#pragma once

// An album's tracks, in disc and track order. A press on a track's line plays the album from it,
// and its button adds it to the queue: a track is never shuffled. The heading, the album's title,
// ends in the album's presses (ItemPresses): Play, which plays it from its first track, Shuffle, Add
// to queue, and the favourite toggle, which shows the favourite that marks the album as the screen
// was opened with it, then as the backend takes a mark or an unmark, and as the backend holds it,
// asked for again whenever the favourites may have changed unseen.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ListView.h"

class TracksView : public ListView {
    Album _album;
    PagedList<AlbumTrack> _tracks;

public:
    // The tracks of `album`, whose favourite_id is the favourite that marks it now.
    TracksView(UiHost* host, Shell* shell, const Album& album);

protected:
    PageWindow& get_pages() override { return _tracks; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
    void do_render_heading(lv_obj_t* heading) override;
    void request_heading_item() override;
};
