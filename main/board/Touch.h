#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lcd_touch.h"
#include "lvgl.h"

class ScreenPower;

// The GT911 touch controller and the LVGL input device. Polled from LVGL's input timer on the UI
// task.
class Touch {
    ScreenPower* _screen_power;
    esp_lcd_panel_io_handle_t _io{};
    esp_lcd_touch_handle_t _touch{};
    lv_indev_t* _indev{};
    uint16_t _address{};

public:
    explicit Touch(ScreenPower* screen_power) : _screen_power(screen_power) {}
    Touch(const Touch&) = delete;
    Touch& operator=(const Touch&) = delete;

    esp_err_t begin(i2c_master_bus_handle_t bus);
    void begin_lvgl(lv_display_t* display);

    // The address the GT911 answered on, or 0 when it did not.
    uint16_t get_address() const { return _address; }

private:
    static esp_err_t find_address(i2c_master_bus_handle_t bus, uint16_t& address);
    static void read(lv_indev_t* indev, lv_indev_data_t* data);
    bool read_point(lv_point_t& point);
};
