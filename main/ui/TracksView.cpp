#include "TracksView.h"

#include <utility>

#include "Messages.h"

TracksView::TracksView(UiHost* host, Shell* shell, const Album& album, uint32_t favourites_read)
    : ListView(host, shell, album.title), _album(album), _favourites_read(favourites_read) {}

void TracksView::request_page(uint32_t page) {
    _tracks.ask(page);

    _host->request_tracks(_album.id, page, get_scope(), [this](Page<AlbumTrack> answered) {
        _tracks.answered(std::move(answered));
        list_changed();
    });
}

const char* TracksView::get_loading_message() const { return MSG_TRACKS_LOADING; }

const char* TracksView::get_empty_message() const { return MSG_TRACKS_EMPTY; }

void TracksView::build_row(lv_obj_t* row, size_t index) {
    const auto& track = _tracks.items[index];

    create_row_title(row, track.title);
    create_track_presses(row, _album.id, track.id);
}

void TracksView::do_render_heading(lv_obj_t* heading) {
    create_heading_presses(heading, ItemKind::Album, _album.id, _album.favourite_id, _favourites_read);
}

// An album that did not come leaves the toggle as it is.
void TracksView::request_heading_item() {
    _host->request_album(_album.id, get_scope(), [this](std::optional<Album> album) {
        if (album) {
            show_heading_favourite(album->favourite_id);
        }
    });
}
