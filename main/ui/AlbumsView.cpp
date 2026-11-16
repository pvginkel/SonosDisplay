#include "AlbumsView.h"

#include <memory>
#include <utility>

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "TracksView.h"

AlbumsView::AlbumsView(UiHost* host, Shell* shell, const Artist& artist)
    : ListView(host, shell, artist.title), _artist_id(artist.id) {}

void AlbumsView::request_page(uint32_t page) {
    _albums.ask(page);

    _host->request_albums(_artist_id, page, get_scope(), [this](Page<Album> answered) {
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

    create_queue_buttons(row, ItemKind::Album, album.id);
    make_openable(row);
}

void AlbumsView::open_row(size_t index) {
    _shell->push(std::make_unique<TracksView>(_host, _shell, _albums.items[index]));
}
