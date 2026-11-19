#include "TfCard.h"

#include "BoardConfig.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdkconfig.h"
#include "sdmmc_cmd.h"
#include "support.h"

LOG_TAG(TfCard);

// FAT's buffers, a sector for the volume and one for each open file, go to PSRAM: internal RAM has
// no room for them (docs/firmware-design.md, section 7).
#ifndef CONFIG_FATFS_ALLOC_PREFER_EXTRAM
#error "CONFIG_FATFS_ALLOC_PREFER_EXTRAM is off: FAT's buffers would take internal RAM"
#endif

// ArtworkStore has one file open at a time.
static constexpr int MAX_OPEN_FILES = 1;

// The largest unit ESP-IDF formats a card with, 128 sectors. A format writes both copies of the
// FAT, four bytes a unit each: 4 MB on a card of 32 GB at this size, and 500 MB at a sector a
// unit, which is what ESP-IDF formats with when no size is given.
static constexpr size_t FORMAT_ALLOCATION_UNIT = 64 * 1024;

TfCard::Mount TfCard::mount() {
    ESP_LOGI(TAG, "Setting up the TF card's SPI bus");

    spi_bus_config_t bus_config{};
    bus_config.mosi_io_num = TF_CARD_GPIO_MOSI;
    bus_config.miso_io_num = TF_CARD_GPIO_MISO;
    bus_config.sclk_io_num = TF_CARD_GPIO_SCK;
    bus_config.quadwp_io_num = GPIO_NUM_NC;
    bus_config.quadhd_io_num = GPIO_NUM_NC;

    auto err = spi_bus_initialize(TF_CARD_SPI_HOST, &bus_config, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK) {
        return Mount{.found = Found::Failed, .err = err, .megabytes = 0};
    }

    // An empty slot drives nothing. Pulled up, MISO then reads 0xFF, which the driver takes for no
    // answer.
    gpio_set_pull_mode(TF_CARD_GPIO_MISO, GPIO_PULLUP_ONLY);

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = TF_CARD_SPI_HOST;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = TF_CARD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;

    esp_vfs_fat_mount_config_t mount_config{};
    mount_config.max_files = MAX_OPEN_FILES;
    mount_config.allocation_unit_size = FORMAT_ALLOCATION_UNIT;

    sdmmc_card_t* card = nullptr;
    auto found = Found::Mounted;

    // ESP_FAIL is the mount's answer for a card FAT cannot mount; a card that does not answer, or
    // answers wrongly, has the driver's own error. Of the cards FAT cannot mount, the second
    // mount formats the one on which it finds no file system, and no other.
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    if (err == ESP_FAIL) {
        ESP_LOGW(TAG, "FAT cannot mount the TF card; mounting it again, formatting it where it has no file system");

        found = Found::Formatted;
        mount_config.format_if_mount_failed = true;

        err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &card);
    }

    if (err != ESP_OK) {
        spi_bus_free(TF_CARD_SPI_HOST);

        return Mount{.found = err == ESP_ERR_TIMEOUT ? Found::NoCard : Found::Failed, .err = err, .megabytes = 0};
    }

    const auto megabytes = uint64_t(card->csd.capacity) * card->csd.sector_size / (1024 * 1024);

    return Mount{.found = found, .err = ESP_OK, .megabytes = uint32_t(megabytes)};
}
