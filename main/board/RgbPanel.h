#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

// The esp_lcd RGB panel with two framebuffers in PSRAM, and the LVGL display that draws straight
// into them.
class RgbPanel {
    esp_lcd_panel_handle_t _panel{};
    SemaphoreHandle_t _vsync{};
    lv_display_t* _display{};

public:
    RgbPanel() = default;
    RgbPanel(const RgbPanel&) = delete;
    RgbPanel& operator=(const RgbPanel&) = delete;

    esp_err_t begin();
    esp_err_t begin_lvgl();

    // Restarts the panel's transfer, which ends any drift of the picture.
    void restart();

    lv_display_t* get_display() const { return _display; }

private:
    static bool on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data,
                         void* user_ctx);
    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
};
