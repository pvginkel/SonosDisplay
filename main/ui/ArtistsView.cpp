#include "ArtistsView.h"

#include "Messages.h"
#include "Theme.h"
#include "lv_support.h"

// The button that opens the letters, beside the list.
static constexpr int32_t JUMP_SIZE = 80;

// '#' and 'A' to 'Z': every letter an artist files under fits the grid.
static constexpr uint8_t GRID_COLUMNS = 9;
static constexpr uint8_t GRID_ROWS = 3;
static constexpr int32_t GRID_GAP = 8;

static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

ArtistsView::ArtistsView(UiHost* host, Shell* shell) : ListView(host, shell, MSG_ARTISTS) {}

void ArtistsView::request_page(uint32_t page) {
    _artists.ask(page);

    _host->request_artists(page, get_scope(), [this](Page<Artist> answered) {
        _artists.answered(std::move(answered));
        list_changed();
    });
}

const char* ArtistsView::get_loading_message() const { return MSG_ARTISTS_LOADING; }

const char* ArtistsView::get_empty_message() const { return MSG_ARTISTS_EMPTY; }

void ArtistsView::build_row(lv_obj_t* row, size_t index) {
    auto title = lv_label_create(row);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_height(title, lv_font_get_line_height(Theme::FONT_BODY_LARGE));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_label_set_text(title, _artists.items[index].title.c_str());
}

void ArtistsView::do_render_beside(lv_obj_t* body, lv_obj_t* area) {
    auto rail = lv_container_create(body);
    lv_obj_set_height(rail, LV_PCT(100));
    lv_obj_set_style_pad_left(rail, Theme::GAP, LV_PART_MAIN);

    _jump = lv_container_create(rail);
    lv_obj_set_size(_jump, JUMP_SIZE, JUMP_SIZE);
    lv_obj_add_flag(_jump, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_jump, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_jump, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(_jump, jump_clicked, LV_EVENT_CLICKED, this);

    _jump_label = lv_label_create(_jump);
    lv_label_set_text(_jump_label, MSG_JUMP_TO_LETTER);
    lv_obj_set_style_text_font(_jump_label, Theme::FONT_TITLE, LV_PART_MAIN);
    lv_obj_center(_jump_label);

    // Over the list, which it hides while it shows.
    static const int32_t COLUMNS[] = {LV_GRID_FR(1), LV_GRID_FR(1),        LV_GRID_FR(1), LV_GRID_FR(1),
                                      LV_GRID_FR(1), LV_GRID_FR(1),        LV_GRID_FR(1), LV_GRID_FR(1),
                                      LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t ROWS[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static_assert(sizeof(COLUMNS) / sizeof(COLUMNS[0]) == GRID_COLUMNS + 1);
    static_assert(sizeof(ROWS) / sizeof(ROWS[0]) == GRID_ROWS + 1);

    // It takes the presses between its letters, which would scroll the list under it.
    _grid = lv_container_create(area);
    lv_obj_set_size(_grid, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(_grid, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_grid, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_grid, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_grid, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_grid, GRID_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(_grid, GRID_GAP, LV_PART_MAIN);
    lv_obj_set_grid_dsc_array(_grid, COLUMNS, ROWS);

    show_letters(false);
}

void ArtistsView::do_list_built() {
    _letters.clear();

    for (size_t i = 0; i < count_rows(); i++) {
        const auto letter = _artists.items[i].index_letter;
        if (_letters.empty() || _letters.back().first != letter) {
            _letters.emplace_back(letter, i);
        }
    }

    lv_obj_update_flag(lv_obj_get_parent(_jump), LV_OBJ_FLAG_HIDDEN, _letters.empty());

    build_letters();

    if (_letters.empty()) {
        show_letters(false);
    }
}

// One button for each letter, in order, row by row.
void ArtistsView::build_letters() {
    lv_obj_clean(_grid);

    for (size_t i = 0; i < _letters.size() && i < size_t(GRID_COLUMNS * GRID_ROWS); i++) {
        auto button = lv_container_create(_grid);
        lv_obj_set_grid_cell(button, LV_GRID_ALIGN_STRETCH, uint8_t(i % GRID_COLUMNS), LV_GRID_ALIGN_STRETCH,
                             uint8_t(i / GRID_COLUMNS));
        lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
        lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
        lv_obj_add_event_cb(button, letter_clicked, LV_EVENT_CLICKED, this);

        const char text[] = {_letters[i].first, 0};
        auto label = lv_label_create(button);
        lv_label_set_text(label, text);
        lv_obj_set_style_text_font(label, Theme::FONT_HEADLINE, LV_PART_MAIN);
        lv_obj_center(label);
    }
}

void ArtistsView::show_letters(bool shown) {
    lv_obj_update_flag(_grid, LV_OBJ_FLAG_HIDDEN, !shown);

    lv_obj_set_style_bg_color(_jump, Theme::color(shown ? Theme::ACCENT : Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_jump, Theme::color(shown ? Theme::ACCENT_PRESSED : Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_text_color(_jump_label, Theme::color(shown ? Theme::BACKGROUND : Theme::TEXT), LV_PART_MAIN);
}

void ArtistsView::jump_clicked(lv_event_t* event) {
    auto self = (ArtistsView*)lv_event_get_user_data(event);

    self->show_letters(lv_obj_has_flag(self->_grid, LV_OBJ_FLAG_HIDDEN));
}

void ArtistsView::letter_clicked(lv_event_t* event) {
    auto self = (ArtistsView*)lv_event_get_user_data(event);
    const auto index = size_t(lv_obj_get_index(lv_event_get_current_target_obj(event)));

    self->show_letters(false);
    self->scroll_to_row(self->_letters[index].second);
}
