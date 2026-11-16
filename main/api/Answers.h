#pragma once

// The backend's answers, read into the model's types. This is where what the backend sends
// enters the app, so it is checked here: an answer that is not what the API's document says is
// refused whole, and one it allows is never refused for a field the display does not show. A text
// it sends is read without the characters the fonts do not draw (can_draw()).
//
// It includes cJSON, the C++ standard library and ui/Model.h only, so sim/ builds it too and its
// scenes come from bodies as the backend sends them.

#include <cstddef>
#include <string>

#include "cJSON.h"
#include "ui/Model.h"

// Each reader returns what was wrong with the answer, for the log, or nullptr when it was what
// the API's document says.

// Reads the answer to GET /api/speakers into `list`: Loaded with the speakers and the members of
// each one's group, or Failed with what the backend said went wrong, where it said so in the
// error envelope.
const char* read_speakers(int status, const char* body, size_t length, SpeakerList& list);

// Reads the answer to GET /api/speakers/{speaker_id}/playback into `playback`: Loaded with what
// plays and the commands the speaker allows, or Failed with what the backend said went wrong. A
// failure leaves the last playback answered in place.
const char* read_playback(int status, const char* body, size_t length, Playback& playback);

// Reads the answer to a transport command or to a change of a group, which is a 204 when the
// backend took it. `error` is what the backend said went wrong, where it said so.
const char* read_command(int status, const char* body, size_t length, std::string& error);

// Reads the answer to GET /api/speakers/{speaker_id} into `volume`: Loaded with the level and the
// mute of the speaker's group, or Failed, which leaves the last level answered in place. `error`
// is what the backend said went wrong, where it said so.
const char* read_speaker_volume(int status, const char* body, size_t length, Volume& volume, std::string& error);

// Reads the answer to a change of the group's volume, which is a 200 with the level and the mute
// after the change: `volume` is Loaded with them. Any other answer leaves `volume` as it is.
const char* read_volume_change(int status, const char* body, size_t length, Volume& volume, std::string& error);

// Reads the payload of an event that names a speaker into `speaker_id`.
const char* read_speaker_event(const cJSON* payload, std::string& speaker_id);
