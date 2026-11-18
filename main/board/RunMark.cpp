#include "RunMark.h"

#include <cinttypes>
#include <cstddef>
#include <cstring>

#include "esp_attr.h"
#include "esp_rom_crc.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "support.h"

LOG_TAG(RunMark);

namespace {

enum class Restart : uint8_t {
    None,
    Begun,
    Stopped,
    // A present on another task was still under way when the stop's time ran out.
    StopTimedOut,
};

struct Mark {
    uint32_t magic;
    uint32_t stage_ms;
    uint32_t restart_ms;
    RunMark::Stage stage;
    Restart restart;
    bool halted;
    RgbPanel::Halt halt;
    RgbPanel::CacheState cache;
    char restart_task[configMAX_TASK_NAME_LEN];
    uint32_t crc;
};

constexpr uint32_t MARK_MAGIC = 0x4B52414D;

// RTC memory holds what it happens to hold after a power-on, and a reset can cut a write short:
// a mark counts only with its magic and its CRC.
RTC_NOINIT_ATTR Mark rtc_mark;

Mark previous_mark;
bool has_previous_mark;

uint32_t crc_of(const Mark& mark) { return esp_rom_crc32_le(0, (const uint8_t*)&mark, offsetof(Mark, crc)); }

void seal() { rtc_mark.crc = crc_of(rtc_mark); }

uint32_t uptime_ms() { return uint32_t(esp_timer_get_time() / 1000); }

const char* stage_name(RunMark::Stage stage) {
    switch (stage) {
        case RunMark::Stage::Booting:
            return "booting";
        case RunMark::Stage::Configured:
            return "configured";
        case RunMark::Stage::Ready:
            return "ready";
        case RunMark::Stage::Settled:
            return "settled";
    }

    return "unknown";
}

const char* restart_outcome(Restart restart) {
    switch (restart) {
        case Restart::Begun:
            return "it reset before the panel stopped";
        case Restart::Stopped:
            return "the panel stopped";
        case Restart::StopTimedOut:
            return "the panel stopped with a present still under way";
    }

    return "unknown";
}

const char* halt_outcome(RgbPanel::Halt halt) {
    switch (halt) {
        case RgbPanel::Halt::PreloadDone:
            return "was done";
        case RgbPanel::Halt::PreloadRestarted:
            return "hung, and one begun afresh finished";
        case RgbPanel::Halt::PreloadHung:
            return "hung, and one begun afresh did not finish";
    }

    return "unknown";
}

// The S3's reasons as soc/reset_reasons.h names them. A value that names a CPU is the same for
// both CPUs.
const char* raw_reason_name(soc_reset_reason_t reason) {
    switch (reason) {
        case RESET_REASON_CHIP_POWER_ON:
            return "power-on, brown-out or super watchdog reset the chip";
        case RESET_REASON_CORE_SW:
            return "software reset the digital core";
        case RESET_REASON_CORE_DEEP_SLEEP:
            return "deep sleep reset the digital core";
        case RESET_REASON_CORE_MWDT0:
            return "main watchdog 0 reset the digital core";
        case RESET_REASON_CORE_MWDT1:
            return "main watchdog 1 reset the digital core";
        case RESET_REASON_CORE_RTC_WDT:
            return "RTC watchdog reset the digital core";
        case RESET_REASON_CPU0_MWDT0:
            return "main watchdog 0 reset the CPU";
        case RESET_REASON_CPU0_SW:
            return "software reset the CPU";
        case RESET_REASON_CPU0_RTC_WDT:
            return "RTC watchdog reset the CPU";
        case RESET_REASON_SYS_BROWN_OUT:
            return "brown-out reset the digital core";
        case RESET_REASON_SYS_RTC_WDT:
            return "RTC watchdog reset the digital core and the RTC";
        case RESET_REASON_CPU0_MWDT1:
            return "main watchdog 1 reset the CPU";
        case RESET_REASON_SYS_SUPER_WDT:
            return "super watchdog reset the digital core and the RTC";
        case RESET_REASON_SYS_CLK_GLITCH:
            return "clock glitch reset the digital core and the RTC";
        case RESET_REASON_CORE_EFUSE_CRC:
            return "eFuse CRC error reset the digital core";
        case RESET_REASON_CORE_USB_UART:
            return "USB UART reset the digital core";
        case RESET_REASON_CORE_USB_JTAG:
            return "USB JTAG reset the digital core";
        case RESET_REASON_CORE_PWR_GLITCH:
            return "power glitch reset the digital core";
    }

    return "unknown";
}

}  // namespace

void RunMark::begin() {
    has_previous_mark = rtc_mark.magic == MARK_MAGIC && rtc_mark.crc == crc_of(rtc_mark);
    if (has_previous_mark) {
        previous_mark = rtc_mark;
    }

    memset(&rtc_mark, 0, sizeof(rtc_mark));
    rtc_mark.magic = MARK_MAGIC;
    rtc_mark.stage = Stage::Booting;
    rtc_mark.stage_ms = uptime_ms();
    seal();
}

void RunMark::reached(Stage stage) {
    rtc_mark.stage = stage;
    rtc_mark.stage_ms = uptime_ms();
    seal();
}

void RunMark::restart_begun() {
    rtc_mark.restart = Restart::Begun;
    rtc_mark.restart_ms = uptime_ms();
    strlcpy(rtc_mark.restart_task, pcTaskGetName(nullptr), sizeof(rtc_mark.restart_task));
    seal();
}

void RunMark::panel_stopped(bool in_time) {
    rtc_mark.restart = in_time ? Restart::Stopped : Restart::StopTimedOut;
    seal();
}

void RunMark::panel_halted(RgbPanel::Halt halt, const RgbPanel::CacheState& cache) {
    rtc_mark.halted = true;
    rtc_mark.halt = halt;
    rtc_mark.cache = cache;
    seal();
}

void RunMark::log_previous() {
    const auto raw_reason = esp_rom_get_reset_reason(0);
    ESP_LOGI(TAG, "Raw reset reason %d: %s; CPU 1's %d", raw_reason, raw_reason_name(raw_reason),
             esp_rom_get_reset_reason(1));

    if (!has_previous_mark) {
        ESP_LOGI(TAG, "The run before left no mark");
        return;
    }

    const auto& mark = previous_mark;
    if (mark.restart == Restart::None) {
        ESP_LOGI(TAG, "The run before reached %s at %" PRIu32 " ms and began no restart", stage_name(mark.stage),
                 mark.stage_ms);
    } else {
        ESP_LOGI(
            TAG, "The run before reached %s at %" PRIu32 " ms and began a restart on task %s at %" PRIu32 " ms; %s",
            stage_name(mark.stage), mark.stage_ms, mark.restart_task, mark.restart_ms, restart_outcome(mark.restart));
    }

    if (mark.halted) {
        ESP_LOGI(TAG,
                 "The run before's restart halted the scan-out, and the data cache's preload %s; EXTMEM preload "
                 "0x%08" PRIx32 ", autoload 0x%08" PRIx32 ", sync 0x%08" PRIx32 ", state 0x%08" PRIx32,
                 halt_outcome(mark.halt), mark.cache.preload, mark.cache.autoload, mark.cache.sync, mark.cache.state);
    } else if (mark.restart == Restart::Stopped || mark.restart == Restart::StopTimedOut) {
        ESP_LOGI(TAG, "The run before reset before its restart halted the scan-out");
    }
}
