#pragma once

// Logging for ui/: ESP_LOGx on the device, printf in sim/. This is the only file in ui/ that may
// name an ESP-IDF header, and only behind ESP_PLATFORM.

#ifndef LOG_TAG
#define LOG_TAG(v) [[maybe_unused]] static const char* TAG = #v
#endif

#ifdef ESP_PLATFORM

#include "esp_log.h"

#define UI_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define UI_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define UI_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)

#else

#include <cstdio>

#define UI_LOGI(fmt, ...) printf("I (%s) " fmt "\n", TAG, ##__VA_ARGS__)
#define UI_LOGW(fmt, ...) printf("W (%s) " fmt "\n", TAG, ##__VA_ARGS__)
#define UI_LOGE(fmt, ...) printf("E (%s) " fmt "\n", TAG, ##__VA_ARGS__)

#endif
