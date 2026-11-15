# SonosDisplay firmware: design note

Written 2026-11-15 for plan task 4 of `docs/project-plan.md`. It answers that task's seven
questions with a decision each, and task 5 builds the firmware's skeleton from it.

**What it rests on.** Read on 2026-11-15: `pvginkel/ThermostatDisplay` (the old firmware for this
board, with the vendor documentation under `documentation/`), `pvginkel/PaperClock`,
`pvginkel/esp-libs` at `29e9f7e`, `pvginkel/IoTSupport`, the Jenkins shared library, the backend
template's guide, LVGL 9.2.2's source and ESP-IDF 5.5.3's headers and Kconfig. It was written
before any display ran the firmware. What only a device could settle is listed under "To check on
the first display", with what the first display, flashed the same day, showed.

## The decisions

| Question | Decision |
|---|---|
| Layout | PaperClock's shape: `Application : ApplicationBase`, one UI task that drains esp-mdm's queue and runs LVGL, `esp-libs` as path dependencies. `main/` splits into `board/` (hardware), `backend/` (HTTP and events) and `ui/` (LVGL and standard C++ only). |
| Carried over | The panel, expander, touch, backlight and motion-sensor code of ThermostatDisplay's shipped build: two framebuffers in PSRAM at a 14 MHz pixel clock. Rewritten for LVGL 9.2 and for ESP-IDF's current I2C driver, not copied. |
| Picture drift | No change to `esp-libs`. The panel resynchronises whenever the screen is switched on, as ThermostatDisplay's did, and code runs from PSRAM. Not every frame: on the S3, `CONFIG_LCD_RGB_RESTART_IN_VSYNC` pins the picture to the first framebuffer, which was the first display's artifacts. An update is only written during boot, and the display restarts after it. |
| Brown-out | The backlight is switched at most once a second, in either direction, by one state machine. Boot lights the screen only after a power-on reset. |
| HTTP and events | Three small tasks in the app: one for API calls, one for artwork, one holding the event stream. Results reach the UI task through esp-mdm's queue. The code lives in the app, `main/backend/`, written free of app types so it can move to `esp-libs` when a second firmware needs it. |
| Backend address | IoT Support's device configuration, key `backendUrl`, required by the model's schema. Not a build setting. |
| Screens without a device | `sim/`: a native build of `main/ui/` against LVGL 9.2.2 that renders every scene to an 800×480 PNG. It is the repository's `kc project test`. The Windows simulator is not carried over. |
| Model and provisioning | Model code `sonos_display`, which is also the CMake project name. The operator creates the model and the device in IoT Support, then runs Flash device and Provision device. `scripts/upload.sh` is the two-line fetcher of the script IoT Support serves. |
| Memory | Everything large is in PSRAM: framebuffers 1.5 MB, LVGL's heap, an artwork cache capped at 1.5 MiB, response bodies. At most 200 list rows are built at a time. Roboto as pre-rendered bitmap fonts for Basic Latin, Latin-1 and Latin Extended-A. |

One thing in this note was the operator's to rule: registering the firmware as an architecture
producer changes three other repositories. They ruled on 2026-11-15 that task 5 makes those
changes, and section 6 lists them.

## 1. Layout on `esp-libs`

### What PaperClock does, and what is kept

PaperClock is the reference, and these parts of it are kept as they are:

- **The build.** The top-level `CMakeLists.txt` sets `PROJECT_VER` to the short git hash and
  lists each `esp-libs` component's `sdkconfig.defaults` before the repository's own.
  `main/CMakeLists.txt` globs the sources recursively. `main/idf_component.yml` names the
  `esp-libs` components as `path: ../../esp-libs/<name>`, so `esp-libs` is checked out beside the
  repository, in the environment and in Jenkins.
- **The application.** `Application` derives from esp-mdm's `ApplicationBase`, which does
  provisioning from NVS, WiFi, the Keycloak login, the device configuration, the coredump upload,
  the update check, MQTT and log shipping. The app overrides its `do_*` hooks.
- **One UI task.** A single task calls `process()` (esp-mdm's queue, then `do_process()`) and
  `lv_timer_handler()` in a loop. No other task touches LVGL or the app's state: they hand a
  closure to `get_queue().enqueue(...)`. There is no LVGL lock, and none is added.
- **Style.** `main/.clang-format` (Google, four spaces, 120 columns), PascalCase files and
  classes, snake_case methods, `_member` fields, `LOG_TAG(Class)` at the top of each `.cpp`,
  `esp_err_t` returns with `esp-support`'s `error.h` macros, no exceptions.
- **`lvgl_assert.h`**, unchanged: it makes an LVGL assertion restart the device, and
  `CONFIG_LV_ASSERT_HANDLER_INCLUDE="../../../../main/lvgl_assert.h"` points at it.

These parts of PaperClock are not followed, each for a reason found in its code:

- **Its own `Callback.h`** is an older copy that shadows `esp-support`'s. SonosDisplay uses
  `esp-support`'s `Callback.h`, `Queue.h`, `defer.h`, `strformat.h` and `error.h`.
- **`includes.h`**, which pulls every ESP-IDF header and `using namespace std` into every file.
  `main/ui/` must compile without ESP-IDF (section 5), so each file includes what it uses.
- **Two tick sources.** PaperClock calls `lv_tick_inc` from a timer and again from its loop, so
  LVGL's clock runs fast. SonosDisplay has one: `lv_tick_set_cb` returning
  `esp_timer_get_time() / 1000`.
- **Fonts through `tiny_ttf`**, and its FontAwesome files, which are not for a public
  repository (section 7).
- **`lv_obj_set_visibility` in `lv_support.cpp`**, whose branches are swapped. `lv_support` is
  copied without it and without `lv_label_get_text_size`, which is LVGL 8.
- **The partition table**, which has a factory slot and no coredump partition (below).

### The tree

```
CMakeLists.txt              project(sonos_display); PROJECT_VER; SDKCONFIG_DEFAULTS
sdkconfig.defaults
partitions.csv
Jenkinsfile                 job Firmware/SonosDisplay
scripts/upload.sh
main/
  CMakeLists.txt  idf_component.yml  .clang-format  lvgl_assert.h
  main.cpp                  app_main: Board, Application, idle loop
  Application.{h,cpp}       : ApplicationBase; owns the UI task and the Shell; implements UiHost
  board/                    ESP-IDF and LVGL's driver interface; nothing above it
    BoardConfig.h           every pin, address and timing of this note, as constants
    Board.{h,cpp}           begin(), process(); owns the parts below
    RgbPanel.{h,cpp}        esp_lcd RGB panel, the LVGL display and its flush
    Ch422g.{h,cpp}          the I/O expander
    Touch.{h,cpp}           GT911 and the LVGL input device
    ScreenPower.{h,cpp}     backlight, wake and sleep (section 3)
    MotionSensor.{h,cpp}
    lv_mem_psram.c          LVGL's allocator on PSRAM (section 7)
  backend/                  ESP-IDF only; no LVGL, no app types
    HttpClient.{h,cpp}  EventStream.{h,cpp}
  ui/                       LVGL and standard C++ only
    UiHost.h  Model.h       the only two doors between ui/ and the rest
    Shell.{h,cpp}  View.{h,cpp}  Theme.{h,cpp}  Messages.h  lv_support.{h,cpp}  log.h
    LoadingView.{h,cpp}  PlaceholderView.{h,cpp}
    fonts/                  generated .c files and icons.h
sim/                        the native render build (section 5)
tools/fonts/                the TTF files, their licences and generate.sh
```

There is no `Kconfig.projbuild`: the board is one fixed product, so its constants are in
`BoardConfig.h`, and what varies per display comes from IoT Support (section 4).

### Tasks and the boot sequence

| Task | Stack | Priority | Core | Does |
|---|---|---|---|---|
| `ui` | 16384 | 2 | 1 | `process()`, `lv_timer_handler()`, everything that touches LVGL, the model, I2C and NVS |
| `backend_api` | 6144 | 1 | 0 | API requests, one at a time |
| `backend_art` | 6144 | 1 | 0 | artwork downloads, one at a time |
| `backend_events` | 6144 | 1 | 0 | the event stream |

esp-mdm's own tasks (log shipping, MQTT) are as it creates them. The UI task's stack is 16 KB
because esp-mdm's HTTPS login and its update download run on whichever task calls `process()`.

The UI loop sleeps for what `lv_timer_handler()` returns, clamped to 1 to 10 ms.
`CONFIG_FREERTOS_HZ=1000` makes that delay real.

1. `app_main` constructs `Board` and calls `board.begin()`: panel, I2C bus, expander, touch,
   `lv_init()`, the LVGL display and input device. Then `Application(&board).begin()`, then it
   idles.
2. `do_begin()` creates the UI task. Its first act is the Shell with `LoadingView` ("Starting").
3. `do_configuration_loaded(cJSON*)` runs on the UI task once WiFi is up. It copies `backendUrl`
   and `screenOffSeconds` out of the JSON, which esp-mdm frees when the hook returns. It then sets
   the loading text to "Checking for an update" and calls `lv_refr_now(nullptr)`, because the
   update check that follows blocks this task (section 2).
4. `do_ready()` runs once MQTT is connected. It starts the three backend tasks and replaces
   `LoadingView` with the app's first view. In task 5 that is `PlaceholderView`.
5. `do_process()` calls `board.process()` and the current view's `update()`.

esp-mdm restarts the device when WiFi cannot be joined, when WiFi or MQTT drops, and when IoT
Support announces a new configuration or firmware. SonosDisplay accepts that: a restart takes
seconds and the speaker plays on. `do_network_connection_failed()` is not overridden, since the
restart follows it at once and nothing drawn there would be seen. A backend that is down is
different: it never restarts the device (section 3).

esp-mdm needs MQTT even though this firmware publishes nothing of its own, so
`sdkconfig.defaults` sets `CONFIG_MQTT_TOPIC_PREFIX` to the value PaperClock's sets, and
`CONFIG_MQTT_DEVICE_MANUFACTURER`, `CONFIG_MQTT_DEVICE_MODEL="Sonos Display"` and
`CONFIG_MQTT_DEVICE_MODEL_ID="Sonos Display v1"`.

### Flash layout

8 MB flash, DIO, 80 MHz. `partitions.csv`:

```
# Name,   Type, SubType,  Offset,   Size
nvs,      data, nvs,      0x9000,   0x4000,
otadata,  data, ota,      0xd000,   0x2000,
phy_init, data, phy,      0xf000,   0x1000,
ota_0,    app,  ota_0,    0x10000,  0x300000,
ota_1,    app,  ota_1,    0x310000, 0x300000,
coredump, data, coredump, 0x610000, 0x40000,
```

- **No factory slot.** ThermostatDisplay's image is 1.4 MB, and this firmware adds fonts and a
  larger LVGL, so 2 MB slots would be tight. Two 3 MB slots fit with the first flash going to
  `ota_0`. The app stays at `0x10000`, where a factory slot would have put it.
- **A coredump partition.** esp-mdm turns coredumps to flash on and uploads them to IoT Support,
  but PaperClock's table has nowhere to store one. This table has.
- **`nvs` stays `0x9000`, 16 KB,** as on every other device: Provision device finds it through
  the partition table and requires at least 12 KB.

### Dependencies

`main/idf_component.yml`: `idf: ">=5.5"`, `lvgl/lvgl: "9.2.2"` (exact, so `sim/` can pin the same
tag), `espressif/esp_lcd_touch_gt911: "1.2.1"` from the component registry, and
`esp-network-support`, `esp-mqtt-support`, `esp-support` and `esp-mdm` by path. ThermostatDisplay
vendored the touch driver under `components/`; that copy is not carried over.

## 2. What is carried over from ThermostatDisplay

ThermostatDisplay's repository holds three display variants behind Kconfig switches. Only one
ever ran on the displays: the committed `sdkconfig` selects two framebuffers. That variant is
what is carried over. The single-framebuffer, bounce-buffer and vsync-semaphore paths, and their
21 MHz timings, are dead code there and are left behind.

### The board

Waveshare ESP32-S3-Touch-LCD-4.3: an ESP32-S3 with 8 MB flash and 8 MB octal PSRAM, an 800×480
RGB565 panel (ST7262), GT911 touch, a CH422G expander.

| Signal | Pin |
|---|---|
| Panel HSYNC, VSYNC, DE, PCLK | GPIO 46, 3, 5, 7. No DISP pin (`-1`). |
| Panel data D0 to D15 | GPIO 14, 38, 18, 17, 10, 39, 0, 45, 48, 47, 21, 1, 2, 42, 41, 40 (D0–D4 blue, D5–D10 green, D11–D15 red) |
| I2C (expander and touch) | port 0, SDA GPIO 8, SCL GPIO 9, 400 kHz, internal pull-ups |
| Touch interrupt | GPIO 4, not used: touch is polled |
| Motion sensor | GPIO 6, the board's "Sensor" header |
| Serial for flashing and the console | the USB-C port marked UART (CH343 on UART0, GPIO 43/44), with an automatic download circuit |

Expander outputs, one byte: bit 1 touch reset, bit 2 backlight, bit 3 LCD reset, bit 4 SD chip
select, bit 5 USB/CAN select. The firmware only ever writes bits 1 to 3.

### Panel

`esp_lcd_new_rgb_panel` with these values, which are the shipped ones:

| Field | Value |
|---|---|
| `clk_src` | `LCD_CLK_SRC_DEFAULT` |
| `timings.pclk_hz` | 14 000 000 |
| `h_res`, `v_res` | 800, 480 |
| hsync pulse, back porch, front porch | 10, 10, 20 |
| vsync pulse, back porch, front porch | 10, 10, 10 |
| `timings.flags.pclk_active_neg` | 0 |
| `data_width`, `bits_per_pixel` | 16, 16 |
| `num_fbs` | 2 |
| `bounce_buffer_size_px` | 0 |
| `dma_burst_size` | 64 (ESP-IDF 5.5's name for the old `psram_trans_align`) |
| `flags.fb_in_psram` | 1 |

Then `esp_lcd_panel_reset()` and `esp_lcd_panel_init()`. That is about 33 frames a second. The
14 MHz clock is deliberate: ThermostatDisplay's comment is "We need to lower the clock speed when
double buffering", and 21 MHz and the vendor's 30 MHz were both too fast for framebuffers in
PSRAM.

### LVGL 9.2 on the panel

| LVGL 8.2 in ThermostatDisplay | LVGL 9.2 in SonosDisplay |
|---|---|
| `lv_disp_draw_buf_init(&buf, fb0, fb1, 800*480)` | `lv_display_set_buffers(disp, fb0, fb1, 800*480*2, LV_DISPLAY_RENDER_MODE_DIRECT)` (the size is in bytes now) |
| `lv_disp_drv_init`, `lv_disp_drv_register`, `full_refresh = true` | `lv_display_create(800, 480)`, `lv_display_set_flush_cb`, `lv_display_set_user_data` |
| `flush_cb(drv, area, lv_color_t*)` | `flush_cb(lv_display_t*, const lv_area_t*, uint8_t* px_map)` |
| `lv_disp_flush_ready(drv)` | `lv_display_flush_ready(disp)` |
| `lv_indev_drv_init`, `lv_indev_drv_register` | `lv_indev_create()`, `lv_indev_set_type(LV_INDEV_TYPE_POINTER)`, `lv_indev_set_read_cb`, `lv_indev_set_display` |
| `read_cb(drv, data)`, `LV_INDEV_STATE_PR` / `_REL` | `read_cb(lv_indev_t*, lv_indev_data_t*)`, `LV_INDEV_STATE_PRESSED` / `_RELEASED` |
| a 2 ms `esp_timer` calling `lv_tick_inc(2)` | `lv_tick_set_cb` (section 1) |
| `LV_COLOR_16_SWAP` off | nothing to set: the panel takes LVGL's native RGB565 |
| `lv_img_dsc_t`, `LV_IMG_CF_TRUE_COLOR` | `lv_image_dsc_t`, `LV_COLOR_FORMAT_RGB565` (section 7) |

The two framebuffers come from `esp_lcd_rgb_panel_get_frame_buffer(panel, 2, &fb0, &fb1)`, and
LVGL draws straight into them.

**Render mode: direct, not full.** ThermostatDisplay redrew the whole screen on every change
(`full_refresh`), which suited a static thermostat. A scrolling list would redraw 384,000 pixels
per frame for nothing. In direct mode LVGL draws only what changed and copies those areas into
the other buffer itself (`refr_sync_areas` in `lv_refr.c`). The flush callback:

```cpp
void RgbPanel::flush(lv_display_t* disp, const lv_area_t*, uint8_t* px_map) {
    if (lv_display_flush_is_last(disp)) {
        present(px_map);
    }
    lv_display_flush_ready(disp);
}

void RgbPanel::present(uint8_t* fb) {
    if (now - _last_vsync_us > LCD_SWAP_DEADLINE_US) {   // 20 ms into a 30.6 ms frame
        xSemaphoreTake(_vsync, 0);
        wait_vsync();                                     // swap at the start of the next frame instead
    }
    esp_lcd_panel_draw_bitmap(_panel, 0, 0, 800, 480, fb);  // fb is a panel framebuffer: the driver swaps
    xSemaphoreTake(_vsync, 0);                            // drop a signal from before the swap
    wait_vsync();                                         // the DMA is on the new buffer from here
}
```

`_vsync` is a binary semaphore given from the panel's `on_vsync` callback with
`xSemaphoreGiveFromISR`; the callback also records the time. Waiting for it keeps LVGL from
drawing into the buffer that is still on screen. That holds only because of how the driver swaps,
read in ESP-IDF 5.5.3's `esp_lcd_panel_rgb.c` after the first display's artifacts (section 3):

- `esp_lcd_panel_draw_bitmap` with a panel framebuffer writes the cache back over the rows given
  (all 480: LVGL's copies between the buffers are not in the flushed areas) and points the last
  DMA node of every framebuffer at the new one. The swap takes effect at the end of the frame on
  screen, not at once.
- The DMA reads that last node's link a few lines before the frame ends. A swap made later shows
  the old buffer one frame more, while LVGL already draws into it. So a swap more than 20 ms
  after the last vblank waits for the next one first; the frame is 840 × 510 clocks, 30.6 ms at
  14 MHz.
- The panel starts on fb0, so LVGL is given fb1 as its first buffer: its first frame is drawn
  out of sight.

Direct mode itself is not what went wrong on the first display: `LV_DISPLAY_RENDER_MODE_FULL`
alternates the same two buffers and would have shown the same artifacts.

### Expander

Two 7-bit I2C addresses, as ThermostatDisplay and the vendor's example use them: write `0x01` to
address `0x24` once (outputs enabled), then write the output byte to address `0x38`. Every later
change is one write of the whole byte to `0x38`. `Ch422g` keeps the byte and offers
`set(bits, on)`.

**The I2C driver changes.** ThermostatDisplay uses the legacy `driver/i2c.h`. ESP-IDF 5.5
deprecates it and aborts at start-up when both drivers end up linked. SonosDisplay uses
`driver/i2c_master.h` throughout: one `i2c_new_master_bus` on port 0, one
`i2c_master_bus_add_device` for each of `0x24` and `0x38`, and the touch driver on the same bus
handle. ThermostatDisplay's I2C mutex is dropped, because all I2C happens on the UI task.

### Touch

`esp_lcd_new_panel_io_i2c(bus, &io_config, &io)` with `ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG()` and
`scl_speed_hz = 400000`, then `esp_lcd_touch_new_i2c_gt911` with `x_max = 800`, `y_max = 480`, no
swap, no mirror, and reset and interrupt pins unset. The controller's reset line is the
expander's bit 1, set high by the first expander write and never pulsed.

- **Address.** The GT911 answers on `0x5D` or `0x14` depending on its interrupt line at power-on.
  ThermostatDisplay assumed `0x5D`. `Touch` probes both with `i2c_master_probe`, every 10 ms for
  up to 500 ms. After a real power-on the expander's outputs start low, so the GT911 leaves reset
  only with the first expander write, just before the probe, and answers some tens of
  milliseconds later. A probe made once, at once, found nothing after a power-on, and the first
  display had no touch (section 3). After any other reset the expander keeps its outputs and the
  first probe answers.
- **Logged twice.** `Board::begin` runs before esp-mdm ships the log, so `Board::log_state`
  repeats the touch result from `do_ready`, where it reaches IoT Support: `Touch: GT911 at 0x5D`,
  or `Touch is not available: ...`.
- **Reading.** The LVGL read callback calls `esp_lcd_touch_read_data` and takes one point. A
  failed read reports "released" rather than the last coordinates.
- **Waking.** The callback asks `ScreenPower` first (section 3): a touch on a dark screen wakes
  it and never reaches the UI.

### Motion sensor

GPIO 6 as an input with the internal pull-down, interrupt on the rising edge: the sensor's output
is active high. ThermostatDisplay's interrupt handler posts a `std::function` to a FreeRTOS queue
with the blocking, non-ISR call, which is unsafe, and its "gpio_interrupt_task" receives nothing.
Neither is carried over. The new handler is `IRAM_ATTR` and only sets a `std::atomic<bool>`.
`MotionSensor::poll()`, called from the UI loop, returns true when the flag was set or the pin
reads high, so presence that lasts keeps the screen on.

### Not carried over

- `dithering.cpp`: it smoothed one generated gradient. The backend dithers the artwork, and the
  dark theme uses flat colours.
- ThermostatDisplay's own WiFi, MQTT, OTA, log and configuration classes: `esp-mdm` replaces
  them.
- The LVGL 8 fonts: regenerated (section 7).
- The Windows simulator (section 5).

### `sdkconfig.defaults`

After the three `esp-libs` defaults files, the repository's own:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y

CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536

CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="certs/homelab-root.pem"

CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000

CONFIG_LV_COLOR_DEPTH_16=y
CONFIG_LV_USE_CUSTOM_MALLOC=y
CONFIG_LV_USE_CLIB_STRING=y
CONFIG_LV_USE_CLIB_SPRINTF=y
CONFIG_LV_CACHE_DEF_SIZE=0
CONFIG_LV_USE_ASSERT_NULL=y
CONFIG_LV_USE_ASSERT_MALLOC=y
CONFIG_LV_USE_ASSERT_STYLE=y
CONFIG_LV_ASSERT_HANDLER_INCLUDE="../../../../main/lvgl_assert.h"
CONFIG_LV_USE_LOG=y
CONFIG_LV_LOG_LEVEL_WARN=y
CONFIG_LV_LOG_PRINTF=y
```

plus the four MQTT lines of section 1.

- **No `CONFIG_LV_CONF_MINIMAL`.** PaperClock starts from LVGL's minimal set and adds widgets
  one by one. A touch UI uses most of them, so SonosDisplay takes LVGL's defaults and changes
  only the lines above. `sim/lv_conf.h` then holds the same short list (section 5).
- **`CONFIG_SPIRAM_USE_MALLOC`** is what `esp-network-support` expects: it lets mbedTLS, WiFi and
  lwIP allocate from PSRAM. ThermostatDisplay had it off.
- **240 MHz**, where ThermostatDisplay ran at 160: software rendering of a scrolling list is the
  heaviest thing this firmware does. It raises the board's current draw a little, which matters
  for the brown-out quirk; section 3 names the fallback.
- **The PSRAM clock stays at 80 MHz.** The vendor's benchmark figure is for 120 MHz, which
  ESP-IDF 5.5.3 offers for octal PSRAM only as an experimental feature.

## 3. The two hardware quirks

### The picture drifts while flash is written

**What it is.** The panel's pixels are fetched from the PSRAM framebuffer by DMA, continuously.
A flash write holds up PSRAM access for a moment, the DMA falls behind the pixel clock, and from
then on the picture is shifted until the transfer is restarted.

**What ThermostatDisplay did.** Three things were in effect: the 14 MHz clock, code and
read-only data moved to PSRAM (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA`),
and `esp_lcd_rgb_panel_restart()` every time the screen was switched on, "preventing screen drift
from becoming permanent". The fourth, switching the screen off when an update starts, was never
wired: `OTAManager` declares the callback and nothing calls it.

**What SonosDisplay does.**

1. **Keeps all three.** `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` is ESP-IDF 5.5's switch for both
   options; its help text says code "that need to be executing during Flash operation can
   continue working normally".
2. **Does not set `CONFIG_LCD_RGB_RESTART_IN_VSYNC`.** The skeleton set it, to make a drift last
   one frame instead of until the next wake, and the first display showed artifacts with it
   ("What the first display showed", below). A drift now lasts until the screen is next switched
   on, as it did on ThermostatDisplay.
3. **Adds no hook for updates, and changes nothing in `esp-libs`.** In esp-mdm, a firmware
   update is downloaded and written only during boot, synchronously, on the task that calls
   `process()`, and it ends in `esp_restart()`. So while flash is written the UI task is blocked,
   the screen holds the loading view, and the restart that follows re-initialises the panel.
   There is no running screen to protect.

The other flash writes are short: the speaker choice saved to NVS, esp-mdm storing new
provisioning data (followed by a restart), and a coredump being erased after its upload at boot.
Item 1 covers them, and the restart on the next wake puts right what it does not.

The loading view cannot animate during an update, since its task is blocked. It shows "Checking
for an update" as a still frame (section 1, step 3).

### What the first display showed

The skeleton, flashed on 2026-11-15, showed screen update artifacts. The cause is in ESP-IDF
5.5.3's RGB driver, not in flash or PSRAM traffic:

- **On the S3 a restart always scans out fb0.** A hardware workaround (`RGB_LCD_NEEDS_SEPARATE_RESTART_LINK`)
  restarts the DMA through a one-node "restart link" that is mounted on `fbs[0]` when the panel
  is created and never moved. The transfer follows the current buffer only through the link at
  the end of a frame.
- **`CONFIG_LCD_RGB_RESTART_IN_VSYNC` restarts on every vblank**, so it never gets there. The
  panel showed fb0 always. LVGL alternates the two buffers, so every second update was drawn
  straight into the visible buffer, and every other one was never shown until the next update
  copied it across. ESP-IDF's FAQ calls the option a last resort, "Not Recommended", and its
  own RGB example does not set it.
- **ThermostatDisplay never set it.** Its only restart was the one on waking, which costs one
  frame from fb0 and then follows the current buffer.

So the option is off, and the restart on wake remains. `RgbPanel::restart` waits out the frame
from fb0 before it returns, so the backlight comes on over the current picture. The swap itself
is made safe in section 2.

**Then the drift showed.** With that update installed, the operator switched the display on and
saw the picture shifted, until the screen went dark and woke: "It's very familiar." This is the
drift the restart in every vblank had hidden, and startup is where it comes from. Startup reads
and writes flash: WiFi's and the PHY's data in NVS, the update check, the core dump check. Flash
and PSRAM share the bus, so each access holds up the panel's DMA, and the FIFO between DMA and
panel holds 16 pixels, about a microsecond at 14 MHz. So `do_ready`, once startup is over, calls
`Board::resync_panel` and restarts the panel once. If the frame from fb0 is one update old, it
shows for 30 ms while the loading view gives way to the next view.

What would come next if the picture drifts in use (shifted sideways until the next wake): bounce
buffers. The setting is `bounce_buffer_size_px = 10 * 800`, with `CONFIG_LCD_RGB_ISR_IRAM_SAFE`,
so the refill also runs while flash is accessed, and `CONFIG_ESP32S3_DATA_CACHE_LINE_64B`, which
ESP-IDF's FAQ asks for in this mode. The panel's DMA then reads ten lines of internal RAM, 600 µs
of slack instead of a microsecond, and the driver restarts by itself when a frame
comes up short. The costs:

- 32 KB of internal RAM;
- an interrupt that copies 25 MB a second out of PSRAM, a third or more of core 0;
- the frame-complete callback in place of the vblank as the sign that a swap took effect.

The firmware itself writes flash only when the speaker choice is saved, so drift in use is not
expected.

### Switching the backlight too fast browns the board out

**What ThermostatDisplay did.** A change of the backlight is refused within 1000 ms of the
previous change, in either direction, and retried on the next loop: "This is to mitigate
brownouts". After a brown-out or watchdog reset it starts with the backlight off. An experiment
that lowered WiFi power around the switch was added and reverted a day later, with no reason
recorded.

**What SonosDisplay does.** `ScreenPower` is the only code that writes the backlight bit. It
runs on the UI task, from `board.process()`:

- **States:** `Off`, `On`, and `WaitRelease` (lit by a touch that is still down).
- **The interval:** at least 1000 ms between two switches of the backlight. A request inside the
  interval stays pending and is applied when it ends. The first switch after boot is not delayed.
- **Switching on:** `RgbPanel::restart()` first, which returns once the restarted frame is past
  (section 3, up to two frames), then the expander write.
- **Switching off:** the expander write only. The panel keeps scanning and LVGL keeps drawing.
- **Wake:** motion, or a touch. A touch on a dark screen wakes it and is swallowed until the
  finger lifts, so it cannot press what was under it.
- **Sleep:** `screenOffSeconds` after the last touch or motion. The default is 60; the device
  configuration can set it (section 4). ThermostatDisplay's 20 seconds was a build setting.
- **Boot:** the backlight comes on with the loading view only when `esp_reset_reason()` is
  `ESP_RST_POWERON`. After any other reset the screen stays dark until motion or a touch. A
  display that restarts at night because WiFi dropped then stays dark, a brown-out cannot loop
  through the backlight, and the screen is still "off until the motion sensor or a touch wakes
  it".

The WiFi power experiment is not carried over. The brown-out detector stays at ESP-IDF's default.
esp-mdm logs the reset reason at every start, and it reaches IoT Support's device log. If
brown-out resets show up there, the first change is `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y`,
ThermostatDisplay's setting.

## 4. The backend: HTTP, the event stream, and its address

### Where the code lives: in the app

`esp-libs` has no HTTP client to reuse. It has two helpers that read an already opened response
into a `std::string` (`http_support.h` in `esp-network-support`), five hand-written request
sequences inside esp-mdm, and nothing that streams. SonosDisplay is the first firmware with an
HTTP backend, so a shared component would be designed from one user.

The code therefore goes into `main/backend/`, with one rule that keeps the door open:
`HttpClient` and `EventStream` include nothing from `ui/` or `Application`. They take a base URL,
strings and callbacks, and depend only on `esp_http_client`, FreeRTOS and `esp-support`. When a
second firmware needs them, the two classes move to `esp-libs` as they are. That move changes
another project's repository and is the operator's to rule then.

### Requests

The backend is unauthenticated and on the local network, so requests carry no token. Both
schemes work: the client is configured with `crt_bundle_attach = esp_crt_bundle_attach`.

**The deployed backend is HTTPS only**, found at the first display's bring-up on 2026-11-15: its
ingress answers plain HTTP with a 301 to HTTPS, and its certificate is issued by the homelab CA,
which Mozilla's bundle does not hold. Every connection then failed the handshake
(`esp-x509-crt-bundle: No matching trusted root certificate found`), and the screen stayed at
"Connecting". The firmware therefore adds the homelab root, `certs/homelab-root.pem`, to the
bundle (`CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE`), so `backendUrl` is `https://`. The file is a
root certificate, public by nature, and holds no hostname or address; it is valid until 2036.
Every client in the firmware verifies against the same bundle, esp-mdm's included.

```cpp
struct HttpRequest {
    esp_http_client_method_t method;
    std::string path;                    // "/api/..." appended to the base URL
    std::string body;                    // JSON for POST/PUT/DELETE, else empty
    size_t max_response = 256 * 1024;
};
struct HttpResponse {
    esp_err_t err;                       // transport error, or ESP_OK
    int status;
    uint8_t* body; size_t length;        // in PSRAM; the receiver frees it or keeps it
};
void HttpClient::send(HttpRequest request, std::weak_ptr<void> scope,
                      std::function<void(HttpResponse&&)> done);
```

- **One task per client, one request at a time.** `send` puts the request on the client's
  FreeRTOS queue (16 deep). The task keeps one `esp_http_client` handle with keep-alive and
  reuses it; after a transport error it destroys the handle and makes a new one. A connection idle
  for more than 1.5 seconds is closed before the next request: a server that closed it first would
  fail that request, and only a GET is retried.
- **Two clients.** `backend_api` carries API calls and `backend_art` carries artwork, so a
  command is never queued behind twenty covers.
- **The body goes to PSRAM.** The task reads with `esp_http_client_open`,
  `esp_http_client_fetch_headers` and a read loop into a `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
  buffer sized from `Content-Length`, growing when there is none, and failing with
  `ESP_ERR_INVALID_SIZE` beyond `max_response`. Artwork requests set `max_response` to 512 KB.
- **The callback runs on the UI task**, through `get_queue().enqueue(...)`.
- **`scope`** is a weak reference the caller owns, in practice the view that asked. If it has
  expired when the request is about to start or the response arrives, the request is dropped and
  the body freed. A closed view therefore neither receives late data nor leaves artwork
  downloads behind it.
- **Timeouts and retries:** 5 seconds. A GET is retried once after a transport error; nothing
  else is retried, since a repeated "next" is a different command.
- **Errors:** a status of 400 or above carries the template's envelope,
  `{"error": ..., "details": {"message": ...}, "code": ...}`. The client hands status and body to
  the caller; the UI shows `error`.

`cJSON_InitHooks` is set at start-up to allocate from PSRAM (section 7).

### The event stream

The backend template serves its stream itself, in-process, at
`GET /api/sse/stream?request_id=<id>`: plain HTTP/1.1, no `Content-Length`, no `id:` or `retry:`
fields, no replay. `EventStream` owns the `backend_events` task and a second `esp_http_client`:

1. **Identity.** `request_id` is 32 hex characters from `esp_random()`, made once per boot and
   reused on every reconnect. The server closes an older stream with the same id.
2. **Connect.** GET with `Accept: text/event-stream` and `timeout_ms = 10000`, then
   `esp_http_client_open(client, 0)` and `esp_http_client_fetch_headers`. Anything but status 200
   counts as a failed attempt. Once the headers are in, the timeout drops to 200 ms with
   `esp_http_client_set_timeout_ms`: `esp_http_client_read` returns only when its buffer is full or
   its timeout passes, so at 10 seconds a small frame would wait up to 10 seconds.
3. **Read.** `esp_http_client_read(client, buf, 1024)` in a loop. It decodes chunked transfer
   itself. A positive result goes to the parser and resets the idle clock.
   `-ESP_ERR_HTTP_EAGAIN` means the read timeout passed without data, which is normal. Zero or any
   other negative result ends the connection.
4. **Idle limit.** The server writes a `: heartbeat` comment every 30 seconds in production. With
   no byte for 75 seconds the connection is treated as dead and closed.
5. **Parse.** Lines end in `\n`, with a `\r` before it stripped. A line starting with `:` is a
   comment. `event:` sets the frame's name, `data:` appends to its data, an empty line ends the
   frame. A frame over 64 KB is dropped.
6. **Dispatch.**
   - `event: ready` is the server's first frame: the stream counts as connected from here, not
     from the HTTP 200, because a refused stream also opens with 200.
   - `event: rejected` is a refusal: log it and wait 60 seconds.
   - A frame with no name is a domain event, `{"type": "<name>", "payload": {...}}`. It is parsed
     and handed to the UI task as `(type, payload)`.
   - The template's own `version` event is ignored.
7. **Reconnect** after 1, 2, 4, 8, then 15 seconds, reset by `ready`.
8. **After every `ready`** the app is told the stream is (re)connected. It renews whatever
   subscriptions the API defines, since they end with the connection, and refetches the state it
   shows, since events sent while it was away are gone.

The UI works without the stream: commands go over `HttpClient` either way. While the stream has
been down for more than five seconds the Shell shows a "Reconnecting" marker. The backend being
unreachable never restarts the device.

`EventStream` stays connected while the screen is dark, so the state is current the moment it
wakes. Artwork is the exception: the app does not fetch it while the screen is dark.

### Where the backend's address comes from: IoT Support's device configuration

The firmware reads `backendUrl` from the configuration JSON that esp-mdm fetches at boot, in
`do_configuration_loaded`. esp-mdm passes the whole object to that hook and ignores keys it does
not know, so this needs no change to esp-mdm or IoT Support.

A build setting is rejected:

- the repository is public, and the address is the operator's network;
- a second display elsewhere, or a backend that moves, would mean a new firmware;
- IoT Support already has the mechanism: saving a device's configuration publishes a message on
  which esp-mdm restarts the device, so a changed address takes effect within seconds.

| Key | Type | Meaning |
|---|---|---|
| `deviceName`, `deviceEntityId` | string | required by esp-mdm |
| `enableOTA` | boolean | esp-mdm: update over the air |
| `backendUrl` | string | the backend's base URL: scheme, host and optional port, no path, no trailing slash |
| `screenOffSeconds` | integer, optional | seconds until the screen goes dark; 60 when absent |

IoT Support stores configuration per device and has no defaults per model, so every display's
JSON carries `backendUrl`, and the model's schema requires it (section 6). If the key is missing
or malformed all the same, the firmware does not crash: after `do_ready` it shows "No backend
address in this display's IoT Support configuration" and starts no backend task.

**The speaker choice** is the one thing stored on the display: NVS namespace `sonos`, key
`speaker`, a string holding the speaker's id as the API gives it. It must not go into esp-mdm's
namespace `prov`, whose unknown keys are deleted whenever provisioning data is renewed. Provision
device rewrites the whole `nvs` partition, so a display provisioned again over USB asks for its
speaker again.

### What this asks of the API design (task 6)

These are the firmware's constraints. The endpoints' shapes are task 6's.

- **Artwork** in the device form is LVGL 9's binary image: the 12-byte header (magic `0x19`,
  colour format `0x12` RGB565, flags 0, width, height, stride) followed by the pixels,
  little-endian, uncompressed, no alpha, already dithered, at exactly the requested square size,
  with `Content-Length` set. IoT Support's `LVGLImage.py` writes this form. The sizes are 400, 160
  and 80 pixels (section 7).
- **Text** in Unicode NFC. The fonts hold composed characters, so a decomposed accent would be
  drawn beside its letter.
- **Lists** are paged; the template's paging allows at most 100 items per page, and the firmware
  asks for 100.
- **The queue** needs a way to ask for the page that holds the current track.
- **Events** are small and name what changed; the firmware fetches the state itself.

## 5. Checking screens without a device

### The decision

`sim/` is a native program, `render`, that links the firmware's own `main/ui/` sources against
LVGL 9.2.2 and writes each screen to an 800×480 PNG. It is built and run in the environment by
`kc project test`. The Windows simulator is not carried over: PaperClock's no longer builds (its
project still compiles files the firmware deleted), it needs Visual Studio, and it draws into a
window, where the aim here is an image a session can produce, read and attach.

### The rule that makes it possible

**`main/ui/` includes LVGL and the C++ standard library, and nothing else.** No ESP-IDF header,
no FreeRTOS, no `esp-libs`, no `Application.h`. It has two doors to the rest of the firmware,
both headers in `ui/`:

- **`Model.h`**: plain structs holding what the views draw, such as the loading state now and the
  now-playing state and list items from milestone 1.
- **`UiHost.h`**: an abstract class with what a view may ask for, such as a command to the
  backend, a piece of artwork, a restart. `Application` implements it on the device; `sim/`
  implements it with fixtures.

`ui/log.h` gives the views `UI_LOGI`, `UI_LOGW` and `UI_LOGE`: `ESP_LOGx` when `ESP_PLATFORM` is
defined, `printf` otherwise. The rule is enforced by the build: an ESP-IDF include in `ui/` fails
`kc project test`.

### The UI's structure

- **One LVGL screen.** `Shell` owns it: a content area, and from milestone 1 the now-playing bar
  under it, which is how the bar "stays at the bottom of every screen".
- **Views** are what PaperClock calls a UI: a class deriving from `View`, with `do_render(parent)`
  building its objects inside the content area and `do_update()` called from the loop. `Shell`
  keeps a stack of them. Going back destroys the view that is left, so memory is bounded by the
  depth of the stack, not by what was visited.
- **`Theme`** sets up the dark theme once, with `lv_theme_default_init(disp, primary, secondary,
  true, font)`, and holds the palette and the type scale as named constants. Starting values:
  background `#101214`, surface `#1C1F24`, text `#F2F2F2`, secondary text `#A0A6AD`, accent
  `#E8A33D`. The milestones' slices own the look and change these freely.
- **`Messages.h`** holds every string the UI shows, in English, one macro each, as PaperClock
  does.

### The build

```
sim/
  CMakeLists.txt    C++20; FetchContent of lvgl at tag v9.2.2; globs ../main/ui/*.cpp and ../main/ui/fonts/*.c
  lv_conf.h         LVGL's template with the same changes as sdkconfig.defaults, plus LV_USE_LODEPNG 1
  main.cpp          display, tick, scene loop, PNG writer
  SimHost.{h,cpp}   UiHost on fixtures
  scenes.cpp        the scenes
```

- **Display:** `lv_display_create(800, 480)` with one full-frame RGB565 buffer in direct mode and
  a flush callback that only calls `lv_display_flush_ready`.
- **Time:** `lv_tick_set_cb` returns a counter the program advances itself, so a scene renders
  the same on every run.
- **A scene** is a name and a function that builds a view from fixture data, for example
  `loading`, `loading-update`, `placeholder`, and later `now-playing`, `now-playing-radio`,
  `artists`, `queue-empty`. One view has several scenes where its states differ.
- **Rendering a scene:** build it, advance the clock by 500 ms in 10 ms steps calling
  `lv_timer_handler()`, call `lv_refr_now(disp)`, convert the buffer to 24-bit RGB, encode it with
  `lodepng_encode24` and write the file with stdio. LVGL bundles lodepng with its encoder compiled
  in, so no other library is needed; its `lodepng_encode24_file` writes through `lv_fs`, which has
  no driver here.
- **Artwork in scenes** is generated by `SimHost`: a deterministic two-colour pattern at the
  requested size. No image files are committed.
- **Use:** `render --all --out <dir>`, or `render <scene> --out <dir>`. It exits non-zero when a
  scene is unknown, an LVGL assertion fires, or LVGL logs a warning or an error.

`lv_conf.h` and `sdkconfig.defaults` configure the same LVGL twice. Because both start from
LVGL's defaults and differ only in the short list of section 2, the rule is: an LVGL option
changed in one is changed in the other in the same commit. Two of LVGL's ESP-IDF Kconfig defaults
differ from its `lv_conf_template.h`, so `lv_conf.h` pins the device's values:
`LV_COLOR_MIX_ROUND_OFS 128` and `LV_TXT_BREAK_CHARS`. What differs on purpose: the simulator uses
the C library's allocator where the device uses PSRAM, which changes no pixel; an assertion fails
the render instead of restarting; and LVGL's log goes through a callback that counts warnings,
with `LV_LOG_PRINTF` off, since LVGL 9.2.2's `lv_log_add` reuses its `va_list` after `printf` when
both are on.

### In the environment and in the workflow

`.kubecoder/project.yaml`:

- `build`: `cexec esp-idf idf.py build`
- `test`: `cexec native sh -c 'cmake -S sim -B build-sim -G Ninja && cmake --build build-sim && build-sim/render --all --out build-sim/screens'`
- `lint`: `clang-format --dry-run --Werror` over `main/` and `sim/`, leaving out `main/ui/fonts/`,
  in the `native` sidecar, as PaperClock does.

`.kubecoder/config.yaml` declares `esp-libs` as a shared repository beside this one, and the
toolchains `esp-idf`, `native` and `frontend`. The last is only for regenerating fonts
(section 7).

A slice that changes a screen runs the test, reads the PNGs under `build-sim/screens/`, and
attaches the ones that changed to what it puts to the operator. `build-sim/` is ignored by git.
Jenkins does not build `sim/`: its firmware job has the ESP-IDF container only, and the dev
workflow runs `kc project test` on every slice.

**What it does not show:** timing, scrolling speed, touch, the panel's colours, memory use. Those
are seen on the display.

## 6. Registering the model and provisioning a display

### What IoT Support needs

- **The model's code is `sonos_display`.** IoT Support takes the code from the CMake project
  name when firmware is uploaded, and codes match `^[a-z0-9_]+$`. So the top-level
  `CMakeLists.txt` says `project(sonos_display)`, and the image is `build/sonos_display.bin`.
- **The model is created by hand, before the first upload.** A model is made in IoT Support's UI
  (Device models, New) or through its admin API. An upload for an unknown code is refused with a
  404; it does not create the model. So the Jenkins job's Deploy stage fails until the model
  exists.
- **The model's configuration schema**, pasted into the model's editor:

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "type": "object",
  "properties": {
    "deviceName": { "type": "string", "minLength": 1 },
    "deviceEntityId": { "type": "string", "minLength": 1 },
    "enableOTA": { "type": "boolean" },
    "backendUrl": { "type": "string", "pattern": "^https?://[^/]+$" },
    "screenOffSeconds": { "type": "integer", "minimum": 10, "maximum": 3600 }
  },
  "required": ["deviceName", "deviceEntityId", "enableOTA", "backendUrl"],
  "additionalProperties": false
}
```

- **Firmware versions.** IoT Support reads the version from the image itself, which is
  `PROJECT_VER`, the short git hash. It keeps one current version per model and tells every
  display of the model to restart when a new one is uploaded.

### Jenkins and `scripts/upload.sh`

The job is `Firmware/SonosDisplay`, created through the Jenkins API with its push trigger, as the
pipelines guide's rules for a new job say. Its `Jenkinsfile` is PaperClock's with the repository's
name changed: it checks out SonosDisplay and `esp-libs` side by side, then calls
`espFirmware.build(dir: 'SonosDisplay')` and `espFirmware.upload(dir: 'SonosDisplay')`.

`espFirmware.upload` runs the repository's `scripts/upload.sh` with IoT Support's address as its
argument. In PaperClock that script only fetches the upload script IoT Support itself serves, at
`/api/pipeline/upload.sh`, and runs it; the served script is what collects the build's files and
posts them. SonosDisplay's is the same, without a default address, since the repository is
public:

```sh
#!/bin/sh
set -e
cd "$(dirname "$0")/.."
HOST=${1:?usage: scripts/upload.sh <IoT Support base URL>}
curl -fsSL "$HOST/api/pipeline/upload.sh" | sh -s --
```

**What Flash device needs, and how it gets it.** Today the served script sends five files:
the app image, the ELF, the map, `sdkconfig` and a `version.json`. IoT Support refuses a bundle
with any other file in it. A blank board also needs the bootloader, the partition table and the
initial OTA data, with their offsets. All of them are ordinary outputs of `idf.py build`, in the
directory the served script already reads:

| File | Offset on this board |
|---|---|
| `build/bootloader/bootloader.bin` | `0x0` |
| `build/partition_table/partition-table.bin` | `0x8000` |
| `build/ota_data_initial.bin` | `0xd000` |
| `build/sonos_display.bin` | `0x10000` |
| `build/flasher_args.json` | names the four with these offsets, and the flash mode, frequency and size |

So SonosDisplay sends what Flash device needs by staying on the served script: when the Flash
device slice extends that script and IoT Support's bundle check, this repository changes nothing.
The project's brief describes each repository's `upload.sh` as building the bundle. It is the
served script that does, which puts the change with the Flash device slice. If that slice settles
on a different hand-over, task 5 follows it.

### Bringing up a fresh display

Task 5 registers these as the operator's steps. Step 1 can be done at once. Step 3 waits for the
architecture changes below, and steps 4 and 5 wait for the Flash device slice.

1. **Create the model** in IoT Support: code `sonos_display`, name `Sonos Display`, the schema
   above. This comes before the Jenkins job's first build can pass its Deploy stage.
2. **The first build** of `Firmware/SonosDisplay` uploads the firmware. This step is automatic.
3. **Create the device** under that model, with its configuration: `deviceName`,
   `deviceEntityId`, `enableOTA: true`, and `backendUrl` set to SonosController's address. IoT
   Support creates the device's Keycloak client.
4. **Flash device**, from a desktop Chromium browser, with the display on the USB-C port marked
   UART. The board has an automatic download circuit, so no button is held.
5. **Provision device**, on the device's page. It writes the `nvs` partition with the device's
   credentials and the WiFi, MQTT and IoT Support addresses. It reads the partition table from
   the board, which is why it comes after Flash device. Then reset the display.
6. **Check:** the device's Logs tab in IoT Support shows the boot log, and the screen shows the
   placeholder view.

Until Flash device exists, the same first flash is `idf.py -p <port> flash` from a workstation
with ESP-IDF 5.5.3.

### The architecture model

Every firmware in IoT Support is also an architecture producer: PaperClock has
`docs/architecture/architecture.yaml`, a `Jenkinsfile.architecture` and the job `AaC/PaperClock`.
This is not optional for SonosDisplay. IoT Support's own architecture job reads the registered
devices and stops at one whose model it does not know; its generator says "add its firmware
product UUID", and it also stops at a UUID the published architecture does not hold. From the
moment the first display is registered in step 3, that job fails unless the changes below are in
place.

**In SonosDisplay,** after PaperClock: the model under `docs/architecture/` with the producer id
`sonos-display` and a firmware product whose UUID is minted once, `Jenkinsfile.architecture`, the
job `AaC/SonosDisplay`, and the `aac-tools` toolchain with its `arch-validate` lint line.

**In three other repositories.** Changes to another project's repository are the operator's to
rule. Asked on 2026-11-15, they answered "Task 5 makes them": three small commits, with each push
asked for when it comes. In the order they have to land:

1. `pvginkel/Architecture`, `pipeline-producers.yaml`: an entry for the producer `sonos-display`,
   repository `pvginkel/SonosDisplay`, job `AaC/SonosDisplay`, after that job's first green
   build. The collector fails on a registered producer that has no artifact yet. PaperClock's
   entry is the model.
2. `pvginkel/IoTSupport`, `backend/docs/architecture/firmware-products.yaml`: the line
   `sonos_display: <the firmware product's UUID>`. It must be in before step 3 of the bring-up,
   the device's creation.
3. `pvginkel/JenkinsPipelineUtils`, `docs/pages/guide/job-properties.md`: the row that names "the
   eight ESP-IDF firmware jobs" gains `Firmware/SonosDisplay`.

None of the three pushes is pre-approved, and task 5 names each one when it registers its work.
The push to IoTSupport runs its pipeline, which tests it, builds its images and deploys them to
prd. The push to Architecture runs the collector, and the push to JenkinsPipelineUtils publishes
the pipelines guide.

## 7. The memory budget

### Where memory comes from

The module has about 512 KB of internal RAM and 8 MiB of PSRAM. Internal RAM is the scarce one:
WiFi, the task stacks and every small allocation live there. So everything large or numerous goes
to PSRAM, by these four mechanisms:

- **The framebuffers**, through the panel's `fb_in_psram`.
- **LVGL's heap**, through `main/board/lv_mem_psram.c`. With `CONFIG_LV_USE_CUSTOM_MALLOC=y`,
  LVGL calls `lv_malloc_core`, `lv_realloc_core` and `lv_free_core`, which this file implements
  with `heap_caps_malloc_prefer(size, 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_INTERNAL
  | MALLOC_CAP_8BIT)` and its realloc and free counterparts. The remaining functions of that
  interface (`lv_mem_init`, `lv_mem_deinit`, `lv_mem_add_pool`, `lv_mem_remove_pool`,
  `lv_mem_monitor_core`, `lv_mem_test_core`) are the empty ones of LVGL's own
  `src/stdlib/clib/lv_mem_core_clib.c`. PaperClock lets LVGL use plain `malloc`, which puts every
  small widget allocation in internal RAM; a list of 200 rows would take most of what is free.
- **Response bodies and artwork**, through `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` in
  `HttpClient`.
- **JSON trees**, through `cJSON_InitHooks` with the same allocator, set once in `app_main`.

Plain `malloc` and `new` follow ESP-IDF's rule: under 4 KB internal first, larger from PSRAM.

### PSRAM

| Use | Budget | Ground |
|---|---|---|
| The firmware's code and constants | 2.5 MiB | `CONFIG_SPIRAM_XIP_FROM_PSRAM` copies them to PSRAM at boot; this is the price of section 3's drift measure. ThermostatDisplay's image was 1.4 MB. |
| Two framebuffers | 1.46 MiB | 2 × 800 × 480 × 2 bytes |
| Artwork cache | 1.5 MiB | cap, below |
| LVGL's heap | 0.75 MiB | 200 rows at about 1 KB each, plus the rest of the UI |
| Responses and JSON in flight | 0.5 MiB | a 256 KB body and its tree |
| WiFi, lwIP, TLS, MQTT, logs | 0.25 MiB | |
| **Total** | **about 7 MiB** | 1 MiB is left |

The build must fail when the app image grows beyond 2.5 MiB, well before the 3 MiB slot is full:
a post-build step in `CMakeLists.txt` that checks the size of `sonos_display.bin`.

### Internal RAM

Task stacks take about 75 KB: the four tasks of section 1, esp-mdm's, WiFi's and the system's.
The model of the open list takes up to 40 KB. The target is at least 60 KB of internal RAM free
with the largest list open. The firmware logs `heap_caps_get_free_size` and
`heap_caps_get_minimum_free_size` for both kinds of memory once a minute; the lines reach IoT
Support's device log, where every milestone can read them.

### Artwork

The display decodes nothing. An image arrives in the form section 4 names, is read straight into
its PSRAM buffer, checked (magic, colour format, width and height as requested, length equal to
12 + width × height × 2), and handed to LVGL as an `lv_image_dsc_t` pointing into that buffer.
`CONFIG_LV_CACHE_DEF_SIZE=0`: LVGL has nothing to decode or cache.

| Size | Used for | Bytes each |
|---|---|---|
| 400 × 400 | now playing | 320,000 |
| 160 × 160 | favourites tiles, album grids | 51,200 |
| 80 × 80 | list rows, the now-playing bar | 12,800 |

The cache holds images by artwork id and size, capped at 1.5 MiB in total. That is, for example,
two large images and sixteen tiles. An image shown by a live widget is pinned. When the cap is
reached the least recently used unpinned image is freed; when everything is pinned, the new image
is not loaded and its widget keeps the placeholder, a flat tile with an icon. A view asks only
for what is visible, and its requests die with it (the `scope` of section 4). Nothing is fetched
while the screen is dark.

### Lists

- **At most 200 rows are built at a time.** The firmware asks for pages of 100 and keeps two.
  Scrolling past an end fetches the next page and drops the far one.
- **The library fits whole.** 94 artists, an artist's albums and an album's tracks are each under
  200, so the A–Z bar jumps within rows that are already there.
- **The queue and Plex's large playlists do not fit**, one of them at 12,019 tracks. They are
  windowed the same way, and the queue opens on the page holding the current track.
- **A response body is capped at 256 KB**, about ten times a page of 100 items.

### Fonts

**Pre-rendered bitmap fonts**, not PaperClock's run-time TrueType. A bitmap font costs flash and
no heap, draws without rasterising, and is the same bytes in the simulator and on the device.
The cost is a fixed set of sizes.

- **Faces:** Roboto Regular and Medium for text, Material Design Icons for icons. Both are
  Apache-2.0, and their licence files sit beside the TTFs in `tools/fonts/`. PaperClock's
  FontAwesome files are not copied into this repository.
- **Characters:** Basic Latin `0x20-0x7E`, Latin-1 Supplement `0xA0-0xFF`, Latin Extended-A
  `0x100-0x17F`, and the punctuation `0x2013-0x2014`, `0x2018-0x201D`, `0x2022`, `0x2026`,
  `0x20AC`: about 335 glyphs, which is "Latin script with accents". Anything else is drawn as
  LVGL's placeholder box.
- **Sizes.** The panel is about 217 pixels per inch and is read from arm's length in a workshop,
  so the scale is larger than a phone's: Regular 24 and 32, Medium 32 and 40, icons 40 and 64.
  A slice that needs another size adds a line to the generator.
- **Format:** 4 bits per pixel, uncompressed, one `.c` file per face and size in
  `main/ui/fonts/`, named for its symbol, such as `font_regular_32.c`. `icons.h` holds a `#define`
  per icon codepoint.
- **Cost:** about 35 KB for a text font at 24, 60 KB at 32 and 95 KB at 40, so about 250 KB for
  the four, and under 50 KB for the icons.
- **Generation:** `tools/fonts/generate.sh` runs `npx lv_font_conv` (pinned to 1.5.3) once per
  face and size, with `--bpp 4 --no-compress --format lvgl` and the ranges above; LVGL's own
  built-in fonts are made this way. It runs in the `frontend` sidecar. The generated files are
  committed, so neither the firmware build nor `sim/` needs Node.

## To check on the first display

Each of these was designed from the sources before any display ran it, and the fallback is the
change to make if one fails. The first display, `sonos_display_garage`, was flashed and
provisioned on 2026-11-15; the results are from its log in IoT Support (Elasticsearch,
`logstash-http-*`) and from the operator's look at it.

| Check | Result on the first display | Fallback |
|---|---|---|
| Direct render mode shows no stale or torn areas when a view changes and when a list scrolls | Artifacts on screen. Their cause was the restart in every vblank, not direct mode (section 3); the swap is also made safe now (section 2). With that fixed, the picture drifted at power-on, until the next wake; startup now ends with a restart (section 3). Holds since `f2217d1`: the operator's "Works" after a power-on and a wake, 2026-11-15 | `LV_DISPLAY_RENDER_MODE_FULL`, ThermostatDisplay's mode; it would not have helped here. For drift in use: bounce buffers (section 3) |
| `CONFIG_LCD_RGB_RESTART_IN_VSYNC` gives a steady picture, and the picture is right again after an update was installed with the screen lit | Failed: on the S3 it pins the picture to fb0. Taken out | done: the restart on every wake remains |
| The new I2C driver reaches the expander at `0x24` and `0x38` and the touch controller at `0x5D` or `0x14` | The expander holds: the backlight is switched through it. The touch controller answered at bring-up, when the board had only been reset by esptool, but not after the operator's power-ons: it was probed before it left reset. It is now probed until it answers, and the result is logged where IoT Support gets it (section 2). Holds since `f2217d1`: `Touch: GT911 at 0x5D` after a power-on, and the operator's "Works" | none needed |
| Touch coordinates match what is drawn, corner to corner: `PlaceholderView` draws a dot where each touch lands | Holds: the operator's "Works" at bring-up | `swap_xy` or the mirror flags of the touch configuration |
| No brown-out reset in IoT Support's device log after a day of waking and sleeping at 240 MHz | One boot so far, `ESP_RST_POWERON`, then 7 minutes without a reset. The day is still to come | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y` |
| Free internal RAM at the placeholder view is above 100 KB | Holds: 144,735 bytes free with the event stream connected, at least 140,011 since boot | lower the UI task's stack to 12 KB; read the per-task high-water marks |
| An OTA update from IoT Support installs and the display comes back by itself | Holds: the display was off when `55f0ff2` was uploaded, and installed it when switched on. The install boot's own lines were not shipped before its restart, but the next boot runs `55f0ff2` with reset reason `ESP_RST_SW` and connects by itself | this is esp-mdm's path, unchanged from PaperClock |
| The event stream reaches the deployed backend (added at bring-up) | Failed at first: "Connecting" on screen. The backend answered, but its certificate is the homelab CA's, and every handshake failed. With that root trusted (section 4), `Event stream ready` 1.5 seconds after startup, on every boot since | `backendUrl` on plain HTTP needs the backend's ingress to stop redirecting to HTTPS |

Two more things from the first boot's log:

- **A core dump was uploaded at that boot**, from an earlier session whose log never reached IoT
  Support. The session between Flash device and Provision device fits: until provisioning, NVS
  holds no `prov` namespace, and esp-mdm's `ESP_ERROR_CHECK(_mdm_configuration.load())` aborts
  on every start. IoT Support parsed it (core dump 333), but its summary is in IoT Support's
  database only, so this is inferred, not read.
- **The log shows only from 3.9 seconds after boot**, when esp-mdm's log shipping starts. What a
  board check needs from earlier, such as the touch address, is logged again later or read over
  USB.

The event stream and the HTTP client are first exercised against a real backend in milestone 1.
What to watch there: that `esp_http_client_read` returns `-ESP_ERR_HTTP_EAGAIN` on a quiet stream
as its header documents, and that the 75-second idle limit holds against the deployed heartbeat.
