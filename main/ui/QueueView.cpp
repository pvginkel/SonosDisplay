#include "QueueView.h"

#include <algorithm>
#include <utility>

#include "Messages.h"
#include "Theme.h"
#include "lv_support.h"

// The artist beside an entry's title, which takes the rest of the row.
static constexpr int32_t ARTIST_MAX_WIDTH = 240;
static constexpr int32_t CURRENT_BORDER = 3;
static constexpr int32_t CLEAR_HEIGHT = 64;
static constexpr int32_t CLEAR_PADDING = 24;
static constexpr int32_t CONFIRM_BUTTON_HEIGHT = 72;
static constexpr int32_t CONFIRM_BUTTON_PADDING = 40;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

QueueView::QueueView(UiHost* host, Shell* shell)
    : ListView(host, shell, MSG_QUEUE), _queue_changes(host->get_model().queue_changes) {
    if (host->get_model().stream_state == StreamState::Connected) {
        _stream_down_since = host->get_model().stream_down_since;
    }
}

void QueueView::do_update() {
    const auto& model = _host->get_model();

    // The events sent while the stream was away are gone, so the queue is asked for again once it
    // is back. Every loss moves stream_down_since, so one lost and back between two passes counts.
    const auto reconnected =
        model.stream_state == StreamState::Connected && model.stream_down_since != _stream_down_since;
    if (model.queue_changes != _queue_changes || reconnected) {
        _queue_changes = model.queue_changes;
        if (reconnected) {
            _stream_down_since = model.stream_down_since;
        }
        refresh();
    }

    ListView::do_update();
}

// Until a page is held, the queue is asked for at the loaded track.
void QueueView::request_page(uint32_t page) { ask_queue(page, !_entries.is_held(), false); }

void QueueView::ask_queue(uint32_t page, bool locate, bool replaces) {
    _entries.ask(page);

    _host->request_queue(page, locate, get_scope(), [this, locate, replaces](QueuePage answered) {
        // A page past the queue's end, which a change made shorter, holds none of it: the queue is
        // asked for again at the loaded track, or, where that answer is past the end too, by its
        // last page, and either takes the place of every page held.
        if (answered.state == LoadState::Loaded && answered.items.empty() && answered.total) {
            if (locate) {
                ask_queue((answered.total - 1) / PAGE_SIZE + 1, false, true);
            } else {
                ask_queue(1, true, true);
            }
            return;
        }

        if (replaces) {
            _entries.clear();
        }
        if (answered.state == LoadState::Loaded) {
            _current = answered.current_position;
            _located = _located || locate;
        }

        _entries.answered(std::move(answered));
        list_changed();
    });
}

const char* QueueView::get_loading_message() const { return MSG_QUEUE_LOADING; }

const char* QueueView::get_empty_message() const { return MSG_QUEUE_EMPTY; }

void QueueView::build_row(lv_obj_t* row, size_t index) {
    const auto& entry = _entries.items[index];
    const auto current = _current && *_current == _entries.first_index() + index + 1;
    const auto color = Theme::color(current ? Theme::ACCENT : Theme::TEXT_SECONDARY);

    auto title = create_row_title(row, entry.title);

    // As wide as it is, up to its most: LVGL cuts a text to the size its label has when it is set,
    // so a label sized by its content keeps the dots alone.
    lv_point_t size;
    lv_text_get_size(&size, entry.artist.c_str(), Theme::FONT_BODY_LARGE, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

    auto artist = lv_label_create(row);
    lv_obj_set_size(artist, std::min(size.x, ARTIST_MAX_WIDTH), lv_font_get_line_height(Theme::FONT_BODY_LARGE));
    lv_label_set_long_mode(artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(artist, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(artist, color, LV_PART_MAIN);
    lv_label_set_text(artist, entry.artist.c_str());

    if (current) {
        lv_obj_set_style_text_color(title, color, LV_PART_MAIN);
        lv_obj_set_style_border_color(row, color, LV_PART_MAIN);
        lv_obj_set_style_border_width(row, CURRENT_BORDER, LV_PART_MAIN);
    }
}

void QueueView::do_render_heading(lv_obj_t* heading) {
    _clear = lv_container_create(heading);
    lv_obj_set_height(_clear, CLEAR_HEIGHT);
    lv_obj_set_style_pad_hor(_clear, CLEAR_PADDING, LV_PART_MAIN);
    lv_obj_set_flex_flow(_clear, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_clear, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(_clear, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(_clear, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_clear, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(_clear, clear_clicked, LV_EVENT_CLICKED, this);

    _clear_label = lv_label_create(_clear);
    lv_label_set_text(_clear_label, MSG_CLEAR);
    lv_obj_set_style_text_font(_clear_label, Theme::FONT_TITLE, LV_PART_MAIN);
}

static lv_obj_t* create_confirm_button(lv_obj_t* parent, const char* text, uint32_t color, uint32_t pressed,
                                       uint32_t text_color, lv_event_cb_t clicked, void* user_data) {
    auto button = lv_container_create(parent);
    lv_obj_set_height(button, CONFIRM_BUTTON_HEIGHT);
    lv_obj_set_style_pad_hor(button, CONFIRM_BUTTON_PADDING, LV_PART_MAIN);
    lv_obj_set_flex_flow(button, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(button, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, Theme::color(color), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, Theme::color(pressed), PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);

    auto label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, Theme::FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(text_color), LV_PART_MAIN);

    return button;
}

void QueueView::do_render_beside(lv_obj_t*, lv_obj_t* area) {
    // Over the list, which it hides while it shows. It takes the presses beside its buttons, which
    // would scroll the list under it.
    _confirm = lv_container_create(area);
    lv_obj_set_size(_confirm, LV_PCT(100), LV_PCT(100));
    lv_obj_add_flag(_confirm, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(_confirm, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_confirm, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_row(_confirm, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(_confirm, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_confirm, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto question = lv_label_create(_confirm);
    lv_label_set_text(question, MSG_CLEAR_QUEUE);
    lv_obj_set_style_text_font(question, Theme::FONT_HEADLINE, LV_PART_MAIN);

    auto stops = lv_label_create(_confirm);
    lv_label_set_text(stops, MSG_CLEAR_QUEUE_STOPS);
    lv_obj_set_style_text_font(stops, Theme::FONT_BODY_LARGE, LV_PART_MAIN);
    lv_obj_set_style_text_color(stops, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    auto buttons = lv_container_create(_confirm);
    lv_obj_set_style_pad_top(buttons, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(buttons, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);

    create_confirm_button(buttons, MSG_CANCEL, Theme::SURFACE, Theme::SURFACE_PRESSED, Theme::TEXT, cancel_clicked,
                          this);
    create_confirm_button(buttons, MSG_CLEAR, Theme::ACCENT, Theme::ACCENT_PRESSED, Theme::BACKGROUND, confirm_clicked,
                          this);

    show_confirm(false);
}

void QueueView::do_list_built() {
    // A queue known to be empty has nothing to clear.
    const auto empty = _entries.is_held() && !_entries.total;
    lv_obj_update_flag(_clear, LV_OBJ_FLAG_HIDDEN, empty);
    if (empty) {
        show_confirm(false);
    }

    show_note(_entries.is_held() && _entries.total && !_current ? MSG_QUEUE_ELSEWHERE : nullptr);

    if (!_located || !_entries.is_held()) {
        return;
    }

    _located = false;

    const auto first = _entries.first_index();
    if (_current && *_current > first && *_current - first <= count_rows()) {
        scroll_to_row(*_current - first - 1);
    }
}

void QueueView::show_confirm(bool shown) {
    lv_obj_update_flag(_confirm, LV_OBJ_FLAG_HIDDEN, !shown);

    lv_obj_set_style_bg_color(_clear, Theme::color(shown ? Theme::ACCENT : Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_clear, Theme::color(shown ? Theme::ACCENT_PRESSED : Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_text_color(_clear_label, Theme::color(shown ? Theme::BACKGROUND : Theme::ACCENT), LV_PART_MAIN);
}

void QueueView::clear_clicked(lv_event_t* event) {
    auto self = (QueueView*)lv_event_get_user_data(event);

    self->show_confirm(lv_obj_has_flag(self->_confirm, LV_OBJ_FLAG_HIDDEN));
}

void QueueView::confirm_clicked(lv_event_t* event) {
    auto self = (QueueView*)lv_event_get_user_data(event);

    self->show_confirm(false);
    self->_host->clear_queue();
}

void QueueView::cancel_clicked(lv_event_t* event) {
    auto self = (QueueView*)lv_event_get_user_data(event);

    self->show_confirm(false);
}
