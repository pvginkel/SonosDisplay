#include "ListView.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "Messages.h"
#include "PressButton.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t HEADER_TOP = 16;
static constexpr int32_t HEADER_HEIGHT = 64;
static constexpr int32_t BACK_SIZE = 64;
static constexpr int32_t ROW_PADDING = 24;
// Room at the list's right for its scrollbar, beside the rows.
static constexpr int32_t SCROLLBAR_ROOM = 16;
static constexpr int32_t SCROLLBAR_WIDTH = 6;
static constexpr int32_t NOTICE_WIDTH = 560;
static constexpr int32_t NOTICE_DETAIL_LINES = 2;
static constexpr int32_t BUTTON_HEIGHT = 72;
static constexpr int32_t BUTTON_PADDING = 40;
static constexpr int32_t SPINNER_SIZE = 56;
static constexpr int32_t SPINNER_ARC_WIDTH = 6;
static constexpr int32_t TILE_GAP = 24;
static constexpr int32_t TILE_TITLE_GAP = 8;
// The queue's buttons at a row's end, as far from its end as from its top and bottom.
static constexpr int32_t QUEUE_BUTTONS_END = (ListView::ROW_HEIGHT - PressButton::SIZE) / 2;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

ListView::ListView(UiHost* host, Shell* shell, std::string title)
    : View(host),
      _title(std::move(title)),
      _changes_applied(host->get_model().favouring.changes_taken),
      _favourites_changes(host->get_model().favourites_changes),
      _shell(shell) {}

void ListView::do_render(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_size(column, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_hor(column, Theme::PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_top(column, HEADER_TOP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    // The heading at the left: the Shell's "Reconnecting" marker shows at the right of this row.
    _heading = lv_container_create(column);
    lv_obj_set_size(_heading, LV_PCT(100), HEADER_HEIGHT);
    lv_obj_set_style_pad_column(_heading, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(_heading, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_heading, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    fit_heading();

    if (has_back()) {
        auto back = lv_container_create(_heading);
        lv_obj_set_size(back, BACK_SIZE, BACK_SIZE);
        lv_obj_add_flag(back, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(back, Theme::color(Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_color(back, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
        lv_obj_set_style_bg_opa(back, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(back, Theme::RADIUS, LV_PART_MAIN);
        lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, this);

        auto back_icon = lv_label_create(back);
        lv_label_set_text(back_icon, ICON_ARROW_LEFT);
        lv_obj_set_style_text_font(back_icon, Theme::FONT_ICON, LV_PART_MAIN);
        lv_obj_set_style_text_color(back_icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);
        lv_obj_center(back_icon);
    }

    auto title = lv_label_create(_heading);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_set_height(title, lv_font_get_line_height(Theme::FONT_HEADLINE));
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(title, Theme::FONT_HEADLINE, LV_PART_MAIN);
    lv_label_set_text(title, _title.c_str());

    do_render_heading(_heading);

    // One line, cut where it ends.
    _note = lv_label_create(column);
    lv_obj_set_size(_note, LV_PCT(100), Theme::GAP + lv_font_get_line_height(Theme::FONT_BODY_LARGE));
    lv_obj_set_style_pad_top(_note, Theme::GAP, LV_PART_MAIN);
    lv_label_set_long_mode(_note, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_note, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(_note, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
    lv_obj_add_flag(_note, LV_OBJ_FLAG_HIDDEN);

    auto body = lv_container_create(column);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    // Padding, not a margin: LVGL's flex gives a growing item the room its margin takes as well,
    // which puts the body's end under the view's.
    lv_obj_set_style_pad_top(body, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_ROW);

    auto area = lv_container_create(body);
    lv_obj_set_height(area, LV_PCT(100));
    lv_obj_set_flex_grow(area, 1);

    _content = lv_container_create(area);
    lv_obj_set_size(_content, LV_PCT(100), LV_PCT(100));

    do_render_beside(body, area);

    ask(1);
    build();
}

void ListView::do_update() {
    fit_heading();
    if (has_favourites() || _heading_presses) {
        apply_favourites();
    }

    // Not under a finger that scrolls the list: its rows would go, and the scroll with them.
    if (_changed && !(_list && lv_obj_is_scrolling(_list))) {
        build();
    } else {
        show_presses();
    }
}

// The heading ends a gap before the marker, whose reach is from the screen's edge, where the
// heading's is from the view's padding.
void ListView::fit_heading() {
    const auto reach = _shell->get_marker_reach();
    const auto room = reach ? reach + Theme::GAP - Theme::PADDING : 0;

    if (room != lv_obj_get_style_pad_right(_heading, LV_PART_MAIN)) {
        lv_obj_set_style_pad_right(_heading, room, LV_PART_MAIN);
    }
}

// With pages held, the spinner past them shows already, unless the page did not come before. The
// answer builds the list again.
void ListView::ask(uint32_t page) {
    const auto& pages = get_pages();
    const auto shown = !pages.is_held() || pages.has_failed(page);

    request_page(page);

    if (shown) {
        _changed = true;
    }
}

void ListView::build() {
    _changed = false;

    auto& pages = get_pages();

    // The rows on screen: the one at the top, how far past its top the list is scrolled, and the
    // one at the bottom. The list built again shows the top one where it was. A list scrolled to
    // its end stays there, unless rows come after it.
    std::optional<uint32_t> anchor;
    int32_t anchor_offset = 0;
    uint32_t last_shown = 0;
    auto at_end = false;
    const auto end_before = _first_index + uint32_t(_row_count);
    if (_list) {
        lv_obj_update_layout(_list);
        const auto scrolled = lv_obj_get_scroll_y(_list);
        const auto shown_end = scrolled + lv_obj_get_height(_list);
        at_end = lv_obj_get_scroll_bottom(_list) <= 0;

        for (size_t i = 0; i < _row_count; i++) {
            const auto row = lv_obj_get_child(_list, int32_t(_first_row_child + i));
            const auto top = lv_obj_get_y(row);
            if (top >= shown_end) {
                break;
            }
            if (top + lv_obj_get_height(row) <= scrolled) {
                continue;
            }

            if (!anchor) {
                anchor = _first_index + uint32_t(i);
                anchor_offset = scrolled - top;
            }
            last_shown = _first_index + uint32_t(i);
        }
    }

    // A page came beside those built, one past MAX_PAGES: the page at the far end from it goes,
    // unless a row on screen is in it; then the page that came goes.
    if (pages.page_sizes.size() > PageWindow::MAX_PAGES) {
        const size_t first = pages.first_index();
        const auto came_first = first < _first_index;
        const auto far_shown = anchor && (came_first ? last_shown >= first + pages.count() - pages.page_sizes.back()
                                                     : *anchor < first + pages.page_sizes.front());

        pages.drop(came_first == far_shown);
    }

    do_clear_list();
    lv_obj_clean(_content);
    _list = nullptr;
    _row_count = 0;
    _presses.clear();
    _shown_presses.clear();
    _shown_changes.clear();

    if (!pages.is_held()) {
        switch (pages.state) {
            case LoadState::Loading:
            case LoadState::Loaded:
                build_loading();
                break;
            case LoadState::Unreachable:
                build_notice(ICON_CLOUD_OFF, MSG_BACKEND_UNREACHABLE, "", true);
                break;
            case LoadState::Failed:
                build_notice(ICON_ALERT, MSG_BACKEND_FAILED, pages.error, true);
                break;
        }
    } else if (!pages.count()) {
        build_notice(get_empty_icon(), get_empty_message(), "", false);
    } else {
        build_list();
        lv_obj_update_layout(_list);

        const auto first = pages.first_index();
        if (at_end && first + pages.count() == end_before) {
            lv_obj_scroll_to_y(_list, LV_COORD_MAX, LV_ANIM_OFF);
        } else if (anchor && *anchor >= first && *anchor - first < _row_count) {
            const auto row = lv_obj_get_child(_list, int32_t(_first_row_child + *anchor - first));

            lv_obj_scroll_to_y(_list, lv_obj_get_y(row) + anchor_offset, LV_ANIM_OFF);
        }
    }

    do_list_built();
    show_presses();

    // What ask_near_ends() held back while a page that came was not built yet is asked for here.
    if (_list) {
        ask_near_ends();
    }
}

static lv_obj_t* create_centered_column(lv_obj_t* parent) {
    auto column = lv_container_create(parent);
    lv_obj_set_width(column, LV_PCT(100));
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(column, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_center(column);

    return column;
}

static void create_spinner(lv_obj_t* parent) {
    auto spinner = lv_spinner_create(parent);
    lv_obj_set_size(spinner, SPINNER_SIZE, SPINNER_SIZE);
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(spinner, SPINNER_ARC_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, Theme::color(Theme::ACCENT), LV_PART_INDICATOR);
}

static lv_obj_t* create_retry(lv_obj_t* parent, lv_event_cb_t clicked, void* user_data) {
    auto button = lv_button_create(parent);
    lv_obj_set_height(button, BUTTON_HEIGHT);
    lv_obj_set_style_pad_hor(button, BUTTON_PADDING, LV_PART_MAIN);
    lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);

    auto label = lv_label_create(button);
    lv_label_set_text(label, MSG_TRY_AGAIN);
    lv_obj_set_style_text_font(label, Theme::FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_center(label);

    return button;
}

void ListView::build_loading() {
    auto column = create_centered_column(_content);

    create_spinner(column);

    auto label = lv_label_create(column);
    lv_label_set_text(label, get_loading_message());
    lv_obj_set_style_text_font(label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
}

// The backend's own words, which may be any length: they get two lines.
static void create_detail(lv_obj_t* parent, const std::string& detail, int32_t width) {
    auto label = lv_label_create(parent);
    lv_obj_set_width(label, width);
    lv_obj_set_style_max_height(label, NOTICE_DETAIL_LINES * lv_font_get_line_height(Theme::FONT_BODY), LV_PART_MAIN);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
    lv_label_set_text(label, detail.c_str());
}

void ListView::build_notice(const char* icon, const char* message, const std::string& detail, bool retry) {
    auto column = create_centered_column(_content);

    auto icon_label = lv_label_create(column);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto message_label = lv_label_create(column);
    lv_label_set_text(message_label, message);
    lv_obj_set_width(message_label, NOTICE_WIDTH);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(message_label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);

    if (!detail.empty()) {
        create_detail(column, detail, NOTICE_WIDTH);
    }

    if (retry) {
        create_retry(column, retry_clicked, this);
    }
}

const char* ListView::get_empty_icon() const { return ICON_MUSIC; }

void ListView::build_list() {
    const auto& pages = get_pages();
    const auto tile_width = get_tile_width();

    // A page's state past the tiles takes a line of its own: it is as wide as the list.
    _list = lv_container_create(_content);
    lv_obj_set_size(_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(_list, tile_width ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(_list, tile_width ? TILE_GAP : ROW_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(_list, TILE_GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(_list, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_list, SCROLLBAR_ROOM, LV_PART_MAIN);
    // A press anywhere on the list scrolls it, a row's too: the rows take no press.
    lv_obj_add_flag(_list, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_set_style_width(_list, SCROLLBAR_WIDTH, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(_list, SCROLLBAR_WIDTH / 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(_list, Theme::color(Theme::TEXT_UNAVAILABLE), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(_list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_add_event_cb(_list, scrolled, LV_EVENT_SCROLL, this);

    if (const auto before = pages.page_before()) {
        build_page_state(before);
    }

    _first_row_child = lv_obj_get_child_count(_list);
    _first_index = pages.first_index();
    _row_count = pages.count();

    for (size_t i = 0; i < _row_count; i++) {
        if (tile_width) {
            auto tile = lv_container_create(_list);
            lv_obj_set_size(tile, tile_width, LV_SIZE_CONTENT);
            lv_obj_set_style_pad_row(tile, TILE_TITLE_GAP, LV_PART_MAIN);
            lv_obj_set_flex_flow(tile, LV_FLEX_FLOW_COLUMN);

            build_row(tile, i);
            continue;
        }

        auto row = lv_container_create(_list);
        lv_obj_set_size(row, LV_PCT(100), ROW_HEIGHT);
        lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_radius(row, Theme::RADIUS, LV_PART_MAIN);
        lv_obj_set_style_pad_hor(row, ROW_PADDING, LV_PART_MAIN);
        lv_obj_set_style_pad_column(row, Theme::GAP, LV_PART_MAIN);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        build_row(row, i);
    }

    if (const auto after = pages.page_after()) {
        build_page_state(after);
    }
}

// A page past the rows: a spinner while it has not come, which it is asked for at within a
// screen of it, and when it did not come, why, in the backend's words where it said so, with a
// way to ask again.
void ListView::build_page_state(uint32_t page) {
    const auto& pages = get_pages();

    auto state = lv_container_create(_list);
    lv_obj_set_width(state, LV_PCT(100));
    lv_obj_set_style_min_height(state, ROW_HEIGHT, LV_PART_MAIN);
    lv_obj_set_style_pad_row(state, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(state, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(state, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    if (!pages.has_failed(page)) {
        create_spinner(state);
        return;
    }

    auto message = lv_label_create(state);
    lv_obj_set_width(message, LV_PCT(100));
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(message, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_label_set_text(message, pages.state == LoadState::Unreachable ? MSG_BACKEND_UNREACHABLE : MSG_BACKEND_FAILED);

    if (!pages.error.empty()) {
        create_detail(state, pages.error, LV_PCT(100));
    }

    create_retry(state, retry_clicked, this);
}

void ListView::make_openable(lv_obj_t* row) {
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(row, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_add_event_cb(row, row_clicked, LV_EVENT_CLICKED, this);
}

lv_obj_t* ListView::create_row_title(lv_obj_t* row, const std::string& title) {
    auto label = lv_label_create(row);
    lv_obj_set_flex_grow(label, 1);
    lv_obj_set_height(label, lv_font_get_line_height(Theme::FONT_BODY_LARGE));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(label, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_label_set_text(label, title.c_str());

    return label;
}

void ListView::create_queue_buttons(lv_obj_t* row, ItemKind kind, const std::string& item_id,
                                    std::optional<int64_t> favourite_id) {
    lv_obj_set_style_pad_right(row, QUEUE_BUTTONS_END, LV_PART_MAIN);

    _presses.push_back(std::make_unique<ItemPresses>(_host, row, kind, item_id, favourite_id));
}

void ListView::create_track_presses(lv_obj_t* row, const std::string& album_id, const std::string& track_id) {
    lv_obj_set_style_pad_right(row, QUEUE_BUTTONS_END, LV_PART_MAIN);

    _presses.push_back(std::make_unique<ItemPresses>(_host, row, album_id, track_id));
}

void ListView::create_heading_presses(lv_obj_t* heading, ItemKind kind, const std::string& item_id,
                                      std::optional<int64_t> favourite_id) {
    _heading_presses = std::make_unique<ItemPresses>(_host, heading, kind, item_id, favourite_id);
}

void ListView::show_heading_favourite(std::optional<int64_t> favourite_id) {
    _heading_presses->show_favourite(favourite_id);
}

// The changes the backend took since the view last applied them go to the subclass's items, and
// each toggle built shows what they leave of its item's favourite. A view that missed more of them
// than are held, or while the favourites may have changed unseen, asks for its items again, and for
// the heading's item, which show what the backend holds: after the changes are applied, so that the
// answer takes their place.
void ListView::apply_favourites() {
    const auto& model = _host->get_model();
    const auto& favouring = model.favouring;
    const auto count = favouring.changes_taken - _changes_applied;
    const auto missed = count > favouring.changes.size();
    const auto unseen = model.favourites_changes != _favourites_changes;

    _changes_applied = favouring.changes_taken;
    _favourites_changes = model.favourites_changes;

    if (!missed) {
        for (auto change = favouring.changes.end() - ptrdiff_t(count); change != favouring.changes.end(); change++) {
            apply_favourite(*change);

            for (const auto& presses : _presses) {
                presses->apply(*change);
            }
            if (_heading_presses) {
                _heading_presses->apply(*change);
            }
        }
    }

    if (!missed && !unseen) {
        return;
    }
    if (has_favourites()) {
        refresh();
    }
    if (_heading_presses) {
        request_heading_item();
    }
}

// Whether the ids of `sending`, of the requests or the changes on their way, are those of `shown`.
// Makes them so.
template <typename Sent>
static bool update_shown(const std::vector<Sent>& sending, std::vector<uint32_t>& shown) {
    if (std::equal(sending.begin(), sending.end(), shown.begin(), shown.end(),
                   [](const Sent& sent, uint32_t id) { return sent.id == id; })) {
        return true;
    }

    shown.clear();
    for (const auto& sent : sending) {
        shown.push_back(sent.id);
    }

    return false;
}

// Each row's presses, and the heading's, show whether they are on their way, once the requests or
// the changes on their way changed since the presses last showed them.
void ListView::show_presses() {
    const auto& model = _host->get_model();
    const auto presses_shown = update_shown(model.queueing.sending, _shown_presses);
    if (update_shown(model.favouring.sending, _shown_changes) && presses_shown) {
        return;
    }

    for (const auto& presses : _presses) {
        presses->show_on_its_way();
    }
    if (_heading_presses) {
        _heading_presses->show_on_its_way();
    }
}

void ListView::scroll_to_row(size_t index) {
    const auto row = lv_obj_get_child(_list, int32_t(_first_row_child + index));

    lv_obj_update_layout(_list);
    lv_obj_scroll_to_y(_list, lv_obj_get_y(row), LV_ANIM_OFF);
}

void ListView::refresh() {
    const auto& pages = get_pages();
    auto page = pages.is_held() ? pages.first_page : 1;

    if (_list && _row_count) {
        lv_obj_update_layout(_list);
        const auto scrolled = lv_obj_get_scroll_y(_list);

        size_t top = 0;
        while (top + 1 < _row_count) {
            const auto row = lv_obj_get_child(_list, int32_t(_first_row_child + top));
            if (lv_obj_get_y(row) + lv_obj_get_height(row) > scrolled) {
                break;
            }
            top++;
        }

        page = (_first_index + uint32_t(top)) / PAGE_SIZE + 1;
    }

    _asked_again.clear();
    for (size_t i = 0; i < pages.page_sizes.size(); i++) {
        if (pages.first_page + uint32_t(i) != page) {
            _asked_again.push_back(pages.first_page + uint32_t(i));
        }
    }

    ask(page);
}

void ListView::show_note(const char* note) {
    lv_obj_update_flag(_note, LV_OBJ_FLAG_HIDDEN, !note);
    if (note) {
        lv_label_set_text(_note, note);
    }
}

// A page held that refresh() asks for again is asked for first, unless it is no longer held. Then,
// within a screen of an end that has a page past it, that page is asked for. Neither is while a page
// is on its way already, or one came that the list is not built with yet. A page that did not come
// is asked for again by its Try again only.
void ListView::ask_near_ends() {
    const auto& pages = get_pages();
    if (pages.state == LoadState::Loading || _changed) {
        return;
    }

    while (!_asked_again.empty()) {
        const auto page = _asked_again.front();
        _asked_again.erase(_asked_again.begin());

        if (pages.holds(page)) {
            ask(page);
            return;
        }
    }

    const auto reach = lv_obj_get_height(_list);
    const auto after = pages.page_after();
    const auto before = pages.page_before();

    if (after && !pages.has_failed(after) && lv_obj_get_scroll_bottom(_list) < reach) {
        ask(after);
    } else if (before && !pages.has_failed(before) && lv_obj_get_scroll_top(_list) < reach) {
        ask(before);
    }
}

void ListView::back_clicked(lv_event_t* event) {
    auto self = (ListView*)lv_event_get_user_data(event);

    self->_shell->back();
}

// The pages change, and the next do_update() builds the list again: not here, inside the event of
// a button that goes with it.
void ListView::retry_clicked(lv_event_t* event) {
    auto self = (ListView*)lv_event_get_user_data(event);

    self->ask(self->get_pages().asked);
}

// The index of a row's item among those held. A page that came beside the rows may have moved the
// items held since the list was built, so the row's place in the whole list finds its item. No page
// is dropped but as the list is built, so the item of every row built is held.
size_t ListView::find_item(const lv_obj_t* row) {
    const auto place = _first_index + uint32_t(lv_obj_get_index(row)) - _first_row_child;

    return place - get_pages().first_index();
}

void ListView::row_clicked(lv_event_t* event) {
    auto self = (ListView*)lv_event_get_user_data(event);

    self->open_row(self->find_item(lv_event_get_current_target_obj(event)));
}

void ListView::scrolled(lv_event_t* event) {
    auto self = (ListView*)lv_event_get_user_data(event);

    self->ask_near_ends();
}
