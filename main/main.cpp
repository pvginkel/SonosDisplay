#include "Application.h"
#include "board/Board.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// JSON trees go to PSRAM, as LVGL's heap and the response bodies do (docs/firmware-design.md,
// section 7).
static void* json_malloc(size_t size) {
    return heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

static void json_free(void* ptr) { heap_caps_free(ptr); }

extern "C" void app_main(void) {
    cJSON_Hooks hooks{.malloc_fn = json_malloc, .free_fn = json_free};
    cJSON_InitHooks(&hooks);

    static Board board;
    ESP_ERROR_CHECK(board.begin());

    static Application application(&board);
    application.begin();

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
