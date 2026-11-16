#pragma once

#include <atomic>
#include <cstdint>

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
    // esp_timer's clock at the last vblank, in microseconds, truncated: only differences are used.
    std::atomic<uint32_t> _last_vsync_us{};
    std::atomic<bool> _stopped{};
    std::atomic<bool> _presenting{};
    lv_display_t* _display{};

public:
    RgbPanel() = default;
    RgbPanel(const RgbPanel&) = delete;
    RgbPanel& operator=(const RgbPanel&) = delete;

    esp_err_t begin();
    esp_err_t begin_lvgl();

    // Restarts the panel's transfer, which ends any drift of the picture. Returns once the panel
    // shows the current framebuffer again, up to two frames later.
    void restart();

    // Ends the presents for good, before a restart: none begins after this returns. A present
    // another task is in is waited for, up to LCD_STOP_TIMEOUT_MS, and the result says whether
    // it ended in that time. On the task that presents, it returns at once.
    bool stop();

    lv_display_t* get_display() const { return _display; }

private:
    void wait_vsync();
    void present(uint8_t* framebuffer);
    static bool on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data,
                         void* user_ctx);
    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
};
