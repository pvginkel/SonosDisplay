#include "NowPlayingBar.h"

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

static constexpr int32_t BORDER = 2;
static constexpr int32_t COVER_SIZE = 80;
static constexpr int32_t COVER_INSET = 8;
static constexpr int32_t CONTROL_SIZE = 72;
static constexpr int32_t CONTROL_GAP = 12;
static constexpr int32_t TRANSPORT_WIDTH = 2 * CONTROL_SIZE + CONTROL_GAP;
static constexpr int32_t VOLUME_WIDTH = 240;
// Between next and the volume's down, on top of the bar's own gap.
static constexpr int32_t VOLUME_MARGIN = 8;

NowPlayingBar::NowPlayingBar(UiHost* host, Shell* shell)
    : _host(host), _shell(shell), _cover(host, COVER_SIZE), _transport(host), _volume(host) {}

NowPlayingBar::~NowPlayingBar() {
    // The cover's widget goes before its image, and both before the bar.
    _cover.clear();

    if (_bar) {
        lv_obj_delete(_bar);
    }
}

void NowPlayingBar::create(lv_obj_t* parent) {
    _bar = lv_container_create(parent);
    lv_obj_set_size(_bar, LV_PCT(100), HEIGHT);
    lv_obj_align(_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_side(_bar, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_width(_bar, BORDER, LV_PART_MAIN);
    lv_obj_set_style_border_color(_bar, Theme::color(Theme::SURFACE_PRESSED), LV_PART_MAIN);
    lv_obj_set_style_pad_left(_bar, COVER_INSET, LV_PART_MAIN);
    lv_obj_set_style_pad_right(_bar, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_style_pad_column(_bar, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(_bar, LV_OBJ_FLAG_HIDDEN);

    auto cover = _cover.create(_bar, Theme::FONT_ICON);
    lv_obj_add_flag(cover, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(cover, opened, LV_EVENT_CLICKED, this);

    // The bar's full height, so the texts take a touch above and below their lines too.
    _texts = lv_container_create(_bar);
    lv_obj_set_height(_texts, LV_PCT(100));
    lv_obj_set_flex_grow(_texts, 1);
    lv_obj_set_flex_flow(_texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(_texts, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_texts, opened, LV_EVENT_CLICKED, this);

    // What plays is any length: apply_texts() gives each label its lines.
    _title = lv_label_create(_texts);
    lv_obj_set_width(_title, LV_PCT(100));
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_title, Theme::FONT_BODY, LV_PART_MAIN);

    _artist = lv_label_create(_texts);
    lv_obj_set_width(_artist, LV_PCT(100));
    lv_label_set_long_mode(_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_artist, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(_artist, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);

    _transport.create(_bar, TRANSPORT_WIDTH, CONTROL_SIZE, Theme::FONT_ICON, false);

    auto volume = _volume.create(_bar, VOLUME_WIDTH, CONTROL_SIZE, Theme::FONT_BODY);
    lv_obj_set_style_margin_left(volume, VOLUME_MARGIN, LV_PART_MAIN);
}

void NowPlayingBar::set_shown(bool shown) {
    if (shown == _shown) {
        return;
    }

    _shown = shown;
    lv_obj_update_flag(_bar, LV_OBJ_FLAG_HIDDEN, !shown);

    if (shown) {
        // What came of a press while the bar was hidden is not said by it.
        _notice.skip(_host->get_model());

        apply();
    } else {
        _cover.clear();
    }
}

void NowPlayingBar::update() {
    if (!_shown) {
        return;
    }

    if (_host->get_model().revision != _revision) {
        apply();
    }

    if (_notice.expire()) {
        apply_texts(_host->get_model().playback);
    }
}

void NowPlayingBar::apply() {
    const auto& model = _host->get_model();
    _revision = model.revision;

    _notice.apply(model);

    _cover.apply(model.playback);
    apply_texts(model.playback);
    _transport.apply(model.playback);
    _volume.apply(model.volume);
}

void NowPlayingBar::apply_texts(const Playback& playback) {
    const char* title = "";
    const char* artist = "";
    auto title_color = Theme::TEXT_SECONDARY;

    if (!_notice.get_text().empty()) {
        title = _notice.get_text().c_str();
        title_color = Theme::ACCENT;
    } else {
        switch (playback.state) {
            case LoadState::Loading:
                title = MSG_PLAYBACK_LOADING;
                break;
            case LoadState::Loaded:
                if (playback.track) {
                    title = playback.track->title.c_str();
                    artist = playback.track->artist.c_str();
                    title_color = Theme::TEXT;
                } else {
                    title = MSG_NOTHING_PLAYING;
                }
                break;
            case LoadState::Unreachable:
                title = MSG_BACKEND_UNREACHABLE;
                title_color = Theme::TEXT;
                break;
            case LoadState::Failed:
                title = MSG_BACKEND_FAILED;
                title_color = Theme::TEXT;
                break;
        }
    }

    // The texts have the width the cover and the controls leave them.
    lv_obj_update_layout(_texts);
    const auto width = lv_obj_get_content_width(_texts);

    // The bar has two lines: a text with none under it gets both.
    lv_label_set_text_lines(_title, title, width, *artist ? 1 : 2);
    lv_obj_set_style_text_color(_title, Theme::color(title_color), LV_PART_MAIN);

    lv_label_set_text_lines(_artist, artist, width, 1);
}

void NowPlayingBar::opened(lv_event_t* event) {
    auto self = (NowPlayingBar*)lv_event_get_user_data(event);

    self->_shell->open_now_playing();
}
