#include "TracksView.h"

#include <utility>

#include "Messages.h"

TracksView::TracksView(UiHost* host, Shell* shell, const Album& album)
    : ListView(host, shell, album.title), _album_id(album.id) {}

void TracksView::request_page(uint32_t page) {
    _tracks.ask(page);

    _host->request_tracks(_album_id, page, get_scope(), [this](Page<AlbumTrack> answered) {
        _tracks.answered(std::move(answered));
        list_changed();
    });
}

const char* TracksView::get_loading_message() const { return MSG_TRACKS_LOADING; }

const char* TracksView::get_empty_message() const { return MSG_TRACKS_EMPTY; }

void TracksView::build_row(lv_obj_t* row, size_t index) {
    const auto& track = _tracks.items[index];

    create_row_title(row, track.title);
    create_queue_buttons(row, ItemKind::Track, track.id);
}
