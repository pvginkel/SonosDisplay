#include "HomeView.h"

#include <utility>

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

// The entry into the library is as tall as the heading.
static constexpr int32_t ENTRY_HEIGHT = 64;
static constexpr int32_t ENTRY_PADDING = 24;
static constexpr int32_t ENTRY_GAP = 12;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

HomeView::HomeView(UiHost* host, Shell* shell)
    : ListView(host, shell, MSG_FAVOURITES), _revision(host->get_model().revision) {}

void HomeView::do_update() {
    ListView::do_update();

    const auto revision = _host->get_model().revision;
    if (revision != _revision) {
        _revision = revision;
        show_pictures();
    }
}

void HomeView::request_page(uint32_t page) {
    _favourites.ask(page);

    _host->request_favourites(page, get_scope(), [this](Page<Favourite> answered) {
        _favourites.answered(std::move(answered));
        list_changed();
    });
}

const char* HomeView::get_loading_message() const { return MSG_FAVOURITES_LOADING; }

const char* HomeView::get_empty_message() const { return MSG_FAVOURITES_EMPTY; }

// The toggle's look while it marks nothing, which the message names.
const char* HomeView::get_empty_icon() const { return ICON_HEART_OUTLINE; }

void HomeView::do_render_heading(lv_obj_t* heading) {
    auto library = lv_container_create(heading);
    lv_obj_set_height(library, ENTRY_HEIGHT);
    lv_obj_set_style_pad_hor(library, ENTRY_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_column(library, ENTRY_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(library, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(library, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(library, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(library, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(library, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(library, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(library, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(library, library_clicked, LV_EVENT_CLICKED, this);

    auto library_icon = lv_label_create(library);
    lv_label_set_text(library_icon, ICON_MUSIC_BOX_MULTIPLE);
    lv_obj_set_style_text_font(library_icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(library_icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto library_label = lv_label_create(library);
    lv_label_set_text(library_label, MSG_LIBRARY);
    lv_obj_set_style_text_font(library_label, Theme::FONT_TITLE, LV_PART_MAIN);
}

// The picture over the title, which is cut where the tile ends.
void HomeView::build_row(lv_obj_t* row, size_t index) {
    const auto& favourite = _favourites.items[index];

    auto cover = std::make_unique<Cover>(_host, TILE_SIZE);
    cover->create(row, Theme::FONT_ICON_LARGE);

    auto title = lv_label_create(row);
    lv_obj_set_size(title, LV_PCT(100), lv_font_get_line_height(Theme::FONT_BODY_LARGE));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    if (!favourite.available) {
        lv_obj_set_style_text_color(title, Theme::color(Theme::TEXT_UNAVAILABLE), LV_PART_MAIN);
    }
    lv_label_set_text(title, favourite.title.c_str());

    _tiles.push_back(Tile{.box = row, .artwork_id = favourite.artwork_id, .cover = std::move(cover)});
}

// Each cover deletes its picture's widget before it lets go of the picture.
void HomeView::do_clear_list() { _tiles.clear(); }

void HomeView::do_list_built() {
    if (_tiles.empty()) {
        return;
    }

    lv_obj_add_event_cb(lv_obj_get_parent(_tiles.front().box), scroll_ended, LV_EVENT_SCROLL_END, this);
    show_pictures();
}

// A tile shows while any of it is in the list's box. A favourite that is not available has no
// picture to ask for.
void HomeView::show_pictures() {
    if (_tiles.empty()) {
        return;
    }

    const auto list = lv_obj_get_parent(_tiles.front().box);
    lv_obj_update_layout(list);

    lv_area_t shown;
    lv_obj_get_coords(list, &shown);

    for (auto& tile : _tiles) {
        lv_area_t area;
        lv_obj_get_coords(tile.box, &area);

        if (area.y2 >= shown.y1 && area.y1 <= shown.y2) {
            tile.cover->apply(tile.artwork_id);
        } else {
            tile.cover->clear();
        }
    }
}

void HomeView::library_clicked(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->_shell->open_library();
}

void HomeView::scroll_ended(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->show_pictures();
}
