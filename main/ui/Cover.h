#pragma once

// A picture in a square tile, a track's cover or a favourite's: the picture, or a drawn placeholder
// while there is none to show. The picture is asked of the host at the tile's size, and held for
// exactly as long as the tile draws it.

#include <cstdint>
#include <memory>
#include <string>

#include "UiHost.h"
#include "lvgl.h"

class Cover {
    UiHost* _host;
    int32_t _size;
    // The id of the picture that is shown or asked for; empty while the track has none, or the
    // backend did not give it.
    std::string _id;
    // The scope of the request for it, which ends when another picture is wanted.
    std::shared_ptr<void> _request;
    // The picture, held while _image draws from it.
    Artwork _artwork;
    lv_obj_t* _tile{};
    lv_obj_t* _image{};

public:
    Cover(UiHost* host, int32_t size) : _host(host), _size(size) {}
    Cover(const Cover&) = delete;
    Cover& operator=(const Cover&) = delete;

    // Must run before the tile is deleted: the picture's widget goes here.
    ~Cover();

    // Builds the tile in `parent`, with the placeholder in it, and returns it. The picture goes
    // over the placeholder, and under whatever else the tile is given.
    lv_obj_t* create(lv_obj_t* parent, const lv_font_t* placeholder_font);

    // Shows the cover of the track the playback has loaded. Where it has none, the backend gave
    // no playback or the track has no picture, the placeholder shows. A picture the backend did
    // not give is asked for again at the next call.
    void apply(const Playback& playback);

    // Shows the picture with this id, as apply(const Playback&) does; with an empty one, the
    // placeholder.
    void apply(const std::string& artwork_id);

    // Shows the placeholder and holds no picture. A picture still on its way is no longer waited
    // for.
    void clear();

private:
    void show(Artwork artwork);
};
