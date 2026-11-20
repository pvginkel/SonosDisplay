#include "TfCard.h"

#include "BoardConfig.h"
#include "diskio_impl.h"
#include "diskio_sdmmc.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "ff.h"
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

// The clocks. SCK, MOSI and MISO are SPI2's own pins, which reach it through the IO MUX and not
// the GPIO matrix (BoardConfig.h), so the bus runs at 40 MHz, the APB's 80 MHz halved, reading
// with no dummy cycle. A card takes more than 25 MHz once it is switched to high speed, which
// ESP-IDF does where the host asks for more than SDMMC_FREQ_DEFAULT; the next clock below 40 MHz
// that the bus makes is 26.7 MHz, which is past the card's default speed of 25 MHz.
static constexpr uint32_t HIGH_CLOCK_KHZ = SDMMC_FREQ_HIGHSPEED;
static constexpr uint32_t DEFAULT_CLOCK_KHZ = SDMMC_FREQ_DEFAULT;

// ESP-IDF's time limits for the commands (sdmmc_common.h, which is not public).
static constexpr uint32_t COMMAND_TIMEOUT_MS = 1000;
static constexpr uint32_t WRITE_TIMEOUT_MS = 5000;

// FAT's disk functions for the card, which take the place of ESP-IDF's (fatfs/diskio/
// diskio_sdmmc.c) once it is mounted. ESP-IDF's go through sdmmc_read_sectors() and
// sdmmc_write_sectors(), which split a transfer into or out of PSRAM into a command a sector, each
// followed by a status command: the SD host's DMA cannot reach PSRAM. The SPI host copies every
// sector through a buffer of its own in internal RAM (esp_driver_sdspi's sdspi_host.c), so it takes
// a buffer in PSRAM as it is, and these send one command for the whole transfer, CMD18 or CMD25
// where it is more than a sector. FAT asks for the sectors of a file that are in one allocation
// unit in one transfer, straight into the caller's buffer.
static sdmmc_card_t* s_card;

static esp_err_t send(sdmmc_card_t* card, sdmmc_command_t& command, uint32_t timeout_ms) {
    command.timeout_ms = card->host.command_timeout_ms ? card->host.command_timeout_ms : timeout_ms;

    const auto err = card->host.do_transaction(card->host.slot, &command);

    return err != ESP_OK ? err : command.error;
}

static esp_err_t transfer(sdmmc_card_t* card, void* buffer, uint32_t sector, uint32_t count, bool write) {
    if (sector + uint64_t(count) > uint64_t(card->csd.capacity)) {
        return ESP_ERR_INVALID_SIZE;
    }

    const auto length = size_t(count) * card->csd.sector_size;

    sdmmc_command_t command{};
    if (write) {
        command.opcode = count == 1 ? MMC_WRITE_BLOCK_SINGLE : MMC_WRITE_BLOCK_MULTIPLE;
        command.flags = SCF_CMD_ADTC | SCF_RSP_R1;
    } else {
        command.opcode = count == 1 ? MMC_READ_BLOCK_SINGLE : MMC_READ_BLOCK_MULTIPLE;
        command.flags = SCF_CMD_ADTC | SCF_CMD_READ | SCF_RSP_R1;
    }
    // A card of standard capacity is addressed by the byte, a larger one by the sector.
    command.arg = (card->ocr & SD_OCR_SDHC_CAP) ? sector : sector * card->csd.sector_size;
    command.blklen = card->csd.sector_size;
    command.data = buffer;
    command.datalen = length;
    command.buflen = length;

    auto err = send(card, command, write ? WRITE_TIMEOUT_MS : COMMAND_TIMEOUT_MS);
    if (err != ESP_OK || !write) {
        return err;
    }

    // The SD specification has the host ask for the card's status after a write over SPI: some
    // failures to program show there only. ESP-IDF asks too.
    sdmmc_command_t status{};
    status.opcode = MMC_SEND_STATUS;
    status.arg = MMC_ARG_RCA(card->rca);
    status.flags = SCF_CMD_AC | SCF_RSP_R1;

    err = send(card, status, COMMAND_TIMEOUT_MS);
    if (err != ESP_OK) {
        return err;
    }

    return SD_SPI_R2(status.response) == 0 ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

// Runs the card at the default clock from here on.
static esp_err_t slow_down(sdmmc_card_t* card) {
    const auto err = card->host.set_card_clk(card->host.slot, DEFAULT_CLOCK_KHZ);
    if (err == ESP_OK) {
        card->max_freq_khz = DEFAULT_CLOCK_KHZ;
        card->host.get_real_freq(card->host.slot, &card->real_freq_khz);
    }

    return err;
}

static DSTATUS card_initialize(BYTE) { return sdmmc_get_status(s_card) == ESP_OK ? 0 : STA_NOINIT; }

// ESP-IDF's asks the card only where it is told to, which the mount does not do.
static DSTATUS card_status(BYTE) { return 0; }

static DRESULT card_read(BYTE, BYTE* buffer, DWORD sector, UINT count) {
    auto err = transfer(s_card, buffer, sector, count, false);

    // A card that fails a read at the high clock runs at the default one from here on, and the
    // read is made again, once the card is told to stop sending what is left of the one that
    // failed. A read changes nothing on the card, so it is safe to make again.
    if (err != ESP_OK && s_card->real_freq_khz > int(DEFAULT_CLOCK_KHZ)) {
        ESP_LOGW(TAG, "A read of the TF card failed at %d kHz (%s): it runs at %" PRIu32 " kHz from here on",
                 s_card->real_freq_khz, esp_err_to_name(err), DEFAULT_CLOCK_KHZ);

        sdmmc_command_t stop{};
        stop.opcode = MMC_STOP_TRANSMISSION;
        stop.flags = SCF_CMD_AC | SCF_RSP_R1B;
        send(s_card, stop, COMMAND_TIMEOUT_MS);

        err = slow_down(s_card);
        if (err == ESP_OK) {
            err = transfer(s_card, buffer, sector, count, false);
        }
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "A read of %u sectors at sector %" PRIu32 " of the TF card failed (%s)", count, sector,
                 esp_err_to_name(err));
        return RES_ERROR;
    }

    return RES_OK;
}

// A write that fails is not made again: after a failure the SPI host sends the card no stop token,
// so the card may take what comes next for the rest of the write, at the sectors after it.
static DRESULT card_write(BYTE, const BYTE* buffer, DWORD sector, UINT count) {
    const auto err = transfer(s_card, (void*)buffer, sector, count, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "A write of %u sectors at sector %" PRIu32 " of the TF card failed (%s)", count, sector,
                 esp_err_to_name(err));
        return RES_ERROR;
    }

    return RES_OK;
}

static DRESULT card_ioctl(BYTE, BYTE command, void* buffer) {
    switch (command) {
        // A write is on the card when card_write() returns.
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            *(DWORD*)buffer = s_card->csd.capacity;
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buffer = WORD(s_card->csd.sector_size);
            return RES_OK;
        // ESP-IDF trims an MMC card only, and answers so for an SD card.
        case CTRL_TRIM:
            return RES_PARERR;
    }

    return RES_ERROR;
}

static constexpr ff_diskio_impl_t CARD_DISK = {
    .init = card_initialize,
    .status = card_status,
    .read = card_read,
    .write = card_write,
    .ioctl = card_ioctl,
};

// FAT's allocation unit on the mounted volume of drive `pdrv`, in bytes, or 0.
static uint32_t read_cluster_bytes(BYTE pdrv, const sdmmc_card_t* card) {
    const char drive[] = {char('0' + pdrv), ':', '\0'};

    FF_DIR root;
    if (f_opendir(&root, drive) != FR_OK) {
        return 0;
    }

    const auto bytes = uint32_t(root.obj.fs->csize) * card->csd.sector_size;
    f_closedir(&root);

    return bytes;
}

// Mounts the card with the host's clock at `clock_khz` at most, formatting it where `format` says
// and FAT finds no file system on it.
static esp_err_t mount_at(uint32_t clock_khz, bool format, sdmmc_card_t** card) {
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = TF_CARD_SPI_HOST;
    host.max_freq_khz = int(clock_khz);

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = TF_CARD_SPI_HOST;
    slot_config.gpio_cs = SDSPI_SLOT_NO_CS;

    esp_vfs_fat_mount_config_t mount_config{};
    mount_config.max_files = MAX_OPEN_FILES;
    mount_config.allocation_unit_size = FORMAT_ALLOCATION_UNIT;
    mount_config.format_if_mount_failed = format;

    return esp_vfs_fat_sdspi_mount(TfCard::MOUNT_POINT, &host, &slot_config, &mount_config, card);
}

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
        return Mount{.found = Found::Failed, .err = err};
    }

    // An empty slot drives nothing. Pulled up, MISO then reads 0xFF, which the driver takes for no
    // answer.
    gpio_set_pull_mode(TF_CARD_GPIO_MISO, GPIO_PULLUP_ONLY);

    sdmmc_card_t* card = nullptr;
    auto found = Found::Mounted;
    auto slowed = false;

    // A card that does not answer does so at the probing clock already, whatever the clock asked
    // for. One that answers and cannot be mounted at the high clock is mounted at the default one:
    // a card that fails there, or on which FAT finds nothing it can mount. ESP_FAIL is the mount's
    // answer for a card FAT cannot mount; a card that does not answer, or answers wrongly, has the
    // driver's own error. Of the cards FAT cannot mount at the default clock, the last mount
    // formats the one on which it finds no file system, and no other.
    err = mount_at(HIGH_CLOCK_KHZ, false, &card);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "The TF card cannot be mounted at %" PRIu32 " kHz (%s); mounting it at %" PRIu32 " kHz",
                 HIGH_CLOCK_KHZ, esp_err_to_name(err), DEFAULT_CLOCK_KHZ);

        slowed = true;
        err = mount_at(DEFAULT_CLOCK_KHZ, false, &card);

        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "FAT cannot mount the TF card; mounting it again, formatting it where it has no file system");

            found = Found::Formatted;
            err = mount_at(DEFAULT_CLOCK_KHZ, true, &card);
        }
    }

    if (err != ESP_OK) {
        spi_bus_free(TF_CARD_SPI_HOST);

        return Mount{.found = err == ESP_ERR_TIMEOUT ? Found::NoCard : Found::Failed, .err = err};
    }

    const auto pdrv = ff_diskio_get_pdrv_card(card);
    s_card = card;
    ff_diskio_register(pdrv, &CARD_DISK);

    const auto megabytes = uint64_t(card->csd.capacity) * card->csd.sector_size / (1024 * 1024);

    return Mount{
        .found = found,
        .err = ESP_OK,
        .megabytes = uint32_t(megabytes),
        .clock_khz = uint32_t(card->real_freq_khz),
        .cluster_bytes = read_cluster_bytes(pdrv, card),
        .slowed = slowed,
    };
}
