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

    // How the scan-out ended (halt()).
    enum class Halt : uint8_t {
        // The data cache's preload was done once the refills had ended.
        PreloadDone,
        // It was not, and a preload begun afresh finished.
        PreloadRestarted,
        // It was not, and the one begun afresh did not finish either.
        PreloadHung,
    };

    // The data cache's registers that esp_restart_noos() waits on, as halt() left them.
    struct CacheState {
        uint32_t preload;
        uint32_t autoload;
        uint32_t sync;
        uint32_t state;
    };

    // Ends the scan-out for good, after stop(), so that the data cache's preload is done when
    // esp_restart_noos() disables the cache: it waits for that with core 0 already stalled, and
    // no stage of its watchdog but the last restarts a stalled core. The refill starts a preload
    // of the framebuffer's next lines each time it runs, and nothing ends one.
    Halt halt(CacheState& cache);

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
