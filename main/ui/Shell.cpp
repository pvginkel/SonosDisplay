#include "Shell.h"

#include <algorithm>

#include "ArtistsView.h"
#include "Messages.h"
#include "NowPlayingView.h"
#include "SpeakerPickerView.h"
#include "Theme.h"
#include "log.h"
#include "lv_support.h"

LOG_TAG(Shell);

Shell::Shell(UiHost* host) : _host(host), _bar(host, this) {}

Shell::~Shell() {
    _views.clear();

    if (_marker) {
        lv_obj_delete(_marker);
    }
    if (_content) {
        lv_obj_delete(_content);
    }
}

void Shell::begin(lv_display_t* display) {
    _screen = lv_display_get_screen_active(display);
    lv_obj_remove_flag(_screen, LV_OBJ_FLAG_SCROLLABLE);

    _height = lv_display_get_vertical_resolution(display);

    _content = lv_container_create(_screen);
    lv_obj_set_size(_content, LV_PCT(100), _height);

    _bar.create(_screen);

    create_marker();
}

void Shell::create_marker() {
    _marker = lv_container_create(_screen);
    lv_obj_set_style_bg_color(_marker, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_marker, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_marker, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_marker, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_marker, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(_marker, 12, LV_PART_MAIN);
    lv_obj_set_flex_flow(_marker, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_marker, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    auto icon = lv_label_create(_marker);
    lv_label_set_text(icon, ICON_CLOUD_OFF);
    lv_obj_set_style_text_font(icon, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, Theme::color(Theme::ACCENT), LV_PART_MAIN);

    auto label = lv_label_create(_marker);
    lv_label_set_text(label, MSG_RECONNECTING);
    lv_obj_set_style_text_font(label, Theme::FONT_BODY, LV_PART_MAIN);

    lv_obj_add_flag(_marker, LV_OBJ_FLAG_HIDDEN);
}

void Shell::push(std::unique_ptr<View> view) {
    if (auto current = get_current()) {
        current->set_visible(false);
    }

    view->render(_content);
    _views.push_back(std::move(view));

    layout();
}

void Shell::pop() {
    if (_views.size() <= 1) {
        UI_LOGW("Cannot go back from the last view");
        return;
    }

    _views.pop_back();
    _views.back()->set_visible(true);

    layout();
}

void Shell::replace(std::unique_ptr<View> view) {
    _views.clear();

    push(std::move(view));
}

void Shell::open() {
    _held = _host->get_model().needs_speaker();
    show_first_view();
}

void Shell::show_first_view() {
    if (*_held) {
        replace(std::make_unique<SpeakerPickerView>(_host, this));
    } else {
        replace(std::make_unique<NowPlayingView>(_host, this));
    }
}

void Shell::open_speaker_picker() { push(std::make_unique<SpeakerPickerView>(_host, this)); }

void Shell::open_library() { push(std::make_unique<ArtistsView>(_host, this)); }

void Shell::show_now_playing() {
    const auto found =
        std::find_if(_views.rbegin(), _views.rend(), [](const auto& view) { return view->is_now_playing(); });

    if (found == _views.rend()) {
        push(std::make_unique<NowPlayingView>(_host, this));
        return;
    }
    if (found == _views.rbegin()) {
        return;
    }

    _views.erase(found.base(), _views.end());
    _views.back()->set_visible(true);

    layout();
}

void Shell::update() {
    if (_back_requested) {
        _back_requested = false;

        if (_views.size() > 1) {
            pop();
        }
    }

    if (_now_playing_requested) {
        _now_playing_requested = false;

        show_now_playing();
    }

    if (_held && *_held != _host->get_model().needs_speaker()) {
        _held = !*_held;
        show_first_view();
    }

    layout();

    // Before the view, which fits its heading to where the marker is.
    update_marker();

    if (auto current = get_current()) {
        current->update();
    }

    _bar.update();
}

// Runs whenever the top view or the hold on the picker may have changed, so no frame is drawn
// with the bar over a view's content.
void Shell::layout() {
    const auto current = get_current();
    const auto bar = current && current->has_bar() && !_host->get_model().needs_speaker();
    if (bar == _bar.is_shown()) {
        return;
    }

    _bar.set_shown(bar);
    lv_obj_set_height(_content, _height - (bar ? NowPlayingBar::HEIGHT : 0));
}

int32_t Shell::get_marker_reach() const {
    if (lv_obj_has_flag(_marker, LV_OBJ_FLAG_HIDDEN) || _marker_align != LV_ALIGN_TOP_RIGHT) {
        return 0;
    }

    lv_obj_update_layout(_marker);

    return Theme::GAP + lv_obj_get_width(_marker);
}

void Shell::update_marker() {
    const auto& model = _host->get_model();

    // Without a backend address there is no stream to wait for: the view says so instead.
    const auto show = !model.backend_url.empty() && model.stream_state != StreamState::Connected &&
                      lv_tick_elaps(model.stream_down_since) >= RECONNECTING_MARKER_DELAY_MS;

    // In the corner the top view leaves free, which may change while it shows.
    if (const auto current = get_current(); show && current && current->get_marker_align() != _marker_align) {
        _marker_align = current->get_marker_align();
        lv_obj_align(_marker, _marker_align, _marker_align == LV_ALIGN_TOP_LEFT ? Theme::GAP : -Theme::GAP, Theme::GAP);
    }

    if (show == !lv_obj_has_flag(_marker, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }

    if (show) {
        lv_obj_remove_flag(_marker, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(_marker);
    } else {
        lv_obj_add_flag(_marker, LV_OBJ_FLAG_HIDDEN);
    }
}
