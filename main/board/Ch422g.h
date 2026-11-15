#pragma once

#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_err.h"

// The CH422G I/O expander. It keeps the output byte and writes it whole on every change.
class Ch422g {
    i2c_master_dev_handle_t _system{};
    i2c_master_dev_handle_t _output{};
    uint8_t _value{};

public:
    Ch422g() = default;
    Ch422g(const Ch422g&) = delete;
    Ch422g& operator=(const Ch422g&) = delete;

    esp_err_t begin(i2c_master_bus_handle_t bus, uint8_t value);
    esp_err_t set(uint8_t bits, bool on);
    uint8_t get() const { return _value; }

private:
    esp_err_t write();
};
