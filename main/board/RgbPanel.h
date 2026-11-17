#pragma once

#include <atomic>
#include <cstdint>

#include "esp_err.h"
#include "esp_lcd_panel_rgb.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"

// The esp_lcd RGB panel with two framebuffers in PSRAM, and the LVGL display that draws straight
// into them. The DMA reads two bounce buffers in internal RAM, which the panel's interrupts refill
// from the current framebuffer.
class RgbPanel {
    esp_lcd_panel_handle_t _panel{};
    SemaphoreHandle_t _vsync{};
    SemaphoreHandle_t _taken{};
    std::atomic<bool> _stopped{};
    std::atomic<bool> _presenting{};
    lv_display_t* _display{};

public:
    // The core of the panel's interrupts, the refill's among them: the UI task's, which presents
    // and restarts. Not core 0, WiFi's and the backend's.
    static constexpr BaseType_t INTERRUPT_CORE = 1;

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
    esp_err_t install();
    void present(uint8_t* framebuffer);
    static bool on_vsync(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data,
                         void* user_ctx);
    static bool on_framebuffer_taken(esp_lcd_panel_handle_t panel, const esp_lcd_rgb_panel_event_data_t* event_data,
                                     void* user_ctx);
    static void flush(lv_display_t* display, const lv_area_t* area, uint8_t* px_map);
};
