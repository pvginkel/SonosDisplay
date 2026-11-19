#pragma once

#include <cstdint>

#include "esp_err.h"

// The board's TF card: SD over SPI, with FAT at MOUNT_POINT. The slot has no chip-select line and
// no card-detect line (BoardConfig.h), so a card that is not there is one that does not answer.
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
    };

    TfCard() = default;
    TfCard(const TfCard&) = delete;
    TfCard& operator=(const TfCard&) = delete;

    // Sets up the SPI bus and mounts the card. A format wipes the card and writes its whole FAT.
    // The SPI bus is given back where the card is not mounted. Called once, on the task that then
    // makes every access to the card: it blocks for as long as the card takes.
    Mount mount();
};
