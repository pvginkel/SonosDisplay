// LVGL's configuration for sim/. Like sdkconfig.defaults, it starts from LVGL's defaults and
// changes only the short list below: an LVGL option changed in one is changed in the other in the
// same commit (docs/firmware-design.md, section 5).

#ifndef LV_CONF_H
#define LV_CONF_H

// The changes sdkconfig.defaults makes.

#define LV_COLOR_DEPTH 16
#define LV_USE_STDLIB_STRING LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF LV_STDLIB_CLIB
#define LV_CACHE_DEF_SIZE 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 1
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

// Where the device's ESP-IDF Kconfig defaults differ from LVGL's template, the device's value.

#define LV_COLOR_MIX_ROUND_OFS 128
#define LV_TXT_BREAK_CHARS " ,.;:-_)}"

// What differs on purpose.

// The device allocates from PSRAM (main/board/lv_mem_psram.c); here the C library does. No pixel
// changes.
#define LV_USE_STDLIB_MALLOC LV_STDLIB_CLIB

// The device restarts on an assertion; here the render stops with an error.
#define LV_ASSERT_HANDLER_INCLUDE "sim_assert.h"
#define LV_ASSERT_HANDLER sim_assert_failed();

// The device prints LVGL's log with printf. Here a callback prints it and counts warnings and
// errors, which fail the render: with both on, LVGL 9.2.2's lv_log_add reuses its va_list after
// printf has consumed it.
#define LV_LOG_PRINTF 0

// The PNG encoder for the render's output.
#define LV_USE_LODEPNG 1

#endif
