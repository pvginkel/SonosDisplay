#pragma once

// The bodies of the requests the firmware sends, written as the API's document asks for them.
//
// It includes the C++ standard library and ui/Model.h only, so sim/ builds it too and its scenes
// check what the device sends.

#include <string>

#include "ui/Model.h"

// The body of POST /api/speakers/{speaker_id}/group/volume for `change`: its step, and its unmute
// where it carries one. A step, not a level: the speaker takes it from the volume it has, which
// may not be the one on screen.
std::string write_volume_change(const VolumeChange& change);
