#include "Cover.h"

#include <utility>

#include "Theme.h"
#include "lv_support.h"

Cover::~Cover() { clear(); }

lv_obj_t* Cover::create(lv_obj_t* parent, const lv_font_t* placeholder_font) {
    _tile = lv_container_create(parent);
    lv_obj_set_size(_tile, _size, _size);
    lv_obj_set_style_bg_color(_tile, Theme::color(Theme::SURFACE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(_tile, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(_tile, Theme::RADIUS, LV_PART_MAIN);

    auto placeholder = lv_label_create(_tile);
    lv_label_set_text(placeholder, ICON_MUSIC);
    lv_obj_set_style_text_font(placeholder, placeholder_font, LV_PART_MAIN);
    lv_obj_set_style_text_color(placeholder, Theme::color(Theme::TEXT_UNAVAILABLE), LV_PART_MAIN);
    lv_obj_center(placeholder);

    return _tile;
}

void Cover::apply(const Playback& playback) {
    const auto shows_track = playback.state == LoadState::Loaded && playback.track;
    const auto wanted = shows_track ? playback.track->artwork_id : std::string();
    if (wanted == _id) {
        return;
    }

    clear();

    _id = wanted;
    if (wanted.empty()) {
        return;
    }

    // The placeholder shows until the picture is there.
    _request = std::make_shared<int>(0);
    _host->request_artwork(wanted, _size, _request, [this](Artwork artwork) { show(std::move(artwork)); });
}

void Cover::show(Artwork artwork) {
    // The backend did not give the picture. The placeholder stays, and the picture is asked for
    // again at the next apply(): the tracks of an album share one.
    if (!artwork) {
        _id.clear();
        return;
    }

    _artwork = std::move(artwork);

    _image = lv_image_create(_tile);
    lv_image_set_src(_image, _artwork.get());

    // Over the placeholder, and under what else is in the tile.
    lv_obj_move_to_index(_image, 1);
}

// The widget goes before the image it draws from.
void Cover::clear() {
    _id.clear();
    _request.reset();

    if (_image) {
        lv_obj_delete(_image);
        _image = nullptr;
    }

    _artwork.reset();
}
