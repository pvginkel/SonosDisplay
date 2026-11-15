#include "Ch422g.h"

#include "BoardConfig.h"
#include "support.h"

LOG_TAG(Ch422g);

esp_err_t Ch422g::begin(i2c_master_bus_handle_t bus, uint8_t value) {
    i2c_device_config_t config{};
    config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    config.scl_speed_hz = I2C_SPEED_HZ;

    config.device_address = EXPANDER_SYSTEM_ADDRESS;
    ESP_ERROR_RETURN(i2c_master_bus_add_device(bus, &config, &_system));

    config.device_address = EXPANDER_OUTPUT_ADDRESS;
    ESP_ERROR_RETURN(i2c_master_bus_add_device(bus, &config, &_output));

    const uint8_t enable = EXPANDER_SYSTEM_ENABLE_OUTPUTS;
    ESP_ERROR_RETURN(i2c_master_transmit(_system, &enable, 1, I2C_TIMEOUT_MS));

    _value = value;

    return write();
}

esp_err_t Ch422g::set(uint8_t bits, bool on) {
    _value = on ? (_value | bits) : (_value & ~bits);

    return write();
}

esp_err_t Ch422g::write() {
    ESP_ERROR_RETURN(i2c_master_transmit(_output, &_value, 1, I2C_TIMEOUT_MS));

    return ESP_OK;
}
