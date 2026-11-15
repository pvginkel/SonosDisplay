#pragma once

// What a view may ask of the firmware. Application implements it on the device; sim/ implements
// it on fixtures.

#include <string>

#include "Model.h"

class UiHost {
public:
    UiHost() = default;
    UiHost(const UiHost&) = delete;
    UiHost& operator=(const UiHost&) = delete;
    virtual ~UiHost() = default;

    virtual const Model& get_model() const = 0;

    // Makes the speaker with this id the one the display controls, and stores the choice on the
    // display.
    virtual void choose_speaker(const std::string& id) = 0;

    // Asks the backend for the speakers again.
    virtual void refresh_speakers() = 0;
};
