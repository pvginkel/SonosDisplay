#pragma once

#include "Ch422g.h"
#include "MotionSensor.h"
#include "RgbPanel.h"
#include "ScreenPower.h"
#include "TfCard.h"
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
    TfCard _tf_card;
    esp_err_t _touch_err{ESP_OK};
    esp_err_t _shutdown_handler_err{ESP_OK};

public:
    Board() = default;
    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;

    // The panel, the I2C bus, the expander, touch, lv_init(), the LVGL display and input device.
    esp_err_t begin();

    // Called from the UI loop.
    void process();

    // Logs what begin() found, again, and the mark the run before left (RunMark): begin() runs
    // before esp-mdm ships the log.
    void log_state();

    lv_display_t* get_display() const { return _panel.get_display(); }
    ScreenPower& get_screen_power() { return _screen_power; }

    // The TF card, which begin() leaves alone: the task that uses it mounts it, so a card that is
    // missing or fails never stops the boot.
    TfCard& get_tf_card() { return _tf_card; }

private:
    static void shut_down();
};
