#pragma once

// The view the display opens on once a speaker is chosen, with the now-playing bar under it: the
// favourites as tiles, by title, under a heading that ends in the setting of whether motion wakes
// the screen, which a press switches and whose icon shows which way it is set, and the entry into
// the library. A tile shows its favourite's picture and title; one that is not available shows its
// title dimmed, and no picture, and opens nothing. A tile asks for its picture only while it shows
// in the list, and holds it only while it draws it: the tiles look as the list is built, as a
// scroll of it ends, and at every change of the model, which asks again for a picture the backend
// did not give. Home asks for the favourites again whenever they may have changed unseen. While
// the Shell's "Reconnecting" marker shows at the top right, the heading ends before it.
//
// A press on an available tile opens its sheet over the view: the favourite's title after a way
// back, and under it the presses its item's row in the library has (ItemPresses), and, for an
// artist or an album, the way to its albums or its tracks, which closes the sheet. A press beside
// the sheet closes it too. Its toggle shows the favourite that marks the item: the tile's, until the
// backend takes a mark or an unmark of the item from this display; the tile of a favourite unmarked
// stays until the favourites come again. An artist's albums open with the favourite the toggle
// shows.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Cover.h"
#include "ItemPresses.h"
#include "ListView.h"

class HomeView : public ListView {
    // A tile built: its box, the picture its favourite had as it was built, and the cover that
    // draws it.
    struct Tile {
        lv_obj_t* box;
        std::string artwork_id;
        std::unique_ptr<Cover> cover;
    };

    // A favourite held, and the favourite that marks its item now: its own as it came, then what
    // the backend took of the marks and unmarks of the item since (FavouriteChange::apply()).
    struct Held {
        Favourite favourite;
        std::optional<int64_t> favourite_id;
    };

    PagedList<Held> _favourites;
    // The motion setting's icon, and the setting it shows.
    lv_obj_t* _motion_icon{};
    bool _shows_motion_wakes{};
    // The tiles built, in the order of the favourites they were built from.
    std::vector<Tile> _tiles;
    // The model's revision when the tiles last looked.
    uint32_t _revision;
    // The sheet over the whole view, hidden while it is closed; its title; and the row that holds
    // the way to the item's albums or tracks and its presses, built as the sheet opens.
    lv_obj_t* _sheet{};
    lv_obj_t* _sheet_title{};
    lv_obj_t* _sheet_actions{};
    // The favourite the sheet last opened on, and the presses of its item, none before it first
    // opens.
    Favourite _sheet_favourite;
    std::unique_ptr<ItemPresses> _sheet_presses;

public:
    // The size of a tile's picture, and the tile's width.
    static constexpr int32_t TILE_SIZE = 160;

    HomeView(UiHost* host, Shell* shell);

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;
    PageWindow& get_pages() override { return _favourites; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    int32_t get_tile_width() const override { return TILE_SIZE; }
    bool has_back() const override { return false; }
    void open_row(size_t index) override;
    bool has_favourites() const override { return true; }
    void apply_favourite(const FavouriteChange& change) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
    const char* get_empty_icon() const override;
    void do_render_heading(lv_obj_t* heading) override;
    void do_clear_list() override;
    void do_list_built() override;

private:
    void show_motion();
    void show_pictures();
    void build_sheet(lv_obj_t* parent);
    void open_sheet(const Favourite& favourite, std::optional<int64_t> favourite_id);
    static void motion_clicked(lv_event_t* event);
    static void library_clicked(lv_event_t* event);
    static void scroll_ended(lv_event_t* event);
    static void sheet_closed(lv_event_t* event);
    static void item_clicked(lv_event_t* event);
};
