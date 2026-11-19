#pragma once

// Every pin, address and timing of the Waveshare ESP32-S3-Touch-LCD-4.3, as this firmware uses
// it. The values are ThermostatDisplay's shipped ones; docs/firmware-design.md, section 2.

#include <cstddef>
#include <cstdint>

#include "driver/gpio.h"
#include "driver/i2c_types.h"
#include "hal/spi_types.h"

// RGB panel (ST7262, 800x480 RGB565).

constexpr uint32_t LCD_H_RES = 800;
constexpr uint32_t LCD_V_RES = 480;

// 14 MHz is deliberate: 21 MHz and the vendor's 30 MHz were too fast for two framebuffers in
// PSRAM.
constexpr uint32_t LCD_PCLK_HZ = 14 * 1000 * 1000;

constexpr uint32_t LCD_HSYNC_PULSE_WIDTH = 10;
constexpr uint32_t LCD_HSYNC_BACK_PORCH = 10;
constexpr uint32_t LCD_HSYNC_FRONT_PORCH = 20;
constexpr uint32_t LCD_VSYNC_PULSE_WIDTH = 10;
constexpr uint32_t LCD_VSYNC_BACK_PORCH = 10;
constexpr uint32_t LCD_VSYNC_FRONT_PORCH = 10;

constexpr size_t LCD_DATA_WIDTH = 16;
constexpr size_t LCD_BITS_PER_PIXEL = 16;
constexpr size_t LCD_FRAME_BUFFERS = 2;
constexpr size_t LCD_DMA_BURST_SIZE = 64;

// The DMA reads the picture from two bounce buffers of ten lines in internal RAM, 16,000 bytes
// each, which the panel's interrupts refill from the current framebuffer. The driver refuses a
// framebuffer that is not a whole number of them.
constexpr size_t LCD_BOUNCE_BUFFER_PX = 10 * LCD_H_RES;
static_assert(LCD_H_RES * LCD_V_RES % LCD_BOUNCE_BUFFER_PX == 0);

constexpr gpio_num_t LCD_GPIO_HSYNC = GPIO_NUM_46;
constexpr gpio_num_t LCD_GPIO_VSYNC = GPIO_NUM_3;
constexpr gpio_num_t LCD_GPIO_DE = GPIO_NUM_5;
constexpr gpio_num_t LCD_GPIO_PCLK = GPIO_NUM_7;
constexpr gpio_num_t LCD_GPIO_DISP = GPIO_NUM_NC;

// D0-D4 blue, D5-D10 green, D11-D15 red.
constexpr gpio_num_t LCD_GPIO_DATA[LCD_DATA_WIDTH] = {
    GPIO_NUM_14, GPIO_NUM_38, GPIO_NUM_18, GPIO_NUM_17, GPIO_NUM_10, GPIO_NUM_39, GPIO_NUM_0,  GPIO_NUM_45,
    GPIO_NUM_48, GPIO_NUM_47, GPIO_NUM_21, GPIO_NUM_1,  GPIO_NUM_2,  GPIO_NUM_42, GPIO_NUM_41, GPIO_NUM_40,
};

// How long a wait for a vblank, or for the refill to take a framebuffer, lasts at most. Each comes
// once a frame, and a frame is 840 x 510 clocks, 30.6 ms at 14 MHz.
constexpr uint32_t LCD_FRAME_TIMEOUT_MS = 100;

// How long the stop before a restart waits for a present another task is in: the swap, and its
// one wait for the refill to take the framebuffer.
constexpr uint32_t LCD_STOP_TIMEOUT_MS = LCD_FRAME_TIMEOUT_MS + 50;

// The halt before a restart. Once the LCD stops, a refill under way may still run, the last, and
// start a preload; a bounce buffer lasts 600 us. A preload of a bounce buffer's 16,000 bytes takes
// well under a millisecond, and one of a single cache line far less.
constexpr uint32_t LCD_HALT_SETTLE_US = 2 * 1000;
constexpr uint32_t LCD_PRELOAD_TIMEOUT_US = 5 * 1000;

// I2C bus, shared by the expander and the touch controller.

constexpr i2c_port_num_t I2C_PORT = 0;
constexpr gpio_num_t I2C_GPIO_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_GPIO_SCL = GPIO_NUM_9;
constexpr uint32_t I2C_SPEED_HZ = 400 * 1000;
constexpr int I2C_TIMEOUT_MS = 100;

// CH422G I/O expander: write 0x01 to the first address once to enable the outputs, then the
// output byte to the second.

constexpr uint16_t EXPANDER_SYSTEM_ADDRESS = 0x24;
constexpr uint16_t EXPANDER_OUTPUT_ADDRESS = 0x38;
constexpr uint8_t EXPANDER_SYSTEM_ENABLE_OUTPUTS = 0x01;

constexpr uint8_t EXPANDER_TOUCH_RESET = 1 << 1;
constexpr uint8_t EXPANDER_BACKLIGHT = 1 << 2;
constexpr uint8_t EXPANDER_LCD_RESET = 1 << 3;
constexpr uint8_t EXPANDER_TF_CARD_SELECT = 1 << 4;

// TF card slot: SD over SPI. Its chip select is the expander's EXIO4, active low. No expander
// write sets that bit, so the card is selected from the first write on and the SPI slot has no
// chip-select line: the card needs no I2C, which only the UI task touches.

constexpr spi_host_device_t TF_CARD_SPI_HOST = SPI2_HOST;
constexpr gpio_num_t TF_CARD_GPIO_MOSI = GPIO_NUM_11;
constexpr gpio_num_t TF_CARD_GPIO_SCK = GPIO_NUM_12;
constexpr gpio_num_t TF_CARD_GPIO_MISO = GPIO_NUM_13;

static_assert(((EXPANDER_TOUCH_RESET | EXPANDER_BACKLIGHT | EXPANDER_LCD_RESET) & EXPANDER_TF_CARD_SELECT) == 0);

// GT911 touch controller. It answers on one of two addresses, depending on its interrupt line at
// power-on. Its interrupt (GPIO 4) is not used: touch is polled.

constexpr uint16_t TOUCH_ADDRESS = 0x5D;
constexpr uint16_t TOUCH_ADDRESS_BACKUP = 0x14;

// After a power-on the GT911 is held in reset until the expander's first write, and answers some
// tens of milliseconds after it.
constexpr int64_t TOUCH_STARTUP_TIMEOUT_MS = 500;
constexpr uint32_t TOUCH_PROBE_INTERVAL_MS = 10;

// Motion sensor on the board's "Sensor" header, active high.

constexpr gpio_num_t MOTION_SENSOR_GPIO = GPIO_NUM_6;

// The backlight is switched at most once a second, in either direction: switching it faster
// browns the board out.

constexpr int64_t BACKLIGHT_MIN_INTERVAL_MS = 1000;
