#pragma once

// The backend's answers, read into the model's types. This is where what the backend sends
// enters the app, so it is checked here: an answer that is not what the API's document says is
// refused whole, and one it allows is never refused for a field the display does not show.
//
// It includes cJSON, the C++ standard library and ui/Model.h only, so sim/ builds it too and its
// scenes come from bodies as the backend sends them.

#include <cstddef>

#include "ui/Model.h"

// Reads the answer to GET /api/speakers into `list`: Loaded with the speakers, or Failed with
// what the backend said went wrong, where it said so in the error envelope. Returns what was
// wrong with the answer, for the log, or nullptr when it was a list.
const char* read_speakers(int status, const char* body, size_t length, SpeakerList& list);
