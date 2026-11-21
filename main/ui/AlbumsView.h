#pragma once

// An artist's albums, newest first, as playing the artist queues them, each with the year it was
// released where it has one. An album's row is its title and its year, and a press on it opens its
// tracks, whose heading ends in the album's presses (TracksView). The heading, the artist's
// name, ends in the artist's presses (ItemPresses): Play and Add to queue, which queue its albums
// newest first, Shuffle, and the favourite toggle, which shows the favourite that marks the artist
// as the screen was opened with it, then as the backend takes a mark or an unmark, and as the
// backend holds it, asked for as the screen opens where the favourites may have changed unseen
// since its opener read the mark, and again whenever they may have changed unseen.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ListView.h"

class AlbumsView : public ListView {
    Artist _artist;
    uint32_t _favourites_read;
    PagedList<Album> _albums;

public:
    // The albums of `artist`, whose favourite_id is the favourite that marked it while the model's
    // favourites_changes was `favourites_read`.
    AlbumsView(UiHost* host, Shell* shell, const Artist& artist, uint32_t favourites_read);

protected:
    PageWindow& get_pages() override { return _albums; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    void open_row(size_t index) override;
    bool has_favourites() const override { return true; }
    void apply_favourite(const FavouriteChange& change) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
    void do_render_heading(lv_obj_t* heading) override;
    void request_heading_item() override;
};
