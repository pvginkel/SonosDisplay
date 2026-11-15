#include "Touch.h"

#include <cinttypes>

#include "BoardConfig.h"
#include "ScreenPower.h"
#include "esp_lcd_touch_gt911.h"
#include "support.h"

LOG_TAG(Touch);

// The GT911 picks its address from its interrupt line when it leaves reset. Its reset line is the
// expander's, which is never pulsed, so ask which one it took. After a power-on it leaves reset
// only with the expander's first write, just before this, and needs a moment to answer: so ask
// until it does.
esp_err_t Touch::find_address(i2c_master_bus_handle_t bus, uint16_t& address) {
    const auto start = esp_get_millis();

    while (true) {
        for (const auto candidate : {TOUCH_ADDRESS, TOUCH_ADDRESS_BACKUP}) {
            if (i2c_master_probe(bus, candidate, I2C_TIMEOUT_MS) == ESP_OK) {
                address = candidate;
                ESP_LOGI(TAG, "GT911 answers at 0x%02X after %" PRId64 " ms", address, esp_get_millis() - start);
                return ESP_OK;
            }
        }

        if (esp_get_millis() - start >= TOUCH_STARTUP_TIMEOUT_MS) {
            return ESP_ERR_NOT_FOUND;
        }

        vTaskDelay(pdMS_TO_TICKS(TOUCH_PROBE_INTERVAL_MS));
    }
}

esp_err_t Touch::begin(i2c_master_bus_handle_t bus) {
    uint16_t address;
    ESP_ERROR_RETURN(find_address(bus, address));

    // ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG(), spelled out: the macro's designators are not in
    // declaration order, which C++ refuses.
    esp_lcd_panel_io_i2c_config_t io_config{};
    io_config.dev_addr = address;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 16;
    io_config.flags.disable_control_phase = 1;
    io_config.scl_speed_hz = I2C_SPEED_HZ;

    ESP_ERROR_RETURN(esp_lcd_new_panel_io_i2c(bus, &io_config, &_io));

    esp_lcd_touch_config_t config{};
    config.x_max = LCD_H_RES;
    config.y_max = LCD_V_RES;
    config.rst_gpio_num = GPIO_NUM_NC;
    config.int_gpio_num = GPIO_NUM_NC;

    ESP_ERROR_RETURN(esp_lcd_touch_new_i2c_gt911(_io, &config, &_touch));

    _address = address;

    return ESP_OK;
}

void Touch::begin_lvgl(lv_display_t* display) {
    _indev = lv_indev_create();
    lv_indev_set_type(_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(_indev, read);
    lv_indev_set_user_data(_indev, this);
    lv_indev_set_display(_indev, display);
}

void Touch::read(lv_indev_t* indev, lv_indev_data_t* data) {
    auto self = (Touch*)lv_indev_get_user_data(indev);

    lv_point_t point;
    const auto pressed = self->read_point(point);

    // A touch on a dark screen wakes it and never reaches the UI.
    if (!self->_screen_power->handle_touch(pressed)) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (pressed) {
        data->point = point;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

bool Touch::read_point(lv_point_t& point) {
    if (!_touch) {
        return false;
    }

    // A failed read reports "released" rather than the last coordinates.
    if (esp_lcd_touch_read_data(_touch) != ESP_OK) {
        return false;
    }

    esp_lcd_touch_point_data_t data{};
    uint8_t count = 0;
    if (esp_lcd_touch_get_data(_touch, &data, &count, 1) != ESP_OK || count == 0) {
        return false;
    }

    point.x = data.x;
    point.y = data.y;

    return true;
}
