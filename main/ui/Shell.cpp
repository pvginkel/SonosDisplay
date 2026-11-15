#include "Shell.h"

#include "Messages.h"
#include "NowPlayingView.h"
#include "SpeakerPickerView.h"
#include "Theme.h"
#include "log.h"
#include "lv_support.h"

LOG_TAG(Shell);

Shell::Shell(UiHost* host) : _host(host) {}

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

    _content = lv_container_create(_screen);
    lv_obj_set_size(_content, LV_PCT(100), LV_PCT(100));

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
    lv_obj_align(_marker, LV_ALIGN_TOP_RIGHT, -Theme::GAP, Theme::GAP);

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
}

void Shell::pop() {
    if (_views.size() <= 1) {
        UI_LOGW("Cannot go back from the last view");
        return;
    }

    _views.pop_back();
    _views.back()->set_visible(true);
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

void Shell::update() {
    if (_back_requested) {
        _back_requested = false;

        if (_views.size() > 1) {
            pop();
        }
    }

    if (_held && *_held != _host->get_model().needs_speaker()) {
        _held = !*_held;
        show_first_view();
    }

    if (auto current = get_current()) {
        current->update();
    }

    update_marker();
}

void Shell::update_marker() {
    const auto& model = _host->get_model();

    // Without a backend address there is no stream to wait for: the view says so instead.
    const auto show = !model.backend_url.empty() && model.stream_state != StreamState::Connected &&
                      lv_tick_elaps(model.stream_down_since) >= RECONNECTING_MARKER_DELAY_MS;

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
