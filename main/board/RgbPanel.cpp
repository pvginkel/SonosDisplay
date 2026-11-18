#include "RgbPanel.h"

#include "BoardConfig.h"
#include "esp32s3/rom/cache.h"
#include "esp_attr.h"
#include "esp_lcd_panel_ops.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "hal/lcd_ll.h"
#include "sdkconfig.h"
#include "soc/extmem_reg.h"
#include "support.h"

LOG_TAG(RgbPanel);

static constexpr uint32_t INSTALL_TASK_STACK_SIZE = 4096;
static constexpr uint32_t PRELOAD_POLL_US = 10;

static bool wait_for_preload() {
    const auto deadline = esp_timer_get_time() + LCD_PRELOAD_TIMEOUT_US;
    while (!Cache_DCache_Preload_Done()) {
        if (esp_timer_get_time() >= deadline) {
            return false;
        }

        esp_rom_delay_us(PRELOAD_POLL_US);
    }

    return true;
}

esp_err_t RgbPanel::begin() {
    ESP_LOGI(TAG, "Installing the RGB panel driver on core %d", int(INTERRUPT_CORE));

    _vsync = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(_vsync, ESP_ERR_NO_MEM, TAG, "Cannot create the vsync semaphore");

    _taken = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(_taken, ESP_ERR_NO_MEM, TAG, "Cannot create the framebuffer semaphore");

    // The driver allocates the panel's interrupts on the core that installs it.
    struct Job {
        RgbPanel* panel;
        TaskHandle_t waiter;
        esp_err_t result;
    } job{this, xTaskGetCurrentTaskHandle(), ESP_FAIL};

    const auto created = xTaskCreatePinnedToCore(
        [](void* arg) {
            auto job = (Job*)arg;
            job->result = job->panel->install();
            xTaskNotifyGive(job->waiter);
            vTaskDelete(nullptr);
        },
        "lcd_install", INSTALL_TASK_STACK_SIZE, &job, uxTaskPriorityGet(nullptr), nullptr, INTERRUPT_CORE);
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "Cannot start the panel's install task");

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    return job.result;
}

esp_err_t RgbPanel::install() {
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
    config.bounce_buffer_size_px = LCD_BOUNCE_BUFFER_PX;
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
    callbacks.on_frame_buf_complete = on_framebuffer_taken;

    // With CONFIG_LCD_RGB_ISR_IRAM_SAFE the driver refuses callbacks outside IRAM and a context
    // outside internal RAM: this panel is a member of Board, a static in app_main.
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
    // The driver restarts the transfer at the next vblank, on the framebuffer the refill took
    // last: the current one, since a present returns only once the refill took it. The sign is
    // cleared after the request, so a vblank between the two, which already restarted, costs a
    // frame. The vblank's interrupt gives the sign before it restarts, and runs on this task's
    // core, so the restart is done when the wait returns.
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_rgb_panel_restart(_panel));
    xSemaphoreTake(_vsync, 0);
    xSemaphoreTake(_vsync, pdMS_TO_TICKS(LCD_FRAME_TIMEOUT_MS));
}

bool RgbPanel::stop() {
    // Paired with present(), which sets _presenting before it reads _stopped: either the present
    // sees the stop, or the stop sees the present.
    _stopped.store(true);

    const auto deadline = esp_timer_get_time() + LCD_STOP_TIMEOUT_MS * 1000ll;
    while (_presenting.load()) {
        if (esp_timer_get_time() >= deadline) {
            return false;
        }

        vTaskDelay(1);
    }

    return true;
}

RgbPanel::Halt RgbPanel::halt(CacheState& cache) {
    // As the driver's own teardown does. The DMA then waits on the LCD's full FIFO, so no bounce
    // buffer ends and no refill begins.
    lcd_ll_stop(LCD_LL_GET_HW(0));
    esp_rom_delay_us(LCD_HALT_SETTLE_US);

    auto halt = Halt::PreloadDone;
    if (!wait_for_preload()) {
        // No refill runs, so the preload hung. The ROM starts one by setting its enable bit, which
        // the hardware clears only once the preload is done, and never clears it itself: while a
        // hung one holds it, a start may begin nothing. So the bit is cleared first, and one line
        // of a framebuffer is preloaded afresh.
        void* fb0 = nullptr;
        ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(_panel, 1, &fb0));

        REG_CLR_BIT(EXTMEM_DCACHE_PRELOAD_CTRL_REG, EXTMEM_DCACHE_PRELOAD_ENA);
        Cache_Start_DCache_Preload(uint32_t(fb0), CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE, 0);

        halt = wait_for_preload() ? Halt::PreloadRestarted : Halt::PreloadHung;
    }

    cache.preload = REG_READ(EXTMEM_DCACHE_PRELOAD_CTRL_REG);
    cache.autoload = REG_READ(EXTMEM_DCACHE_AUTOLOAD_CTRL_REG);
    cache.sync = REG_READ(EXTMEM_DCACHE_SYNC_CTRL_REG);
    cache.state = REG_READ(EXTMEM_CACHE_STATE_REG);

    return halt;
}

void RgbPanel::present(uint8_t* framebuffer) {
    _presenting.store(true);
    if (_stopped.load()) {
        _presenting.store(false);
        return;
    }

    // framebuffer is one of the panel's own, so the driver only makes it the current one. The
    // refill takes the current buffer as it begins copying a frame, and until then may copy from
    // the old one, which LVGL draws into next. The refill's interrupt runs on this task's core:
    // a sign it gives after the clear comes from a frame begun after the swap, and one begun
    // between the swap and the clear costs a frame.
    esp_lcd_panel_draw_bitmap(_panel, 0, 0, LCD_H_RES, LCD_V_RES, framebuffer);
    xSemaphoreTake(_taken, 0);
    xSemaphoreTake(_taken, pdMS_TO_TICKS(LCD_FRAME_TIMEOUT_MS));

    _presenting.store(false);
}

// Called from the panel's interrupts, which run while flash is written: in IRAM, and calling
// nothing outside it.

bool IRAM_ATTR RgbPanel::on_vsync(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*, void* user_ctx) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(((RgbPanel*)user_ctx)->_vsync, &higher_priority_task_woken);

    return higher_priority_task_woken == pdTRUE;
}

bool IRAM_ATTR RgbPanel::on_framebuffer_taken(esp_lcd_panel_handle_t, const esp_lcd_rgb_panel_event_data_t*,
                                              void* user_ctx) {
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(((RgbPanel*)user_ctx)->_taken, &higher_priority_task_woken);

    return higher_priority_task_woken == pdTRUE;
}

void RgbPanel::flush(lv_display_t* display, const lv_area_t*, uint8_t* px_map) {
    auto self = (RgbPanel*)lv_display_get_user_data(display);

    if (lv_display_flush_is_last(display)) {
        self->present(px_map);
    }

    lv_display_flush_ready(display);
}
