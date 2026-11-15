#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include "UiHost.h"

class SimHost : public UiHost {
    Model _model;

public:
    SimHost();

    Model& model() { return _model; }
    const Model& get_model() const override { return _model; }
};
