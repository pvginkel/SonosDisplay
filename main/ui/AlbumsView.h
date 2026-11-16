#pragma once

// An artist's albums, newest first, as playing the artist queues them, each with the year it was
// released where it has one. A press on an album opens its tracks, and its buttons play it, shuffle
// it or add it to the queue, and mark it as a favourite or unmark it.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ListView.h"

class AlbumsView : public ListView {
    std::string _artist_id;
    PagedList<Album> _albums;

public:
    AlbumsView(UiHost* host, Shell* shell, const Artist& artist);

protected:
    PageWindow& get_pages() override { return _albums; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    void open_row(size_t index) override;
    bool has_favourites() const override { return true; }
    void apply_favourite(const FavouriteChange& change) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
};
