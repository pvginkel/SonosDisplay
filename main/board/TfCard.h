#pragma once

#include <cstdint>

#include "esp_err.h"

// The board's TF card: SD over SPI, with FAT at MOUNT_POINT. The slot has no chip-select line and
// no card-detect line (BoardConfig.h), so a card that is not there is one that does not answer.
//
// FAT reaches the card through this class's own disk functions, not ESP-IDF's: they move however
// many sectors FAT asks for in one command, wherever its buffer is, so a picture is read straight
// into PSRAM (docs/firmware-design.md, section 7).
class TfCard {
public:
    static constexpr char MOUNT_POINT[] = "/sd";

    // NoCard: nothing answered on the bus. Mounted: the card's own FAT. Formatted: the card
    // answered and FAT could not mount it, and it is mounted now that a card with no file system
    // FAT knows is formatted. Failed: anything else.
    enum class Found { NoCard, Mounted, Formatted, Failed };

    struct Mount {
        Found found;
        // What the driver answered where the card is not mounted.
        esp_err_t err;
        // The card's size where it is mounted.
        uint32_t megabytes;
        // The clock the card runs at where it is mounted, in kHz.
        uint32_t clock_khz;
        // FAT's allocation unit where the card is mounted, in bytes, or 0 where it cannot say. A
        // read of a file takes a command to the card for each unit it spans.
        uint32_t cluster_bytes;
        // Where the card was mounted at the default clock after the mount at the high one failed,
        // what that mount answered; ESP_OK otherwise.
        esp_err_t high_clock_err;
    };

    // The clock asked for first, in kHz, and the one a card that fails there runs at.
    static constexpr uint32_t HIGH_CLOCK_KHZ = 40000;
    static constexpr uint32_t DEFAULT_CLOCK_KHZ = 20000;

    TfCard() = default;
    TfCard(const TfCard&) = delete;
    TfCard& operator=(const TfCard&) = delete;

    // Sets up the SPI bus and mounts the card, at 40 MHz where the card takes high speed, and at
    // 20 MHz where it does not or fails at 40. A format wipes the card and writes its whole FAT.
    // The SPI bus is given back where the card is not mounted. Called once, on the task that then
    // makes every access to the card: it blocks for as long as the card takes.
    Mount mount();
};
