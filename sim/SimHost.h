#pragma once

// UiHost on fixtures: what the device gets from IoT Support and the backend, made up.

#include <optional>
#include <string>

#include "UiHost.h"
#include "lvgl.h"

class SimHost : public UiHost {
    Model _model;
    std::optional<lv_point_t> _tap;

public:
    SimHost();

    Model& model() { return _model; }
    const Model& get_model() const override { return _model; }
    void choose_speaker(const std::string& id) override;
    void refresh_speakers() override;

    // The backend's answer to GET /api/speakers, read as the device reads it.
    void answer_speakers(int status, const char* body);

    // Touches the screen at a point as the scene starts, and lifts 100 ms later.
    void tap(int32_t x, int32_t y) { _tap = lv_point_t{x, y}; }
    const std::optional<lv_point_t>& get_tap() const { return _tap; }
};
