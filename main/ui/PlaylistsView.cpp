#include "PlaylistsView.h"

#include <utility>

#include "Messages.h"

PlaylistsView::PlaylistsView(UiHost* host, Shell* shell) : ListView(host, shell, MSG_PLAYLISTS) {}

void PlaylistsView::request_page(uint32_t page) {
    _playlists.ask(page);

    _host->request_playlists(page, get_scope(), [this](Page<Playlist> answered) {
        _playlists.answered(std::move(answered));
        list_changed();
    });
}

const char* PlaylistsView::get_loading_message() const { return MSG_PLAYLISTS_LOADING; }

const char* PlaylistsView::get_empty_message() const { return MSG_PLAYLISTS_EMPTY; }

void PlaylistsView::build_row(lv_obj_t* row, size_t index) {
    create_row_title(row, _playlists.items[index].title);
    create_queue_buttons(row, true);
}

void PlaylistsView::queue_row(size_t index, QueueAction action) {
    _host->queue_item(
        QueueRequest{.kind = ItemKind::Playlist, .item_id = _playlists.items[index].id, .action = action});
}
