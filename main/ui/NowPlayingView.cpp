#include "NowPlayingView.h"

#include "Messages.h"
#include "Shell.h"
#include "Theme.h"
#include "lv_support.h"

// The cover is 400 px of the panel's 480, centred in its height.
static constexpr int32_t COVER_SIZE = 400;
static constexpr int32_t COVER_TOP = 40;
static constexpr int32_t COLUMN_WIDTH = 312;
static constexpr int32_t COLUMN_PADDING = 24;
static constexpr int32_t TOP_BUTTON_SIZE = 64;
static constexpr int32_t TEXT_GAP = 4;
static constexpr int32_t TITLE_LINES = 2;
static constexpr int32_t ERROR_LINES = 2;
static constexpr int32_t CONTROL_SIZE = 88;
static constexpr int32_t BUTTON_HEIGHT = 72;
static constexpr int32_t VOLUME_SIZE = 72;
static constexpr int32_t NOTICE_PADDING_HOR = 20;
static constexpr int32_t NOTICE_PADDING_VER = 12;
// The backend's words may be any length: they get two lines of the cover's width.
static constexpr int32_t NOTICE_TEXT_WIDTH = COVER_SIZE - 2 * Theme::GAP - 2 * NOTICE_PADDING_HOR;
static constexpr int32_t NOTICE_LINES = 2;
static constexpr lv_style_selector_t PRESSED =
    lv_style_selector_t(LV_PART_MAIN) | lv_style_selector_t(LV_STATE_PRESSED);

NowPlayingView::NowPlayingView(UiHost* host, Shell* shell)
    : View(host), _shell(shell), _cover(host, COVER_SIZE), _transport(host), _volume(host) {}

void NowPlayingView::do_render(lv_obj_t* parent) {
    create_cover(parent);

    auto column = lv_container_create(parent);
    lv_obj_set_size(column, COLUMN_WIDTH, LV_PCT(100));
    lv_obj_align(column, LV_ALIGN_TOP_RIGHT, -Theme::PADDING, 0);
    lv_obj_set_style_pad_ver(column, COLUMN_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_row(column, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(column, LV_FLEX_FLOW_COLUMN);

    create_buttons(column);
    create_texts(column);
    _controls = _transport.create(column, LV_PCT(100), CONTROL_SIZE, Theme::FONT_ICON_LARGE, true);
    create_retry(column);
    _volume.create(column, LV_PCT(100), VOLUME_SIZE, Theme::FONT_TITLE);

    // What came of a press before this view opened is not said by it.
    _press_notice.skip(_host->get_model());

    apply();
}

void NowPlayingView::create_cover(lv_obj_t* parent) {
    auto cover = _cover.create(parent, Theme::FONT_ICON_LARGE);
    lv_obj_set_pos(cover, Theme::PADDING, COVER_TOP);

    _notice = lv_container_create(cover);
    lv_obj_set_style_bg_color(_notice, Theme::color(Theme::SURFACE_PRESSED), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_notice, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_notice, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(_notice, NOTICE_PADDING_HOR, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(_notice, NOTICE_PADDING_VER, LV_PART_MAIN);
    lv_obj_align(_notice, LV_ALIGN_BOTTOM_MID, 0, -Theme::GAP);
    lv_obj_add_flag(_notice, LV_OBJ_FLAG_HIDDEN);

    _notice_label = lv_label_create(_notice);
    lv_label_set_long_mode(_notice_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(_notice_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(_notice_label, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(_notice_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);
}

static void create_button(lv_obj_t* parent, const char* icon, lv_event_cb_t clicked, void* user_data) {
    auto button = lv_container_create(parent);
    lv_obj_set_size(button, TOP_BUTTON_SIZE, TOP_BUTTON_SIZE);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_color(button, Theme::color(Theme::SURFACE_PRESSED), PRESSED);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(button, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_add_event_cb(button, clicked, LV_EVENT_CLICKED, user_data);

    auto icon_label = lv_label_create(button);
    lv_label_set_text(icon_label, icon);
    lv_obj_set_style_text_font(icon_label, Theme::FONT_ICON, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon_label, Theme::color(Theme::ACCENT), LV_PART_MAIN);
    lv_obj_center(icon_label);
}

void NowPlayingView::create_buttons(lv_obj_t* parent) {
    auto row = lv_container_create(parent);
    lv_obj_set_style_pad_column(row, Theme::GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);

    create_button(row, ICON_SPEAKER, speaker_clicked, this);
    create_button(row, ICON_MUSIC_BOX_MULTIPLE, library_clicked, this);
}

void NowPlayingView::create_texts(lv_obj_t* parent) {
    auto texts = lv_container_create(parent);
    lv_obj_set_width(texts, LV_PCT(100));
    lv_obj_set_flex_grow(texts, 1);
    lv_obj_set_style_pad_row(texts, TEXT_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(texts, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(texts, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    // What plays is any length: the title gets two lines, the artist and the album one each.
    // apply_texts() gives each label its height.
    _title = lv_label_create(texts);
    lv_obj_set_width(_title, LV_PCT(100));
    lv_label_set_long_mode(_title, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_title, Theme::FONT_TITLE, LV_PART_MAIN);

    _artist = lv_label_create(texts);
    lv_obj_set_width(_artist, LV_PCT(100));
    lv_label_set_long_mode(_artist, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_artist, Theme::FONT_BODY, LV_PART_MAIN);

    _detail = lv_label_create(texts);
    lv_obj_set_width(_detail, LV_PCT(100));
    lv_label_set_long_mode(_detail, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(_detail, Theme::FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(_detail, Theme::color(Theme::TEXT_SECONDARY), LV_PART_MAIN);
}

void NowPlayingView::create_retry(lv_obj_t* parent) {
    _retry = lv_button_create(parent);
    lv_obj_set_size(_retry, LV_PCT(100), BUTTON_HEIGHT);
    lv_obj_set_style_radius(_retry, Theme::RADIUS, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(_retry, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(_retry, retry_clicked, LV_EVENT_CLICKED, this);

    auto label = lv_label_create(_retry);
    lv_label_set_text(label, MSG_TRY_AGAIN);
    lv_obj_set_style_text_font(label, Theme::FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, Theme::color(Theme::BACKGROUND), LV_PART_MAIN);
    lv_obj_center(label);
}

void NowPlayingView::do_update() {
    if (_host->get_model().revision != _revision) {
        apply();
    }

    if (_press_notice.expire()) {
        apply_notice();
    }
}

// What came of a press while another view was over this one is not said by it: the bar under that
// view said it.
void NowPlayingView::do_show() {
    _press_notice.skip(_host->get_model());
    apply_notice();
}

static void set_hidden(lv_obj_t* obj, bool hidden) {
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void NowPlayingView::apply() {
    const auto& model = _host->get_model();
    const auto& playback = model.playback;
    _revision = model.revision;

    _cover.apply(playback);
    apply_texts(playback);
    _volume.apply(model.volume);

    // Without an answer the commands the speaker allows are not known: asking again is all there
    // is to do.
    const auto failed = playback.state == LoadState::Unreachable || playback.state == LoadState::Failed;
    set_hidden(_controls, failed);
    set_hidden(_retry, !failed);

    _transport.apply(playback);

    if (_press_notice.apply(model)) {
        apply_notice();
    }
}

void NowPlayingView::apply_notice() {
    const auto& text = _press_notice.get_text();

    set_hidden(_notice, text.empty());
    if (text.empty()) {
        return;
    }

    // A text of one line is as wide as it is; a longer one wraps within the cover.
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), Theme::FONT_BODY, 0, 0, NOTICE_TEXT_WIDTH, LV_TEXT_FLAG_NONE);
    const auto width = size.y > lv_font_get_line_height(Theme::FONT_BODY) ? NOTICE_TEXT_WIDTH : size.x;

    lv_obj_set_width(_notice_label, width);
    lv_label_set_text_lines(_notice_label, text.c_str(), width, NOTICE_LINES);
}

void NowPlayingView::apply_texts(const Playback& playback) {
    const char* title = "";
    const char* artist = "";
    const char* detail = "";
    auto title_color = Theme::TEXT_SECONDARY;
    int32_t detail_lines = 1;

    switch (playback.state) {
        case LoadState::Loading:
            title = MSG_PLAYBACK_LOADING;
            break;
        case LoadState::Loaded:
            if (playback.track) {
                title = playback.track->title.c_str();
                artist = playback.track->artist.c_str();
                detail = playback.track->album.c_str();
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
            // The backend's own words, which may be any length: they get two lines.
            title = MSG_BACKEND_FAILED;
            detail = playback.error.c_str();
            title_color = Theme::TEXT;
            detail_lines = ERROR_LINES;
            break;
    }

    lv_label_set_text_lines(_title, title, COLUMN_WIDTH, TITLE_LINES);
    lv_obj_set_style_text_color(_title, Theme::color(title_color), LV_PART_MAIN);

    lv_label_set_text_lines(_artist, artist, COLUMN_WIDTH, 1);
    lv_label_set_text_lines(_detail, detail, COLUMN_WIDTH, detail_lines);
}

void NowPlayingView::speaker_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);

    self->_shell->open_speaker_picker();
}

void NowPlayingView::library_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);

    self->_shell->open_library();
}

void NowPlayingView::retry_clicked(lv_event_t* event) {
    auto self = (NowPlayingView*)lv_event_get_user_data(event);

    self->_host->refresh_playback();
    self->_host->refresh_volume();
}
