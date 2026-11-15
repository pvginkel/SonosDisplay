#pragma once

#include <atomic>

#include "esp_err.h"

// The motion sensor on GPIO 6. Its interrupt only sets a flag; poll() is called from the UI loop.
class MotionSensor {
    std::atomic<bool> _triggered{};

public:
    MotionSensor() = default;
    MotionSensor(const MotionSensor&) = delete;
    MotionSensor& operator=(const MotionSensor&) = delete;

    esp_err_t begin();

    // True when motion was seen since the last poll or is seen now, so presence that lasts keeps
    // the screen on.
    bool poll();

private:
    static void isr(void* arg);
};
