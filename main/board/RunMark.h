#pragma once

#include <cstdint>

#include "RgbPanel.h"

// How far a run got, kept in RTC memory for the next boot to log beside the chip's raw reset
// reason. A reset of the digital core alone, a software restart's and its RTC watchdog's first
// stage among them, leaves RTC memory as it was; a power-on loses it.
class RunMark {
public:
    // In the order a boot passes them.
    enum class Stage : uint8_t {
        Booting,
        // The device configuration is in; the core dump and update checks follow on the same task.
        Configured,
        // esp-mdm's startup is complete, and the first view opens.
        Ready,
        // The first view is up and the backend's first connections are made.
        Settled,
    };

    // Keeps the run before's mark for log_previous() and starts this run's at Booting.
    static void begin();

    static void reached(Stage stage);

    // Called by the shutdown handler, on the task that began the restart, either side of the
    // panel's stop, and after its halt.
    static void restart_begun();
    static void panel_stopped(bool in_time);
    static void panel_halted(RgbPanel::Halt halt, const RgbPanel::CacheState& cache);

    // The chip's raw reset reason, which tells the restart's watchdog from the panic handler's
    // where esp_reset_reason() says ESP_RST_WDT for both, and the run before's mark.
    static void log_previous();
};
