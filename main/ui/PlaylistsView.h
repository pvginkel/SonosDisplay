#pragma once

// The library's playlists, by title. A playlist is never opened: the backend does not list its
// tracks.

#include <cstddef>
#include <cstdint>

#include "ListView.h"

class PlaylistsView : public ListView {
    PagedList<Playlist> _playlists;

public:
    PlaylistsView(UiHost* host, Shell* shell);

protected:
    PageWindow& get_pages() override { return _playlists; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
};
