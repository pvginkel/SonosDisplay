#include "HomeView.h"

#include <memory>
#include <utility>

#include "AlbumsView.h"
#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "TracksView.h"
#include "lv_support.h"

// The entries, into the library and from a sheet into an item's albums or tracks, the motion
// setting and the sheet's way back are as tall as the heading.
static constexpr int32_t ENTRY_HEIGHT = 64;
static constexpr int32_t ENTRY_PADDING = 24;
static constexpr int32_t ENTRY_GAP = 12;
static constexpr int32_t SHEET_WIDTH = 600;
static constexpr int32_t SHEET_PADDING = 24;
static constexpr lv_opa_t SHEET_BACKDROP_OPA = LV_OPA_80;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

HomeView::HomeView(UiHost* host, Shell* shell)
    : ListView(host, shell, MSG_FAVOURITES), _revision(host->get_model().revision) {}

void HomeView::do_render(lv_obj_t* parent) {
    ListView::do_render(parent);

    build_sheet(parent);
}

void HomeView::do_update() {
    ListView::do_update();

    if (!lv_obj_has_flag(_sheet, LV_OBJ_FLAG_HIDDEN)) {
        _sheet_presses->show_on_its_way();
    }

    if (_host->get_model().motion_wakes != _shows_motion_wakes) {
        show_motion();
    }

    const auto revision = _host->get_model().revision;
    if (revision != _revision) {
        _revision = revision;
        show_pictures();
    }
}

void HomeView::request_page(uint32_t page) {
    _favourites.ask(page);

    _host->request_favourites(page, get_scope(), [this](Page<Favourite> answered) {
        Page<Held> held{.state = answered.state,
                        .error = std::move(answered.error),
                        .number = answered.number,
                        .total = answered.total};
        for (auto& favourite : answered.items) {
            const auto favourite_id = favourite.id;
            held.items.push_back(Held{.favourite = std::move(favourite), .favourite_id = favourite_id});
        }

        _favourites.answered(std::move(held));
        list_changed();
    });
}

const char* HomeView::get_loading_message() const { return MSG_FAVOURITES_LOADING; }

const char* HomeView::get_empty_message() const { return MSG_FAVOURITES_EMPTY; }

// The toggle's look while it marks nothing, which the message names.
const char* HomeView::get_empty_icon() const { return ICON_HEART_OUTLINE; }

// A button of an icon and a label on `background`, a Theme colour.
static void create_entry(lv_obj_t* parent, const char* icon, const char* text, uint32_t background,
                         lv_event_cb_t clicked, void* user_data) {
    auto entry = lv_container_create(parent);
    lv_obj_set_height(entry, ENTRY_HEIGHT);
    lv_obj_set_style_pad_hor(entry, ENTRY_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_column(entry, ENTRY_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(entry, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(entry, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(entry, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(entry, Theme::color(background), LV_PART_MAIN);
    lv_obj_set_style_bg_color(entry, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(entry, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(entry, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(entry, clicked, LV_EVENT_CLICKED, user_data);

    auto icon_label = lv_label_create(entry);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto label = lv_label_create(entry);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, Theme::FONT_TITLE, LV_PART_MAIN);
}

// The motion setting is an icon alone, so the title keeps its room beside the "Reconnecting" marker.
void HomeView::do_render_heading(lv_obj_t* heading) {
    auto motion = lv_container_create(heading);
    lv_obj_set_size(motion, ENTRY_HEIGHT, ENTRY_HEIGHT);
    lv_obj_add_flag(motion, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(motion, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(motion, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(motion, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(motion, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(motion, motion_clicked, LV_EVENT_CLICKED, this);

    _motion_icon = lv_label_create(motion);
    lv_obj_set_style_text_font(_motion_icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_center(_motion_icon);
    show_motion();

    create_entry(heading, ICON_MUSIC_BOX_MULTIPLE, MSG_LIBRARY, Theme::SURFACE, library_clicked, this);
}

// On, the sensor in the accent; off, the sensor struck through, in the secondary text's colour.
void HomeView::show_motion() {
    _shows_motion_wakes = _host->get_model().motion_wakes;

    lv_label_set_text(_motion_icon, _shows_motion_wakes ? ICON_MOTION_SENSOR : ICON_MOTION_SENSOR_OFF);
    lv_obj_set_style_text_color(_motion_icon, Theme::color(_shows_motion_wakes ? Theme::ACCENT : Theme::TEXT_SECONDARY),
                                LV_PART_MAIN);
}

// The picture over the title, which is cut where the tile ends.
void HomeView::build_row(lv_obj_t* row, size_t index) {
    const auto& favourite = _favourites.items[index].favourite;

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

    if (favourite.available) {
        make_openable(row);
    }
}

void HomeView::open_row(size_t index) {
    const auto& held = _favourites.items[index];

    open_sheet(held.favourite, held.favourite_id);
}

// The tiles stay as they are: the backend's event of the change has the favourites asked for again.
void HomeView::apply_favourite(const FavouriteChange& change) {
    for (auto& held : _favourites.items) {
        held.favourite_id = change.apply(held.favourite.kind, held.favourite.item_id, held.favourite_id);
    }
    if (_sheet_presses) {
        _sheet_presses->apply(change);
    }
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

// Over the whole view, which it dims. A press beside the sheet closes it, as its way back does, and
// one on the sheet between its buttons does nothing.
void HomeView::build_sheet(lv_obj_t* parent) {
    _sheet = lv_container_create(parent);
    lv_obj_set_size(_sheet, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_sheet, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_sheet, SHEET_BACKDROP_OPA, LV_PART_MAIN);
    lv_obj_add_event_cb(_sheet, sheet_closed, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(_sheet, LV_OBJ_FLAG_HIDDEN);

    auto panel = lv_container_create(_sheet);
    lv_obj_set_width(panel, SHEET_WIDTH);
    lv_obj_center(panel);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(panel, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(panel, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_set_style_pad_all(panel, SHEET_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_row(panel, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);

    auto heading = lv_container_create(panel);
    lv_obj_set_size(heading, LV_PCT(100), ENTRY_HEIGHT);
    lv_obj_set_style_pad_column(heading, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto back = lv_container_create(heading);
    lv_obj_set_size(back, ENTRY_HEIGHT, ENTRY_HEIGHT);
    lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(back, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_color(back, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(back, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(back, sheet_closed, LV_EVENT_CLICKED, this);

    auto back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, ICON_ARROW_LEFT);
    lv_obj_set_style_text_font(back_icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);
    lv_obj_center(back_icon);

    _sheet_title = lv_label_create(heading);
    lv_obj_set_flex_grow(_sheet_title, 1);
    lv_obj_set_height(_sheet_title, lv_font_get_line_height(Theme::FONT_HEADLINE));
    lv_label_set_long_mode(_sheet_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_sheet_title, Theme::FONT_HEADLINE, LV_PART_MAIN);

    _sheet_actions = lv_container_create(panel);
    lv_obj_set_width(_sheet_actions, LV_PCT(100));
    lv_obj_set_flex_flow(_sheet_actions, LV_FLEX_FLOW_ROW);
}

// The presses end the row, where a library row has them. What the sheet held before goes first,
// its buttons before the presses they call on.
void HomeView::open_sheet(const Favourite& favourite, std::optional<int64_t> favourite_id) {
    lv_obj_clean(_sheet_actions);

    _sheet_favourite = favourite;
    lv_label_set_text(_sheet_title, favourite.title.c_str());

    const auto opens = favourite.kind != ItemKind::Playlist;
    lv_obj_set_flex_align(_sheet_actions, opens ? LV_FLEX_ALIGN_SPACE_BETWEEN : LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    if (favourite.kind == ItemKind::Artist) {
        create_entry(_sheet_actions, ICON_MUSIC_BOX_MULTIPLE, MSG_ALBUMS, Theme::BACKGROUND, item_clicked, this);
    } else if (opens) {
        create_entry(_sheet_actions, ICON_MUSIC, MSG_TRACKS, Theme::BACKGROUND, item_clicked, this);
    }

    _sheet_presses =
        std::make_unique<ItemPresses>(_host, _sheet_actions, favourite.kind, favourite.item_id, favourite_id);
    _sheet_presses->show_on_its_way();

    lv_obj_remove_flag(_sheet, LV_OBJ_FLAG_HIDDEN);
}

void HomeView::motion_clicked(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->_host->set_motion_wakes(!self->_host->get_model().motion_wakes);
}

void HomeView::library_clicked(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->_shell->open_library();
}

void HomeView::scroll_ended(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    self->show_pictures();
}

// Hidden, not deleted: the press that closes it is on it.
void HomeView::sheet_closed(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);

    lv_obj_add_flag(self->_sheet, LV_OBJ_FLAG_HIDDEN);
}

// The sheet closes first, so Home shows without it once the screen opened closes.
void HomeView::item_clicked(lv_event_t* event) {
    auto self = (HomeView*)lv_event_get_user_data(event);
    const auto& favourite = self->_sheet_favourite;

    lv_obj_add_flag(self->_sheet, LV_OBJ_FLAG_HIDDEN);

    if (favourite.kind == ItemKind::Artist) {
        self->_shell->push(
            std::make_unique<AlbumsView>(self->_host, self->_shell,
                                         Artist{.id = favourite.item_id,
                                                .title = favourite.title,
                                                .favourite_id = self->_sheet_presses->get_favourite_id()}));
    } else {
        self->_shell->push(std::make_unique<TracksView>(self->_host, self->_shell,
                                                        Album{.id = favourite.item_id, .title = favourite.title}));
    }
}
