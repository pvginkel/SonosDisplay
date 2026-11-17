#pragma once

// The bodies of the requests the firmware sends, written as the API's document asks for them.
//
// It includes cJSON, the C++ standard library and ui/Model.h only, so sim/ builds it too and its
// scenes check what the device sends.

#include <optional>
#include <string>

#include "ui/Model.h"

// The body of POST /api/speakers/{speaker_id}/group/volume for `change`: its step, and its unmute
// where it carries one. A step, not a level: the speaker takes it from the volume it has, which
// may not be the one on screen.
std::string write_volume_change(const VolumeChange& change);

// The body of POST /api/speakers/{speaker_id}/queue for `request`. An artist played or added
// names its albums' order, newest first by release date: what the speaker plays for an artist
// from the Sonos app. An album played from a track names that track. Nothing when the heap cannot
// hold it.
std::optional<std::string> write_queue_request(const QueueRequest& request);

// The body of POST /api/favourites, which marks the item of `request`. Nothing when the heap
// cannot hold it.
std::optional<std::string> write_favourite_mark(const FavouriteRequest& request);
