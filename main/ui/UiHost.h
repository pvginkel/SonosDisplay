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
    // display. Nothing changes when it is the chosen one already.
    virtual void choose_speaker(const std::string& id) = 0;

    // Asks the backend for the speakers again.
    virtual void refresh_speakers() = 0;

    // Asks the backend again for what the chosen speaker plays.
    virtual void refresh_playback() = 0;

    // Asks the backend again for the volume of the chosen speaker's group.
    virtual void refresh_volume() = 0;

    // Turns the volume of the chosen speaker's group down, by a negative step, or up. A view
    // sends only a step the volume allows.
    virtual void change_volume(int step) = 0;

    // Sends a transport command to the chosen speaker. A view sends only a command the playback
    // allows.
    virtual void send_command(Command command) = 0;
};
