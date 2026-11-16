#pragma once

// A screen of a list the backend gives in pages: a heading with a way back, and under it a row
// for every item held. A subclass holds the pages (PagedList), asks the host for one, builds an
// item's row, and opens an item whose row it made openable. While the Shell's "Reconnecting"
// marker shows at the top right, the heading ends before it.
//
// At most two pages are held, so at most 200 rows are built: scrolled to within a screen of an
// end, the list asks for the page past it, and once it comes the page at the far end from it is
// dropped, or, when a row on screen is in that one, the page that came; the rows on screen stay
// where they are. While no page has come it says why instead: the
// first is on its way, did not come, with a way to ask again, or the list is empty. Past the rows,
// at an end with a page past it, a spinner shows while that page has not come, and why, with a
// way to ask again, when it did not.

#include <cstddef>
#include <cstdint>
#include <string>

#include "View.h"

class Shell;

class ListView : public View {
    std::string _title;
    lv_obj_t* _heading{};
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

    // Builds the row of the item held at `index` in `row`, a flex row the list made.
    virtual void build_row(lv_obj_t* row, size_t index) = 0;

    // Opens the item held at `index`, on a press on its row, which build_row() made openable.
    virtual void open_row(size_t index) {}

    // Has a press on this row, which build_row() was handed, call open_row(). The list still
    // scrolls with a drag that starts on it.
    void make_openable(lv_obj_t* row);

    // A row's title, which takes the room the row has left and is cut where it ends.
    static void create_row_title(lv_obj_t* row, const std::string& title);

    // What the screen says while the first page is on its way, and when the list is empty.
    virtual const char* get_loading_message() const = 0;
    virtual const char* get_empty_message() const = 0;

    // Builds what the subclass shows beside the list, in `body`, the row the list's `area` is in,
    // or over it, in `area`. Called once, before the list is first built.
    virtual void do_render_beside(lv_obj_t* body, lv_obj_t* area) {}

    // Called each time the list was built again, rows or not.
    virtual void do_list_built() {}

    // The pages changed: the next do_update() builds the list again.
    void list_changed() { _changed = true; }

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
    static void back_clicked(lv_event_t* event);
    static void retry_clicked(lv_event_t* event);
    static void row_clicked(lv_event_t* event);
    static void scrolled(lv_event_t* event);
};
