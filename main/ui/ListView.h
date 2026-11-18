#pragma once

// A screen of a list the backend gives in pages: a heading with a way back, unless the screen is
// the root, and under it a row, or a tile, for every item held. A subclass holds the pages
// (PagedList), asks the host for one, builds an item's row, which it may give the item's presses
// (ItemPresses), applies to its items the changes of a favourite the backend took, and
// opens an item whose row it made openable; it may end the heading with its own, the presses of
// the item the screen opens on among them, and say a line under it. A list whose items carry a
// favourite, or are the favourites, asks for them again whenever the favourites may have changed
// unseen, and a screen whose heading ends in an item's presses asks for the item again. While the
// Shell's "Reconnecting" marker shows at the top right, the heading ends before it.
//
// At most two pages are held, so at most 200 rows are built: scrolled to within a screen of an
// end, the list asks for the page past it, and once it comes the page at the far end from it is
// dropped, or, when a row on screen is in that one, the page that came; the rows on screen stay
// where they are. Asked for again (refresh()), the pages held come again one by one, the one at
// the top of the list first, each in its own place. While no page has come it says why instead: the
// first is on its way, did not come, with a way to ask again, or the list is empty. Past the rows,
// at an end with a page past it, a spinner shows while that page has not come, and why, with a
// way to ask again, when it did not.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ItemPresses.h"
#include "View.h"

class Shell;

class ListView : public View {
    std::string _title;
    lv_obj_t* _heading{};
    lv_obj_t* _note{};
    // Where the list, or what is said instead, is built.
    lv_obj_t* _content{};
    lv_obj_t* _list{};
    // The rows in _list: the first one's place among its children, and how many there are.
    uint32_t _first_row_child{};
    size_t _row_count{};
    // The place in the whole list of the first row's item.
    uint32_t _first_index{};
    // Whether the pages changed since the list was built.
    bool _changed{};
    // The pages held that refresh() asks for again once the one it asked for first came, in turn.
    std::vector<uint32_t> _asked_again;
    // The presses of each row built with them.
    std::vector<std::unique_ptr<ItemPresses>> _presses;
    // The presses at the heading's end of the item the screen opens on, if it has them: they stay
    // while the list is built again.
    std::unique_ptr<ItemPresses> _heading_presses;
    // The ids of the requests to queue an item, and of the changes of a favourite, that were on
    // their way when the presses last showed them.
    std::vector<uint32_t> _shown_presses;
    std::vector<uint32_t> _shown_changes;
    // How many changes of a favourite the backend had taken when the view last applied them, and
    // the model's favourites_changes then.
    uint32_t _changes_applied;
    uint32_t _favourites_changes;

protected:
    Shell* _shell;

public:
    // The height of a row, and of the space between two.
    static constexpr int32_t ROW_HEIGHT = 72;
    static constexpr int32_t ROW_GAP = 8;

    ListView(UiHost* host, Shell* shell, std::string title);

protected:
    void do_render(lv_obj_t* parent) override;
    void do_update() override;

    // The pages the subclass holds.
    virtual PageWindow& get_pages() = 0;

    // Asks the host for this page, after PageWindow::ask(). Its answer goes into the pages, and
    // calls list_changed().
    virtual void request_page(uint32_t page) = 0;

    // Builds the row of the item held at `index` in `row`, a flex row the list made, or, where the
    // list shows tiles, its tile, a flex column get_tile_width() wide and as tall as what it holds.
    virtual void build_row(lv_obj_t* row, size_t index) = 0;

    // The width of an item's tile, where the list shows its items as tiles, side by side and a line
    // of them under another; 0, where it shows them as rows, one under another.
    virtual int32_t get_tile_width() const { return 0; }

    // Whether the heading starts with a way back: not on the screen at the root.
    virtual bool has_back() const { return true; }

    // Opens the item held at `index`, on a press on its row, which build_row() made openable.
    virtual void open_row(size_t index) {}

    // Has a press on this row, which build_row() was handed, call open_row(). The list still
    // scrolls with a drag that starts on it.
    void make_openable(lv_obj_t* row);

    // A row's title, which takes the room the row has left and is cut where it ends. Returns its
    // label.
    static lv_obj_t* create_row_title(lv_obj_t* row, const std::string& title);

    // Ends this row, which build_row() was handed, with the presses of its item, of this kind and
    // with this id (ItemPresses), the toggle showing the favourite with `favourite_id`, or none.
    // Each shows its press on its way on every row of the item, built before the press or after it,
    // and the toggle what the backend took. Every row of a list has them, or none does.
    void create_queue_buttons(lv_obj_t* row, ItemKind kind, const std::string& item_id,
                              std::optional<int64_t> favourite_id);

    // Makes this row, which build_row() was handed, the line of the track with `track_id`, with its
    // presses (ItemPresses): a press on it plays the album with `album_id` from the track, and Add
    // to queue ends it. Each shows its press on its way as create_queue_buttons()'s do. Every row of
    // a list has them, or none does.
    void create_track_presses(lv_obj_t* row, const std::string& album_id, const std::string& track_id);

    // Whether the subclass's items carry the favourite that marks each, as artists, albums and
    // playlists do, or are the favourites. The list then applies to them the changes of a favourite
    // the backend took, and asks for them again whenever the favourites may have changed unseen
    // (Model::favourites_changes): a mark or an unmark made on any display.
    virtual bool has_favourites() const { return false; }

    // Applies a change of a favourite the backend took to the items held, of the kind the change
    // names (FavouriteChange::apply()), so that a row built from them shows it. Called, where the
    // list has_favourites(), for every change taken while the view lives, in order, each before the
    // rows show it, unless the view missed more of them than the model holds: it then asks for its
    // items again instead.
    virtual void apply_favourite(const FavouriteChange& change) {}

    // What the screen says while the first page is on its way, and when the list is empty, and the
    // icon over the latter.
    virtual const char* get_loading_message() const = 0;
    virtual const char* get_empty_message() const = 0;
    virtual const char* get_empty_icon() const;

    // Builds what the subclass shows at the heading's end, after its title, in `heading`, a flex
    // row. Called once, before the list is first built.
    virtual void do_render_heading(lv_obj_t* heading) {}

    // Ends `heading`, which do_render_heading() was handed, with the presses of the item the screen
    // opens on, of this kind and with this id (ItemPresses), the toggle showing the favourite with
    // `favourite_id`, or none. They show their press on its way, and the toggle what the backend
    // took, as a row's do, and stay while the list is built again. Whenever the favourites may have
    // changed unseen, the view asks for the item again (request_heading_item()).
    void create_heading_presses(lv_obj_t* heading, ItemKind kind, const std::string& item_id,
                                std::optional<int64_t> favourite_id);

    // Asks the host for the item whose presses end the heading (create_heading_presses()), whose
    // answer goes to show_heading_favourite(). Called whenever the favourites may have changed
    // unseen, or the view missed more of their changes than the model holds.
    virtual void request_heading_item() {}

    // The heading's toggle shows the favourite with this id, or none, as the one that marks its item:
    // what the backend holds, asked for again.
    void show_heading_favourite(std::optional<int64_t> favourite_id);

    // Builds what the subclass shows beside the list, in `body`, the row the list's `area` is in,
    // or over it, in `area`. Called once, before the list is first built.
    virtual void do_render_beside(lv_obj_t* body, lv_obj_t* area) {}

    // Called each time before the list is built, while what build_row() built before is still
    // there: what the subclass holds of it goes here.
    virtual void do_clear_list() {}

    // Called each time the list was built again, rows or not.
    virtual void do_list_built() {}

    // The pages changed: the next do_update() builds the list again.
    void list_changed() { _changed = true; }

    // Asks again for the pages held: first for the page of the row at the top of the list, or,
    // while no row is built, for the first page held, or the list's first; once it came, for each
    // other page still held, one by one. Each takes its own place, and the rows stay as they are
    // until their page comes.
    void refresh();

    // Says this in a line under the heading, or nothing with nullptr.
    void show_note(const char* note);

    // How many rows are built, one for each item held when the list was last built.
    size_t count_rows() const { return _row_count; }

    // Brings the row of the item held at `index` to the top of the list, or, near the list's end,
    // scrolls to the end with the row in view.
    void scroll_to_row(size_t index);

private:
    void fit_heading();
    void ask(uint32_t page);
    void build();
    void build_loading();
    void build_notice(const char* icon, const char* message, const std::string& detail, bool retry);
    void build_list();
    void build_page_state(uint32_t page);
    void ask_near_ends();
    void apply_favourites();
    void show_presses();
    size_t find_item(const lv_obj_t* row);
    static void back_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
    static void row_clicked(lv_event_t* event);
    static void scrolled(lv_event_t* event);
};
