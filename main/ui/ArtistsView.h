#pragma once

// The library's artists, as the backend sorts them, those that file under no letter first, and a
// jump to a letter. The button beside the list opens the letters its artists file under, as a
// grid over the list, each letter as large a target as the grid allows; a press on one brings
// its first artist to the top, and closes the grid. The letters are those of the rows built,
// which are the whole library while it has 200 artists or fewer.

#include <cstddef>
#include <utility>
#include <vector>

#include "ListView.h"

class ArtistsView : public ListView {
    PagedList<Artist> _artists;
    // The letters the rows file under, in order, each with the index of its first row.
    std::vector<std::pair<char, size_t>> _letters;
    lv_obj_t* _jump{};
    lv_obj_t* _jump_label{};
    lv_obj_t* _grid{};

public:
    ArtistsView(UiHost* host, Shell* shell);

protected:
    const PageWindow& get_pages() const override { return _artists; }
    void request_page(uint32_t page) override;
    void build_row(lv_obj_t* row, size_t index) override;
    const char* get_loading_message() const override;
    const char* get_empty_message() const override;
    void do_render_beside(lv_obj_t* body, lv_obj_t* area) override;
    void do_list_built() override;

private:
    void build_letters();
    void show_letters(bool shown);
    static void jump_clicked(lv_event_t* event);
    static void letter_clicked(lv_event_t* event);
};
