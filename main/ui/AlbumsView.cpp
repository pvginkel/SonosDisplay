#include "AlbumsView.h"

#include <memory>
#include <utility>

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "TracksView.h"

AlbumsView::AlbumsView(UiHost* host, Shell* shell, const Artist& artist, uint32_t favourites_read)
    : ListView(host, shell, artist.title), _artist(artist), _favourites_read(favourites_read) {}

void AlbumsView::request_page(uint32_t page) {
    _albums.ask(page);

    _host->request_albums(_artist.id, page, get_scope(), [this](Page<Album> answered) {
        _albums.answered(std::move(answered));
        list_changed();
    });
}

const char* AlbumsView::get_loading_message() const { return MSG_ALBUMS_LOADING; }

const char* AlbumsView::get_empty_message() const { return MSG_ALBUMS_EMPTY; }

void AlbumsView::build_row(lv_obj_t* row, size_t index) {
    const auto& album = _albums.items[index];

    create_row_title(row, album.title);

    if (album.year) {
        auto year = lv_label_create(row);
        lv_obj_set_style_text_font(year, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
        lv_obj_set_style_text_color(year, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
        lv_label_set_text(year, std::to_string(*album.year).c_str());
    }

    make_openable(row);
}

// The albums' favourites are what their tracks screens open with.
void AlbumsView::apply_favourite(const FavouriteChange& change) {
    for (auto& album : _albums.items) {
        album.favourite_id = change.apply(ItemKind::Album, album.id, album.favourite_id);
    }
}

void AlbumsView::open_row(size_t index) {
    _shell->push(std::make_unique<TracksView>(_host, _shell, _albums.items[index], get_favourites_read()));
}

void AlbumsView::do_render_heading(lv_obj_t* heading) {
    create_heading_presses(heading, ItemKind::Artist, _artist.id, _artist.favourite_id, _favourites_read);
}

// An artist that did not come leaves the toggle as it is.
void AlbumsView::request_heading_item() {
    _host->request_artist(_artist.id, get_scope(), [this](std::optional<Artist> artist) {
        if (artist) {
            show_heading_favourite(artist->favourite_id);
        }
    });
}
