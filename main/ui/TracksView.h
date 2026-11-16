#pragma once

// An album's tracks, in disc and track order. A track's buttons play it or add it to the queue: a
// track is never shuffled.

#include <cstddef>
#include <cstdint>
#include <string>

#include "ListView.h"

class TracksView : public ListView {
    std::string _album_id;
    PagedList<AlbumTrack> _tracks;

public:
    TracksView(UiHost* host, Shell* shell, const Album& album);

protected:
    PageWindow& get_pages() override { return _tracks; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    void queue_row(size_t index, QueueAction action) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
};
