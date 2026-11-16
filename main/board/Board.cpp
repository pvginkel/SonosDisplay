#include "Board.h"

#include "BoardConfig.h"
#include "RunMark.h"
#include "esp_system.h"
#include "support.h"

LOG_TAG(Board);

// The panel the shutdown handler stops, which takes no argument.
static RgbPanel* shutdown_panel;

esp_err_t Board::begin() {
    RunMark::begin();

    ESP_ERROR_RETURN(_panel.begin());

    // esp_restart() runs the handlers on the task that called it, newest first, before it arms
    // the watchdog that finishes the restart: nothing bounds a handler but itself. ESP-IDF keeps
    // five; esp_timer's is in before app_main, and esp-mdm's two and WiFi's come after this one.
    shutdown_panel = &_panel;
    _shutdown_handler_err = esp_register_shutdown_handler(shut_down);
    if (_shutdown_handler_err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register the shutdown handler: %s", esp_err_to_name(_shutdown_handler_err));
    }

    ESP_LOGI(TAG, "Setting up the I2C bus");

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port = I2C_PORT;
    bus_config.sda_io_num = I2C_GPIO_SDA;
    bus_config.scl_io_num = I2C_GPIO_SCL;
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    ESP_ERROR_RETURN(i2c_new_master_bus(&bus_config, &_i2c_bus));

    // The screen is lit at boot only after a power-on reset. After any other reset it stays dark
    // until motion or a touch, so a brown-out cannot loop through the backlight, and a display
    // that restarts at night stays dark.
    const auto reset_reason = esp_reset_reason();
    const auto lit = reset_reason == ESP_RST_POWERON;

    ESP_LOGI(TAG, "Reset reason %d; backlight %s at boot", reset_reason, lit ? "on" : "off");

    // The touch and LCD reset lines are set high by this first write and never pulsed.
    ESP_ERROR_RETURN(
        _expander.begin(_i2c_bus, EXPANDER_TOUCH_RESET | EXPANDER_LCD_RESET | (lit ? EXPANDER_BACKLIGHT : 0)));

    // Without touch the display still shows what it should, so this is not fatal.
    _touch_err = _touch.begin(_i2c_bus);
    if (_touch_err != ESP_OK) {
        ESP_LOGE(TAG, "Touch is not available: %s", esp_err_to_name(_touch_err));
    }

    ESP_ERROR_RETURN(_motion_sensor.begin());

    ESP_LOGI(TAG, "Initializing LVGL");

    lv_init();
    lv_tick_set_cb([]() { return uint32_t(esp_timer_get_time() / 1000); });

    ESP_ERROR_RETURN(_panel.begin_lvgl());
    _touch.begin_lvgl(_panel.get_display());

    _screen_power.begin(lit);

    return ESP_OK;
}

void Board::log_state() {
    if (_touch_err == ESP_OK) {
        ESP_LOGI(TAG, "Touch: GT911 at 0x%02X", _touch.get_address());
    } else {
        ESP_LOGE(TAG, "Touch is not available: %s", esp_err_to_name(_touch_err));
    }

    if (_shutdown_handler_err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot register the shutdown handler: %s", esp_err_to_name(_shutdown_handler_err));
    }

    RunMark::log_previous();
}

void Board::shut_down() {
    RunMark::restart_begun();
    RunMark::panel_stopped(shutdown_panel->stop());
}

void Board::process() {
    if (_motion_sensor.poll()) {
        _screen_power.handle_motion();
    }

    _screen_power.process();
}
