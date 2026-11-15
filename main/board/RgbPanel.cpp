#include "RgbPanel.h"

#include "BoardConfig.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_timer.h"
#include "support.h"

LOG_TAG(RgbPanel);

esp_err_t RgbPanel::begin() {
    ESP_LOGI(TAG, "Installing the RGB panel driver");

    _vsync = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(_vsync, ESP_ERR_NO_MEM, TAG, "Cannot create the vsync semaphore");

    esp_lcd_rgb_panel_config_t config{};

    config.clk_src = LCD_CLK_SRC_DEFAULT;
    config.timings.pclk_hz = LCD_PCLK_HZ;
    config.timings.h_res = LCD_H_RES;
    config.timings.v_res = LCD_V_RES;
    config.timings.hsync_pulse_width = LCD_HSYNC_PULSE_WIDTH;
    config.timings.hsync_back_porch = LCD_HSYNC_BACK_PORCH;
    config.timings.hsync_front_porch = LCD_HSYNC_FRONT_PORCH;
    config.timings.vsync_pulse_width = LCD_VSYNC_PULSE_WIDTH;
    config.timings.vsync_back_porch = LCD_VSYNC_BACK_PORCH;
    config.timings.vsync_front_porch = LCD_VSYNC_FRONT_PORCH;
    config.timings.flags.pclk_active_neg = 0;
    config.data_width = LCD_DATA_WIDTH;
    config.bits_per_pixel = LCD_BITS_PER_PIXEL;
    config.num_fbs = LCD_FRAME_BUFFERS;
    config.bounce_buffer_size_px = 0;
    config.dma_burst_size = LCD_DMA_BURST_SIZE;
    config.hsync_gpio_num = LCD_GPIO_HSYNC;
    config.vsync_gpio_num = LCD_GPIO_VSYNC;
    config.de_gpio_num = LCD_GPIO_DE;
    config.pclk_gpio_num = LCD_GPIO_PCLK;
    config.disp_gpio_num = LCD_GPIO_DISP;
    for (size_t i = 0; i < LCD_DATA_WIDTH; i++) {
        config.data_gpio_nums[i] = LCD_GPIO_DATA[i];
    }
    config.flags.fb_in_psram = 1;

    ESP_ERROR_RETURN(esp_lcd_new_rgb_panel(&config, &_panel));

    esp_lcd_rgb_panel_event_callbacks_t callbacks{};
    callbacks.on_vsync = on_vsync;

    ESP_ERROR_RETURN(esp_lcd_rgb_panel_register_event_callbacks(_panel, &callbacks, this));
    ESP_ERROR_RETURN(esp_lcd_panel_reset(_panel));
    ESP_ERROR_RETURN(esp_lcd_panel_init(_panel));

    return ESP_OK;
}

esp_err_t RgbPanel::begin_lvgl() {
    void* fb0 = nullptr;
    void* fb1 = nullptr;
    ESP_ERROR_RETURN(esp_lcd_rgb_panel_get_frame_buffer(_panel, 2, &fb0, &fb1));

    _display = lv_display_create(LCD_H_RES, LCD_V_RES);
    ESP_RETURN_ON_FALSE(_display, ESP_ERR_NO_MEM, TAG, "Cannot create the LVGL display");

    // Direct mode: LVGL draws only what changed, into the panel's own framebuffers, and copies
    // those areas into the other buffer itself. The panel starts on fb0, so LVGL's first frame
    // goes into fb1, out of sight.
    lv_display_set_user_data(_display, this);
    lv_display_set_buffers(_display, fb1, fb0, LCD_H_RES * LCD_V_RES * (LCD_BITS_PER_PIXEL / 8),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(_display, flush);

    return ESP_OK;
}

void RgbPanel::restart() {
    // The driver restarts on the next vblank, and on the S3 always from fb0: its restart link is
    // mounted there whatever buffer is current, and the transfer follows the current buffer
    // only from the frame after. That frame from fb0 may be stale, or being drawn into, so this
    // returns once it is past. The caller lights the backlight after it.
    xSemaphoreTake(_vsync, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_rgb_panel_restart(_panel));
    wait_vsync();
    wait_vsync();
}

void RgbPanel::wait_vsync() { xSemaphoreTake(_vsync, pdMS_TO_TICKS(LCD_VSYNC_TIMEOUT_MS)); }

void RgbPanel::present(uint8_t* framebuffer) {
    // Late in a frame the DMA has already read the link to the next one, so a swap made then
    // would show the old buffer once more while LVGL draws into it. The swap waits for the
    // next frame to start instead.
    if (uint32_t(esp_timer_get_time()) - _last_vsync_us.load() > LCD_SWAP_DEADLINE_US) {
        xSemaphoreTake(_vsync, 0);
        wait_vsync();
    }

    // framebuffer is one of the panel's own, so the driver writes the cache back and points the
    // end of every frame at it, rather than copying.
    esp_lcd_panel_draw_bitmap(_panel, 0, 0, LCD_H_RES, LCD_V_RES, framebuffer);

    // The next vblank comes after the DMA moved to the new buffer: from then on LVGL may draw
    // into the old one.
    xSemaphoreTake(_vsync, 0);
    wait_vsync();
}

bool IRAM_ATTR RgbPanel::on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void* user_ctx) {
    auto self = (RgbPanel*)user_ctx;
    BaseType_t higher_priority_task_woken = pdFALSE;

    self->_last_vsync_us.store(uint32_t(esp_timer_get_time()));
    xSemaphoreGiveFromISR(self->_vsync, &higher_priority_task_woken);

    return higher_priority_task_woken == pdTRUE;
}

void RgbPanel::flush(lv_display_t* display, const lv_area_t*, uint8_t* px_map) {
    auto self = (RgbPanel*)lv_display_get_user_data(display);

    if (lv_display_flush_is_last(display)) {
        self->present(px_map);
    }

    lv_display_flush_ready(display);
}
