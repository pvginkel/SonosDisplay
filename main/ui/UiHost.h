#pragma once

// What a view may ask of the firmware. Application implements it on the device; sim/ implements
// it on fixtures. Milestone 1 adds commands to the backend and artwork.

#include "Model.h"

class UiHost {
public:
    UiHost() = default;
    UiHost(const UiHost&) = delete;
    UiHost& operator=(const UiHost&) = delete;
    virtual ~UiHost() = default;

    virtual const Model& get_model() const = 0;
};
