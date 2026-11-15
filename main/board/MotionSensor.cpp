#include "MotionSensor.h"

#include "BoardConfig.h"
#include "esp_attr.h"
#include "support.h"

LOG_TAG(MotionSensor);

esp_err_t MotionSensor::begin() {
    gpio_config_t config{};
    config.pin_bit_mask = 1ull << MOTION_SENSOR_GPIO;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_POSEDGE;

    ESP_ERROR_RETURN(gpio_config(&config));

    const auto err = gpio_install_isr_service(0);
    if (err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE: someone else installed it already.
        ESP_ERROR_RETURN(err);
    }

    ESP_ERROR_RETURN(gpio_isr_handler_add(MOTION_SENSOR_GPIO, isr, this));

    return ESP_OK;
}

bool MotionSensor::poll() {
    const auto triggered = _triggered.exchange(false);

    return triggered || gpio_get_level(MOTION_SENSOR_GPIO);
}

void IRAM_ATTR MotionSensor::isr(void* arg) { ((MotionSensor*)arg)->_triggered.store(true); }
