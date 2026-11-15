#pragma once

#include "Ch422g.h"
#include "MotionSensor.h"
#include "RgbPanel.h"
#include "ScreenPower.h"
#include "Touch.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "lvgl.h"

// The hardware, and LVGL's driver interface to it. Nothing above board/ talks to the hardware.
class Board {
    i2c_master_bus_handle_t _i2c_bus{};
    RgbPanel _panel;
    Ch422g _expander;
    ScreenPower _screen_power{&_expander, &_panel};
    Touch _touch{&_screen_power};
    MotionSensor _motion_sensor;

public:
    Board() = default;
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;

    // The panel, the I2C bus, the expander, touch, lv_init(), the LVGL display and input device.
    esp_err_t begin();

    // Called from the UI loop.
    void process();

    lv_display_t* get_display() const { return _panel.get_display(); }
    ScreenPower& get_screen_power() { return _screen_power; }
};
