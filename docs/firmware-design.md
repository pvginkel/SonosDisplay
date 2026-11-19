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
| Layout | PaperClock's shape: `Application : ApplicationBase`, one UI task that drains esp-mdm's queue and runs LVGL, `esp-libs` as path dependencies. `main/` splits into `board/` (hardware), `backend/` (HTTP and events), `api/` (the backend's answers, read into the UI's types, and the bodies of the requests) and `ui/` (LVGL and standard C++ only). |
| Carried over | The panel, expander, touch, backlight and motion-sensor code of ThermostatDisplay's shipped build: two framebuffers in PSRAM at a 14 MHz pixel clock. Rewritten for LVGL 9.2 and for ESP-IDF's current I2C driver, not copied. |
| Picture drift | No change to `esp-libs`. The panel's DMA reads two bounce buffers in internal RAM, which interrupts on core 1, the UI task's, refill from the framebuffers, also while flash is written; the driver restarts a frame that comes up short. The panel also resynchronises whenever the screen is switched on, as ThermostatDisplay's did, and code runs from PSRAM. `CONFIG_LCD_RGB_RESTART_IN_VSYNC` stays off. An update is only written during boot, and the display restarts after it. |
| Brown-out | The backlight is switched at most once a second, in either direction, by one state machine. Boot lights the screen only after a power-on reset. |
| HTTP and events | Four small tasks in the app: one for presses, one for fetches, one for artwork, one holding the event stream. Results reach the UI task through esp-mdm's queue. The code lives in the app, `main/backend/`, written free of app types so it can move to `esp-libs` when a second firmware needs it. |
| Backend address | IoT Support's device configuration, key `backendUrl`, required by the model's schema. Not a build setting. |
| Screens without a device | `sim/`: a native build of `main/ui/` against LVGL 9.2.2 that renders every scene to an 800×480 PNG. It is the repository's `kc project test`. The Windows simulator is not carried over. |
| Model and provisioning | Model code `sonos_display`, which is also the CMake project name. The operator creates the model and the device in IoT Support, then runs Flash device and Provision device. `scripts/upload.sh` is the two-line fetcher of the script IoT Support serves. |
| Memory | Everything large is in PSRAM: framebuffers 1.5 MB, LVGL's heap, an artwork cache capped at 1.5 MiB, response bodies. Behind the artwork cache, the board's TF card keeps the covers downloaded, across restarts. At most 200 list rows are built at a time. Roboto as pre-rendered bitmap fonts for Basic Latin, Latin-1, Latin Extended-A and the punctuation and signs the library's titles hold. |

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
certs/homelab-root.pem      the root the backend's certificate is verified against (section 4)
main/
  CMakeLists.txt  idf_component.yml  .clang-format  lvgl_assert.h
  main.cpp                  app_main: Board, Application, idle loop
  Application.{h,cpp}       : ApplicationBase; owns the UI task and the Shell; implements UiHost
  CardTask.{h,cpp}          the TF card's task, the artwork loader's card (section 7)
  board/                    ESP-IDF and LVGL's driver interface; nothing above it
    BoardConfig.h           every pin, address and timing of this note, as constants
    Board.{h,cpp}           begin(), process(); owns the parts below
    RgbPanel.{h,cpp}        esp_lcd RGB panel, the LVGL display and its flush
    Ch422g.{h,cpp}          the I/O expander
    Touch.{h,cpp}           GT911 and the LVGL input device
    ScreenPower.{h,cpp}     backlight, wake and sleep (section 3)
    MotionSensor.{h,cpp}
    RunMark.{h,cpp}         how far a run got, kept across a reset (section 1)
    TfCard.{h,cpp}          the TF card's SPI bus and its FAT at /sd (section 7)
    lv_mem_psram.c          LVGL's allocator on PSRAM (section 7)
  backend/                  ESP-IDF only; no LVGL, no app types
    HttpClient.{h,cpp}  EventStream.{h,cpp}
  api/                      cJSON, LVGL, standard C++ and ui/'s two doors only; sim/ builds it too
    Answers.{h,cpp}         the backend's answers, checked and read into the model
    Requests.{h,cpp}        the bodies of the requests the firmware sends
    Artwork.{h,cpp}         the artwork's checks, its cache and its loader (section 7)
    ArtworkStore.{h,cpp}    the pictures kept in a folder, the TF card's on the device (section 7)
  ui/                       LVGL and standard C++ only
    UiHost.h  Model.h       the only two doors between ui/ and the rest
    Shell.{h,cpp}  View.{h,cpp}  Theme.{h,cpp}  Messages.h  lv_support.{h,cpp}  log.h
    LoadingView.{h,cpp}  SpeakerPickerView.{h,cpp}  NowPlayingView.{h,cpp}
    ListView.{h,cpp}        the paged list Home, every library screen and the queue derive from
    HomeView  ArtistsView  AlbumsView  TracksView  PlaylistsView  QueueView   each .{h,cpp}
    ItemPresses.{h,cpp}  PressButton.{h,cpp}   an item's Play, Shuffle, Add, toggle; a track's line
    NowPlayingBar.{h,cpp}   the bar under the content area
    Cover.{h,cpp}  Controls.{h,cpp}  shared by the now-playing screen and the bar
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
| `backend_api` | 6144 | 1 | 0 | fetches of the backend's state and lists, one at a time |
| `backend_press` | 6144 | 1 | 0 | presses: commands and changes the operator makes, one at a time, in order |
| `backend_art` | 6144 | 1 | 0 | artwork downloads, one at a time |
| `backend_events` | 6144 | 1 | 0 | the event stream |
| `card` | 8192, in PSRAM | 1 | 0 | the TF card: its mount, then the artwork's reads, writes and erases, in the order they were asked for (section 7) |

esp-mdm's own tasks (log shipping, MQTT) are as it creates them. The UI task's stack is 16 KB
because esp-mdm's HTTPS login and its update download run on whichever task calls `process()`.
The card's stack is in PSRAM, where internal RAM has no room for it, and a task with its stack
there may not write flash, which ESP-IDF asserts: the card's task uses no NVS.

The UI loop sleeps for what `lv_timer_handler()` returns, clamped to 1 to 10 ms.
`CONFIG_FREERTOS_HZ=1000` makes that delay real.

1. `app_main` constructs `Board` and calls `board.begin()`: panel, I2C bus, expander, touch,
   `lv_init()`, the LVGL display and input device. Then `Application(&board).begin()`, then it
   idles.
2. `do_begin()` starts the card's task, which mounts the TF card while the boot goes on
   (section 7), then creates the UI task. It reads the speaker choice and the motion setting
   from NVS (section 4), then builds the Shell with `LoadingView` ("Starting").
3. `do_configuration_loaded(cJSON*)` runs on the UI task once WiFi is up. It copies `backendUrl`
   and `screenOffSeconds` out of the JSON, which esp-mdm frees when the hook returns. It then sets
   the loading text to "Checking for an update" and calls `lv_refr_now(nullptr)`, because the
   update check that follows blocks this task (section 2).
4. `do_ready()` runs once MQTT is connected. It starts the four backend tasks, asks for the
   speakers, the chosen speaker's playback and its group's volume, and calls `Shell::open()`,
   which replaces `LoadingView` with the app's first view: the speaker picker or Home (section 5).
5. `do_process()` calls `board.process()`, tells the artwork loader whether the screen is lit
   (section 7), settles the changes whose answer did not come in time (section 4), and calls the
   Shell's `update()`, which updates the top view and the bar. Once startup has settled
   (`Application::has_settled()`), it logs "Startup has settled" and marks the run's `settled`
   stage (`RunMark`, below), once per boot: the first view is up, the event stream is connected,
   and no `HttpClient` holds a request whose answer has not run on the UI task
   (`HttpClient::is_idle()`), a cover the first view asked for among them, and the card holds no
   read whose answer has not run there (`CardTask::is_idle()`). A client added to `Application`
   joins `has_settled()`.

esp-mdm restarts the device when WiFi cannot be joined, when WiFi or MQTT drops, and when IoT
Support announces a new configuration or firmware. SonosDisplay accepts that: a restart takes
seconds and the speaker plays on. `do_network_connection_failed()` is not overridden, since the
restart follows it at once and nothing drawn there would be seen. A backend that is down is
different: it never restarts the device (section 3).

esp-mdm needs MQTT even though this firmware publishes nothing of its own, so
`sdkconfig.defaults` sets `CONFIG_MQTT_TOPIC_PREFIX` to the value PaperClock's sets, and
`CONFIG_MQTT_DEVICE_MANUFACTURER`, `CONFIG_MQTT_DEVICE_MODEL="Sonos Display"` and
`CONFIG_MQTT_DEVICE_MODEL_ID="Sonos Display v1"`.

### What a restart leaves behind

esp-mdm logs `esp_reset_reason()` at every start, and it reaches IoT Support's device log. It says
`ESP_RST_WDT` alike for a restart the firmware began that then stalled, which the one-second RTC
watchdog armed by `esp_restart()` finishes, and for a panic whose handler hung, which the panic's
own RTC watchdog finishes. So the firmware keeps its own record of every run, `RunMark`
(`main/board/RunMark`), and logs it at the next boot:

- **The mark.** A struct in RTC memory (`RTC_NOINIT_ATTR`), with a magic and a CRC, since RTC
  memory holds what it happens to hold after a power-on and a reset can cut a write short. It
  holds the last stage the run reached, `booting`, `configured`, `ready` or `settled` (section 1's
  steps), with its uptime; and of a restart, the task that began it, its uptime, whether the
  panel stopped in time, and how its scan-out halted, with four of the data cache's registers.
  `Board::begin()` keeps the run before's mark and starts this one's.
- **What survives.** RTC memory survives a reset of the digital core alone: a software restart, a
  panic's restart, and the first stage of the restart's RTC watchdog (raw reason 9, "RTC watch dog
  resets digital core"). It also survives raw reason 16, which resets the RTC module too (the
  second stage of that watchdog, and the panic's): the boots after the stalled update restarts of
  SDSP-20 read their run before's mark. A mark that did not survive reads "left no mark", and so
  does one a firmware with another layout of the mark left, whose CRC does not match. A power-on
  loses it.
- **The log.** `Board::log_state()`, which `do_ready()` calls, logs "Raw reset reason …", the
  chip's own for both CPUs, which tells those two watchdog resets apart, and "The run before …",
  the mark, with a second line on the halt.
- **The stop before a restart.** `Board::begin()` registers a shutdown handler, which
  `esp_restart()` runs on the task that called it before it arms its watchdog. It marks the
  restart begun, then stops the panel's presents for good (`RgbPanel::stop()`): none begins after
  it, and one another task is in is waited for, at most `LCD_STOP_TIMEOUT_MS` (a frame's timeout
  and 50 ms); on the task that presents it returns at once. Then it marks how the stop
  ended, halts the scan-out (below) and marks how the halt ended. `esp_restart()` arms its
  watchdog only once every handler has run, so a reset by it comes after the marks.
- **The halt before a restart** (`RgbPanel::halt()`). Each time the refill copies a bounce buffer
  (section 3) it starts the data cache's preload of the framebuffer's next lines, through the ROM's
  `Cache_Start_DCache_Preload()`, and ESP-IDF's driver never ends one. `esp_restart_noos()`
  disables the data cache through the ROM's `Cache_Disable_DCache()`, which spins, with no time
  limit, until the preload is done, and by then it has already stalled the other core. When the
  restart is begun on core 1, the UI task's, that core is core 0, and the stall is kept in the RTC
  domain: the first stage of the restart's watchdog, a reset of the digital core, boots nothing,
  and only its second, raw reason 16, after two seconds, restarts the chip. That is how every
  update restart on the firmware with bounce buffers ended (SDSP-20), and no other restart did.
  What likely sets the update's install run apart is that it alone maps flash into the cache, for
  otadata and to verify the image written, and ESP-IDF freezes the cache for each mapping without
  waiting for a preload under way; the mark says whether the preload had hung. So the handler
  stops the LCD, as the driver's own teardown does, which ends the refills, waits
  `LCD_HALT_SETTLE_US` for the last, then up to `LCD_PRELOAD_TIMEOUT_US` for the preload to be
  done. If it is not, no refill is running and the preload hung: the handler clears its enable
  bit, which a hung one holds and the ROM's start never clears, preloads one cache line afresh and
  waits again. The mark keeps which of the three it was and the data cache's preload, autoload,
  sync and state registers as the halt left them. A panic's restart runs no handler, so it halts
  nothing.
- **No shutdown handler is left.** ESP-IDF keeps five, and all five are in use: esp_timer's,
  `Board`'s, WiFi's, and esp-mdm's two (its log flush and `ApplicationBase`'s), registered in that
  order and run in reverse, so `Board`'s runs fourth. Nothing more can register one.

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
21 MHz timings, are dead code there and are left behind; SonosDisplay's bounce buffers (section 3)
are set up anew.

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
| TF card | SD over SPI on SPI2: MOSI GPIO 11, SCK GPIO 12, MISO GPIO 13, pulled up. Chip select is the expander's bit 4; no card-detect line |

Expander outputs, one byte: bit 1 touch reset, bit 2 backlight, bit 3 LCD reset, bit 4 SD chip
select, bit 5 USB/CAN select. The firmware only ever writes bits 1 to 3. The card's select is
active low, so the card is selected from the first write on, and its SPI slot has no chip-select
line: the card needs no I2C, which only the UI task touches (section 7). A `static_assert` in
`BoardConfig.h` keeps bit 4 out of every bit the firmware writes.

### Panel

`esp_lcd_new_rgb_panel` with these values, which are the shipped ones but for the bounce buffers:

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
| `bounce_buffer_size_px` | 8000, ten lines (`LCD_BOUNCE_BUFFER_PX`, section 3) |
| `dma_burst_size` | 64 (ESP-IDF 5.5's name for the old `psram_trans_align`) |
| `flags.fb_in_psram` | 1 |

Then the callbacks, `on_vsync` and `on_frame_buf_complete`, and `esp_lcd_panel_reset()` and
`esp_lcd_panel_init()`. That is about 33 frames a second. The 14 MHz clock is deliberate:
ThermostatDisplay's comment is "We need to lower the clock speed when double buffering", and
21 MHz and the vendor's 30 MHz were both too fast for framebuffers in PSRAM.

The driver allocates the panel's interrupts on the core that creates the panel, so
`RgbPanel::begin()` runs all of this, `RgbPanel::install()`, in a task pinned to core 1
(`RgbPanel::INTERRUPT_CORE`), which ends once the panel is up (section 3).

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
    if (_stopped) return;                                    // stop(), before a restart (section 1)
    esp_lcd_panel_draw_bitmap(_panel, 0, 0, 800, 480, fb);  // fb is a panel framebuffer: it becomes the current one
    xSemaphoreTake(_taken, 0);                               // drop a sign from before the swap
    xSemaphoreTake(_taken, pdMS_TO_TICKS(LCD_FRAME_TIMEOUT_MS));  // the refill copies from the new buffer from here
}
```

`_taken` is a binary semaphore given from the panel's `on_frame_buf_complete` callback,
`RgbPanel::on_framebuffer_taken`, with `xSemaphoreGiveFromISR`. Waiting for it keeps LVGL from
drawing into the buffer the refill still copies to the screen. That holds only because of how the
driver swaps with bounce buffers (section 3), read in ESP-IDF 5.5.3's `esp_lcd_panel_rgb.c`:

- `esp_lcd_panel_draw_bitmap` with a panel framebuffer only makes it the current one. It writes
  no cache back: the refill reads the framebuffers through the data cache LVGL writes them
  through.
- The refill copies a frame into the bounce buffers ten lines at a time, all from one framebuffer.
  Once it has copied a frame's last lines, it takes the current framebuffer for the next frame
  and calls `on_frame_buf_complete`. Until then it may still copy from the old one, which LVGL
  draws into next.
- The refill's interrupt runs on the core of the task that presents, so it runs whole between two
  of that task's instructions: a sign given after the clear comes from a frame the refill began
  after the swap. One begun between the swap and the clear costs a frame more. On two cores
  neither holds, so `Application.cpp` has `static_assert(UI_TASK_CORE == RgbPanel::INTERRUPT_CORE)`.
- The panel starts on fb0, so LVGL is given fb1 as its first buffer: its first frame is drawn
  out of sight.

A frame is 840 × 510 clocks, 30.6 ms at 14 MHz, so the wait is at most a frame, or two when a
frame begins between the swap and the clear. `_vsync`, given from `on_vsync`, is what
`RgbPanel::restart()` waits for (section 3).

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
reads high, so presence that lasts keeps the screen on while motion wakes it (section 3).

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

CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000

CONFIG_LCD_RGB_ISR_IRAM_SAFE=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y

# CONFIG_SPI_MASTER_ISR_IN_IRAM is not set

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
- **The two panel options** are for the bounce buffers (section 3).
- **The SPI driver's interrupt handler runs from PSRAM**, with the rest of the code, for the TF
  card's bus. In internal RAM, ESP-IDF's default, the handler and what it calls take 8 KB
  (section 7).
- **The PSRAM clock stays at 80 MHz.** The vendor's benchmark figure is for 120 MHz, which
  ESP-IDF 5.5.3 offers for octal PSRAM only as an experimental feature.

## 3. The two hardware quirks

### The picture drifts while flash is written

**What it is.** The panel's pixels are fetched by DMA, continuously, at the pixel clock. When the
DMA reads them straight from the PSRAM framebuffer, a flash write holds up PSRAM access for a
moment, the DMA falls behind the pixel clock, and from then on the picture is shifted until the
transfer is restarted.

**What ThermostatDisplay did.** Three things were in effect: the 14 MHz clock, code and
read-only data moved to PSRAM (`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` and `CONFIG_SPIRAM_RODATA`),
and `esp_lcd_rgb_panel_restart()` every time the screen was switched on, "preventing screen drift
from becoming permanent". The fourth, switching the screen off when an update starts, was never
wired: `OTAManager` declares the callback and nothing calls it.

**What SonosDisplay does.**

1. **Keeps all three.** `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` is ESP-IDF 5.5's switch for both
   options; its help text says code "that need to be executing during Flash operation can
   continue working normally".
2. **Scans out through bounce buffers.** The panel's DMA reads two bounce buffers of ten lines in
   internal RAM, 16,000 bytes each (`LCD_BOUNCE_BUFFER_PX`), and an interrupt refills each from a
   framebuffer in PSRAM once the DMA has read it; section 2 has which, and the swap that goes
   with it. A hold-up of PSRAM then has ten lines, 600 µs, of slack, where the FIFO between DMA
   and panel holds 16 pixels, about a microsecond at 14 MHz. `CONFIG_LCD_RGB_ISR_IRAM_SAFE` keeps
   the panel's interrupts and their callbacks in IRAM, so the refill also runs while flash is
   written, and `CONFIG_ESP32S3_DATA_CACHE_LINE_64B` has the data cache, through which the refill
   reads PSRAM, fetch 64-byte lines, which Espressif's ESP-FAQ asks for in this mode (its LCD
   page: "Therefore, it is necessary to enable CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y. Otherwise, it
   may cause the screen to drift."). A frame that still comes up short, with fewer refills than a
   frame takes, is restarted by the driver itself at the next vblank. Each refill also starts a
   preload of the data cache, so a restart halts the scan-out first (section 1, "The halt before
   a restart").
3. **Does not set `CONFIG_LCD_RGB_RESTART_IN_VSYNC`.** The skeleton set it, without bounce
   buffers, to make a drift last one frame instead of until the next wake, and the first display
   showed artifacts with it ("What the first display showed", below). With bounce buffers the
   driver restarts a short frame by itself, and the option would restart every frame, which the
   driver's own comment says shifts a frame whose vblank interrupt comes late.
4. **Adds no hook for updates, and changes nothing in `esp-libs`.** In esp-mdm, a firmware
   update is downloaded and written only during boot, synchronously, on the task that calls
   `process()`, and it ends in `esp_restart()`. So while flash is written the UI task is blocked,
   the screen holds the loading view, and the restart that follows re-initialises the panel.
   There is no running screen to protect.

The other flash writes are short: the speaker choice and the motion setting saved to NVS, esp-mdm
storing new provisioning data (followed by a restart), and a coredump being erased after its upload
at boot. Items 1 and 2 cover them, and the restart on the next wake puts right what they do not.

The bounce buffers cost 32,000 bytes of internal RAM (section 7) and an interrupt that copies
about 25 MB a second out of PSRAM; how much of its core that takes is not measured. The core is 1,
the UI task's, not 0, where WiFi and the backend's tasks run: the copy's time comes out of
rendering rather than the network, and the waits of `RgbPanel::present()` and
`RgbPanel::restart()` hold only on the core of the interrupts (section 2).

The loading view cannot animate during an update, since its task is blocked. It shows "Checking
for an update" as a still frame (section 1, step 3).

### What the first display showed

The skeleton, flashed on 2026-11-15, showed screen update artifacts. The cause is in ESP-IDF
5.5.3's RGB driver, not in flash or PSRAM traffic:

- **Without bounce buffers, a restart on the S3 always scans out fb0.** A hardware workaround
  (`RGB_LCD_NEEDS_SEPARATE_RESTART_LINK`) restarts the DMA through a one-node "restart link" that
  is mounted on `fbs[0]` when the panel is created and never moved. The transfer follows the current buffer only through the link at
  the end of a frame.
- **`CONFIG_LCD_RGB_RESTART_IN_VSYNC` restarts on every vblank**, so it never gets there. The
  panel showed fb0 always. LVGL alternates the two buffers, so every second update was drawn
  straight into the visible buffer, and every other one was never shown until the next update
  copied it across. ESP-IDF's FAQ calls the option a last resort, "Not Recommended", and its
  own RGB example does not set it.
- **ThermostatDisplay never set it.** Its only restart was the one on waking, which costs one
  frame from fb0 and then follows the current buffer.

So the option is off, and the restart on wake remains. With bounce buffers the restart link is
mounted on the first bounce buffer instead, and the refill follows the framebuffer it took last,
the current one once a present has returned (section 2). So a restart shows the current picture.
`RgbPanel::restart` asks the driver for one, which it makes at the next vblank, and returns once
that vblank has passed, at most two frames later, so the backlight comes on over the restarted
picture.

**Then the drift showed.** With that update installed, the operator switched the display on and
saw the picture shifted, until the screen went dark and woke: "It's very familiar." This is the
drift the restart in every vblank had hidden, and startup is where it comes from. Startup reads
and writes flash: WiFi's and the PHY's data in NVS, the update check, the core dump check. Flash
and PSRAM share the bus, so each access held up the panel's DMA, which read the framebuffer
straight from PSRAM then.

**Then shifts at startup and in use.** Trying the first milestone, the operator saw the picture
shift once during startup, and differently just before the main screen opened; at milestone 2's
try-out, "no improvement at all in the screen shifting". When it showed is not known beyond those
words, so the fix covers a sideways shift wherever it shows, at startup, in use and on waking: the
panel scans out through bounce buffers (item 2 above). At their try-out, on 2026-11-17, the
picture stayed put through three power-on starts, a sitting of use and three wakes: "This really
did solve the shifting problem completely." The panel is restarted on waking alone. Whether the
picture stays put with that restart alone, through power-on starts, a wake, a speaker chosen and
the motion setting switched, is still to see on the display ("To check on the first display").

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
- **Switching on:** `RgbPanel::restart()` first, which returns once the panel has restarted on
  the current picture (above, up to two frames), then the expander write.
- **Switching off:** the expander write only. The panel keeps scanning and LVGL keeps drawing.
- **Wake:** a touch, or motion while the motion setting on Home lets it (section 5), which the
  display stores (section 4). Switched off, `ScreenPower::handle_motion()` returns at once: motion
  neither wakes the screen nor keeps it lit, though the sensor is still polled. This is for a
  sensor too sensitive for its place; the firmware filters no motion. A touch on a dark screen
  wakes it and is swallowed until the finger lifts, so it cannot press what was under it.
- **Sleep:** `screenOffSeconds` after the last touch, or the last motion while motion wakes the
  screen. The default is 60; the device configuration can set it (section 4). ThermostatDisplay's
  20 seconds was a build setting.
- **Boot:** the backlight comes on with the loading view only when `esp_reset_reason()` is
  `ESP_RST_POWERON`. After any other reset the screen stays dark until it is woken. A
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
schemes work: an `https://` server's certificate is verified against the one root each client is
handed in `begin()`.

**The deployed backend is HTTPS only**, found at the first display's bring-up on 2026-11-15: its
ingress answers plain HTTP with a 301 to HTTPS, and its certificate is issued by the homelab CA,
which Mozilla's bundle does not hold. Every connection then failed the handshake
(`esp-x509-crt-bundle: No matching trusted root certificate found`), and the screen stayed at
"Connecting". The firmware therefore carries the homelab root, `certs/homelab-root.pem`, which
`main/CMakeLists.txt` embeds and `Application` hands to the backend's three `HttpClient`s and its
`EventStream` as their `cert_pem`, so `backendUrl` is `https://`. Those four verify against that
root alone. esp-mdm's own connections keep ESP-IDF's bundle: none of them goes to the backend, and
the first display installed an update over the air on that bundle alone. The file is a root
certificate, public by nature, and holds no hostname or address; it is valid until 2036.

```cpp
struct HttpRequest {
    esp_http_client_method_t method{HTTP_METHOD_GET};
    std::string path;                    // "/api/..." appended to the base URL
    std::string body;                    // JSON for POST/PUT/DELETE, else empty
    size_t max_response{256 * 1024};
    bool merges{};                       // goes as one with an equal request that waits
};
struct HttpResponse {
    esp_err_t err;                       // transport error, or ESP_OK
    int status;
    HttpBody body; size_t length;        // in PSRAM, NUL-ended; the receiver moves it out to keep it
};
esp_err_t HttpClient::begin(const std::string& base_url, const char* root_certificate, ...);
void HttpClient::send(HttpRequest request, std::weak_ptr<void> scope,
                      std::function<void(HttpResponse&&)> done);
```

- **One task per client, one request at a time, in the order sent.** `send` puts the request on
  the client's queue (16 deep), and answers it as failed when the queue is full. The task keeps
  one `esp_http_client` handle with keep-alive and reuses it; after a transport error it destroys
  the handle and makes a new one.
- **A connection stays open for 50 seconds idle, and the client then closes it itself**
  (`HttpClient::KEEP_ALIVE_IDLE_MS`). A new connection cost about 2.5 seconds of a typical
  2.7-second press on the first display, and a press on a kept one 0.14 seconds (SDSP-7). Until
  2026-11-18 the limit was 1.5 seconds, so nearly every press after a pause paid a handshake. The
  backend's nginx closes a connection idle for 65 seconds: its default `keepalive_timeout`, which
  nothing in the deploy sets, measured from a pod on 2026-11-18 (65.0 seconds twice, then its
  FIN). The rule is that nothing is sent on a connection the server may have closed: the request
  would fail, and a press is never sent twice, so it would be lost. The 15 seconds between are
  the most the idle time the display counts can fall short of nginx's: the last answer's way to
  the display and the next request's way to nginx, each bounded by the 5-second timeout, and the
  task's wake-up. Past 50 seconds the next request opens a new connection, which costs a
  handshake and nothing else. A connection nginx closed sooner, as it closes its idle ones when
  it reloads, is found before a request goes on it: the socket is peeked, and a byte, its end or
  an error closes it first. One the server closes while a request is on its way fails that
  request, as any transport error does. An answer with `Connection: close` closes it too.
- **The press's and the API's connections are kept open while the screen is awake.** From the
  moment motion or a touch wakes it until it goes dark, every pass of the UI task asks the two
  clients to keep their connections open (`HttpClient::keep_open`). A client with no connection,
  or with one idle for 40 seconds, sends `GET /health/healthz`, the backend's liveness check,
  which reaches neither Sonos nor Plex and answers 32 bytes, and drops the answer; it sends
  nothing while a request waits or is under way, and no second check within 10 seconds of one, so
  a backend that is down is not asked in a loop. The press client goes first, as the screen
  wakes: the motion sensor lights it before a hand reaches it, and the handshake is made
  meanwhile. The API client follows once the press's connection is open, so the two handshakes do
  not share the CPU and the link; it carries the next list a hand opens. The artwork client is
  not kept open: a cover that comes later is no press that waits, and the covers asked for in the
  dark are fetched as the screen wakes, which opens its connection anyway. In the dark the checks
  stop and every connection closes after 50 seconds idle, which frees its TLS state too. The
  price is one short request per client every 40 seconds while the screen is lit.
- **A new connection resumes the client's last TLS session** where nginx still holds it
  (`save_client_session`, `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS`). The resumed handshake checks
  no certificate and makes no key exchange, both done in software on the S3 for the backend's
  ECDSA P-256 chain, and takes one round trip fewer. From a pod, nginx resumed TLS 1.2 sessions by
  ticket and by session id, also after 70 seconds idle; its `ssl_session_timeout` is 10 minutes.
  So it pays for a connection that lapsed while the screen was lit or briefly dark, not for the
  first after a long dark spell, and it costs a Kconfig line and a few hundred bytes a client. On
  the first display, on 2026-11-18, a connection the client had closed after 50 seconds opened
  again in 44 and 49 ms, resumed; one opened 11 minutes later, when nginx no longer held the
  session, took 1,338 ms, a full handshake, against about 2.5 seconds on the firmware before. The
  session is kept with the handle and goes with it after a transport error. The peer's
  certificate is not kept after the handshake (`CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE` off):
  nothing reads it then, and every open connection and kept session would hold a copy in internal
  RAM (section 7).
- **WiFi modem power save is off**, set with `esp_wifi_set_ps(WIFI_PS_NONE)` once the network is
  up. In modem sleep, ESP-IDF's default, the access point holds each frame for the display until
  its next beacon, about 100 ms at DTIM 1, so every round trip of a handshake and every answer can
  wait that long; the first display logged `pm start, type: 1` and lost beacons at RSSI −73. With
  it off the radio listens all the time. That costs current, and the display runs on mains power;
  it adds no traffic. The operator's boards carry a capacitor against brown-out (section 3), and
  the one brown-out reset the first display logged, on 2026-11-16, followed half an hour in which
  it logged nothing: a power-on. The backlight's switching rule is unchanged.
- **Every request is logged** with its method, path, status and time, and whether it went on a
  new connection, with the time to open it, or on a kept one, with how long it had been idle; a
  failure with its error. With the app's "Sending" lines they time a press on the display's log
  alone.
- **Three clients.** `backend_press` carries every press: transport commands, changes of the
  volume and of groups, the queue's play, shuffle, add and clear, and the favourites' marks and
  unmarks. `backend_api` carries every fetch of the backend's state and of its lists.
  `backend_art` carries artwork. So a press waits behind no fetch, however long one stalls, and
  behind no cover, only behind the presses sent before it.
- **A fetch merges.** Every request on `backend_api` is sent with `merges`: one equal to a request
  that still waits in the client, by method, path and body, goes as one with it, the later
  caller's scope and `done` taking the earlier's place. One under way merges with nothing. So a
  burst of events asks for the speakers, the playback, the volume or a page of the queue once
  more, not once per event.
- **A fetch from before a press leaves what the press showed.** A fetch and a press go on two
  clients, so the backend can read a fetch of the playback or of the volume before the speaker
  acts on a press, and answer it after the press's answer. Each fetch carries a stamp
  (`FetchStamp`, `main/ui/Model.h`): the id of the press last sent when it was asked for, and
  whether that press was on its way then. One asked for before a transport command was sent, or
  while it was on its way, is from before it: its track and the commands the speaker allows are
  taken, the play or pause the command showed stays. One like it for a change of the volume is
  dropped, failed or not: the change's answer carries the level. A fetch that merges carries the
  later caller's stamp, which is right, since it is sent after that one was taken. The fetch
  asked for once the press is answered, by the speaker's event or by a press not taken, is taken
  whole. Clear needs none of this: it shows nothing before its answer, and the fetch its
  `queue_changed` asks for is answered after the one from before it, on the same client.
- **A press has a time limit.** `HttpClient` bounds every wait on the link by the 5-second
  timeout below, and esp-tls gives a handshake up at its first step that ends 5 seconds or more
  after it began, so a request on the press path ends within about 30 seconds, but for a host
  lookup that misses lwIP's cache and a handshake whose every read comes just inside the timeout.
  A transport command, a change of the volume, of a group or of a favourite, or a request to play,
  shuffle or add, whose answer has not come 30 seconds after it was sent (`ANSWER_TIME_LIMIT_MS`,
  `main/ui/Model.h`) is settled, so an answer dropped on its way to the UI task, or a press stuck
  behind stalled ones, leaves nothing on its way for good. Each carries an id, and an answer to one
  that is no longer on its way changes nothing: a command is on its way until a later one takes
  its place.
- **One change of the volume on its way at most.** A press sends `{"delta": n}` to the group's
  volume: a step, which the speaker takes from the volume it has, where a level sent from the
  screen could undo a change made in the Sonos app. The presses made while a change is on its way
  wait in the model (`Volume`) and go as one change when it is answered, so a burst of presses is
  one request at a time. While the group shows muted, a press on up also unmutes it: the change
  carries `"muted": false`, also at the top of the range, where its step is 0, and also where up
  and down net to no step. Down leaves the mute, so a group muted while loud can be brought down
  first. A change the time limit settled drops the steps that wait with it, and the volume is
  fetched again, without a notice.
- **A change of a group is pending until a speakers list shows it.** A speaker joins the chosen
  speaker's group with `PUT /api/speakers/{id}/group`, the body `{"speaker_id": ...}` naming the
  chosen speaker, and leaves it with `DELETE` on the same path. The 204 says that the speaker took
  the change, not that the group has changed: the list shows the old groups until the speakers
  report the new ones, and the backend decides from the old groups whether a repeat is sent on to
  the speaker. The change therefore waits in the model (`Grouping`) from the press until a list
  shows the speaker in the group or out of it, as asked, and the speaker takes no second change
  meanwhile. A pick of another speaker leaves it waiting on the group it was sent for. A change the
  backend took that no list shows within 15 seconds of its 204 is settled without a notice, and the
  speakers are fetched again; any answer but 204, or a failed request, is a change not taken, and
  the speakers are fetched again. A change whose answer has not come within the time limit is
  settled without a notice too.
- **Play, Shuffle and Add to queue** send `POST /api/speakers/{id}/queue`, the body `kind`,
  `item_id`, `action` and, for an artist's play and add, `"order": "newest_first"`: what the
  speaker plays for an artist from the Sonos app. A track is added, never played or shuffled on
  its own: a press on its line sends its album's `play` with the track as `start_track_id`, so the
  whole album replaces the queue and playback starts at the track, as in the Sonos app. That press
  is a request of its own beside the album's Play, and on its way apart from it. The backend
  answers 202 with a `task_id` and queues in the background; the task's end comes later on the
  event stream, as `task_event`, which every connection receives and which may come before the
  202. The model (`Queueing`) watches the
  tasks of the display's own requests, the newest 8, and holds the ends no answer has named yet
  while a request is on its way, also 8 at most. A failure is said in the backend's words; an add
  the backend took is said too, since nothing else on screen shows it; Play and Shuffle show as the
  playback's events bring what plays. Any answer but a 202 with a `task_id`, a failed request, or
  none within the time limit, is a request not taken.
- **Clear** sends `DELETE /api/speakers/{id}/queue`. A 204 waits for the queue's events to show
  the empty queue; any other answer, or a failed request, is a Clear not taken. Nothing waits on
  a Clear, so it has no time limit.
- **A favourite** marks an artist, an album or a playlist, the same for every display; a track
  cannot be one. Every artist, album and playlist of the library carries `favourite_id`, the
  favourite that marks it or `null`, which the firmware reads as required. A press marks an item
  with `POST /api/favourites`, the body `kind` and `item_id`, and unmarks it with
  `DELETE /api/favourites/{favourite_id}`. A mark is taken when answered 201, or 200 for an item
  marked already, the answer's favourite holding the `id` that marks it now; an unmark when
  answered 204, or 404 `RECORD_NOT_FOUND`: another client removed it, and the item is unmarked
  either way. Any other answer, a failed request, or none within the time limit, is a change not
  taken. The model (`Favouring`) holds the changes on their way, one an item at most, since a view
  sends no second while one is on its way, and the newest 8 the backend took, which the views
  apply to the items they hold; a view that missed more than 8 fetches its items again. An
  artist's albums and an album's tracks, whose heading holds the item's toggle (section 5), fetch
  that item again on its own, `GET /api/library/artists/{id}` or `GET /api/library/albums/{id}`,
  when they missed more than 8 and whenever the favourites may have changed unseen; one that does
  not come leaves the toggle as it is. Home lists
  the favourites with `GET /api/favourites`, by title (`sort_by=title`), so a new mark lands in its
  place. A favourite that is not available, which Plex does not have or does not answer for, keeps
  its title; the firmware keeps its `item_id` and `artwork_id` only while it is available, and
  refuses an available one without an `item_id`.
- **The bodies are written in `main/api/Requests`** (`write_volume_change`,
  `write_queue_request`, `write_favourite_mark`), which `sim/` builds too, so a scene checks the
  body the device sends.
- **The body goes to PSRAM.** The task reads with `esp_http_client_open`,
  `esp_http_client_fetch_headers` and a read loop into a `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`
  buffer sized from `Content-Length`, growing when there is none, and failing with
  `ESP_ERR_INVALID_SIZE` beyond `max_response`. Artwork requests set `max_response` to 512 KB.
- **The callback runs on the UI task**, through `get_queue().enqueue(...)`.
- **`scope`** is a weak reference the caller owns, in practice the view that asked. If it has
  expired when the request is about to start or the response arrives, the request is dropped and
  the body freed. A closed view therefore receives no late data, and a request of its that has
  not started is never sent. Artwork goes in the app's own scope instead: the loader starts a
  download only while a view wants it, and keeps what one under way brings (section 7).
- **Timeouts and retries:** 5 seconds. A GET is retried once after a transport error; nothing
  else is retried, since a repeated "next" is a different command.
- **Answers are read in `main/api/Answers`**, where what the backend sends enters the app. The
  client hands status and body to the caller, which hands them there. An answer the API's
  document does not allow is refused whole, and one it allows is never refused for a field the
  display does not show. A command is taken when it is answered 204; any other answer, or none,
  is a command not taken, and the playback is fetched again. A text the backend sends, such as a
  title or a speaker's name, is read through `read_text()`: without the bytes that are not UTF-8
  and the characters the fonts do not draw (section 7), cut at a character to 200 bytes. An id is
  read as it is.
- **Errors:** a status of 400 or above carries the template's envelope,
  `{"error": ..., "details": {"message": ...}, "code": ...}`, though not always: a validation
  error is a bare array. The envelope's `error`, read as a text, is what a view shows when a list
  or the playback did not come; any other body is a failure without a message.

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
8. **After every `ready`** the app is told the stream is (re)connected, and fetches the speakers,
   the chosen speaker's playback and its group's volume again, since events sent while it was
   away are gone; the queue screen, while it shows, asks again for the pages it holds; and
   `Model::favourites_changes` moves, as on `favourites_changed` (below). There is nothing to
   renew: every connection receives every event.

**What the app does with an event.** `speakers_changed` fetches the speakers, and the group's
volume, which a speaker joining or leaving the group changes. `playback_changed` and
`volume_changed` name a speaker and come once for every member of a group, so the chosen
speaker's own id is the whole filter: one that names it fetches the playback or the volume. So does
`queue_changed`, which fetches nothing itself: it and `playback_changed` move
`Model::queue_changes`, and the queue screen, while it shows, asks again for the pages it holds.
One whose payload cannot be read is taken for the chosen speaker's. When the chosen speaker comes
back into the speakers list, its playback and volume are fetched too. `favourites_changed` says
that a favourite was marked or unmarked, on any display; its payload is not read. It moves
`Model::favourites_changes` and fetches nothing itself: Home and the artists, albums and playlists
ask again for the pages they hold, at once while they show, and as they show again under a view
that closed; the tracks and the queue do not. `task_event` reports the progress and end of the
backend's tasks; the display reads the ends of those its own requests started (Requests, above).

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

**Two settings are stored on the display**, in NVS namespace `sonos`: the speaker choice, key
`speaker`, a string holding the speaker's id as the API gives it, and whether motion wakes the
screen, key `motion_wakes`, a u8, 1 or 0. That key is absent until the setting is first switched,
and motion wakes the screen while it is. Neither goes into esp-mdm's namespace `prov`, whose
unknown keys are deleted whenever provisioning data is renewed. The motion setting is switched on
the display (section 5), not in the device configuration. Provision device rewrites the whole
`nvs` partition, so a display provisioned again over USB asks for its speaker again, and motion
wakes it again.

### What this asks of the API design (task 6)

These are the firmware's constraints. The endpoints' shapes are task 6's.

- **Artwork** in the device form is LVGL 9's binary image: the 12-byte header (magic `0x19`,
  colour format `0x12` RGB565, flags 0, width, height, stride) followed by the pixels,
  little-endian, uncompressed, no alpha, already dithered, at exactly the requested square size,
  with `Content-Length` set. IoT Support's `LVGLImage.py` writes this form. The sizes are 400, 160
  and 80 pixels (section 7). An artwork id names one picture: the display keeps a picture on its
  TF card by its id, size and format, and does not ask for it again (section 7).
- **Text** in Unicode NFC. The fonts hold composed characters, so a decomposed accent would be
  drawn beside its letter.
- **Lists** are paged; the template's paging allows at most 100 items per page, and the firmware
  asks for 100. The speakers alone come unpaged, a household's being few; the firmware reads the
  first 200.
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

- **`Model.h`**: plain structs holding what the views draw: the event stream's state, the
  speakers and the chosen one with each one's group, whether motion wakes the screen, the
  playback, the group's volume, the pending changes of groups, the items asked to be queued and
  the items asked to be marked or unmarked as a favourite, with the rules by which a command or a
  press changes what they show (`Playback`, `Volume`), a change of a group settles (`Grouping`), a
  request to play, shuffle or add is watched to its end (`Queueing`) and a change of a favourite
  is settled and applied (`Favouring`, `FavouriteChange`); the time limit of a press; the counts
  that tell a view to fetch the queue or the favourites again; the pages of a list a view holds
  (`PagedList`), the library's items, the favourites and the queue's entries; and `can_draw()`,
  which says whether the fonts draw a character.
- **`UiHost.h`**: an abstract class with what a view may ask for: a speaker chosen, motion set to
  wake the screen or not, a transport command, a step of the volume, a speaker added to the chosen
  speaker's group or removed from it, a fetch again, a page of the artists, an artist's albums, an
  album's tracks, the playlists, the favourites or the queue, one artist or one album with the
  favourite that marks it, an item played, shuffled or added to the queue, an item marked or
  unmarked as a favourite, the queue cleared, a piece of artwork.
  `Application` implements it on the device; `sim/` implements it with fixtures.

`ui/log.h` gives the views `UI_LOGI`, `UI_LOGW` and `UI_LOGE`: `ESP_LOGx` when `ESP_PLATFORM` is
defined, `printf` otherwise. The rule is enforced by the build: an ESP-IDF include in `ui/` fails
`kc project test`.

**`main/api/`** is built by `sim/` too: the backend's answers read into the model's types
(`Answers`), the bodies of the requests the firmware sends (`Requests`), the artwork's checks, cache
and loader (`Artwork`), and the pictures kept on the TF card (`ArtworkStore`). It includes cJSON,
LVGL, the C++ standard library and `ui/`'s two doors only, and nothing of ESP-IDF is on `sim/`'s
include path, so an ESP-IDF include there fails `kc project test` as well. The scenes' fixtures are
bodies as the backend sends them, read by the code the device runs, and a scene can check the body
the device would send.

### The UI's structure

- **One LVGL screen.** `Shell` owns it: a content area, the now-playing bar under it, which is how
  the bar "stays at the bottom of every screen", and the "Reconnecting" marker, in the top corner
  the top view leaves free (`View::get_marker_align()`): the top right, but over the now-playing
  screen the top left, over its cover's corner. A list's heading ends before the marker while it
  shows at the top right (`Shell::get_marker_reach()`). The bar shows
  under every view that has it (`View::has_bar()`, true unless a view says otherwise) while the
  display is not held on the speaker picker, and the content area then ends where the bar
  begins. `NowPlayingView`, which carries the bar's controls in full, and `LoadingView` say
  otherwise. `Shell::layout()` alone decides, and runs in `push()`, before the view renders,
  `pop()` and `update()`, so no frame draws a view at the wrong height.
- **The first view.** `Shell::open()` shows the speaker picker while the display needs a speaker
  (`Model::needs_speaker()`: none is chosen, or the last list the backend answered lacks the
  chosen one; a list never answered holds nothing), and Home otherwise. From then on `update()`
  keeps it the right one: when the hold begins or ends, every view gives way.
- **Views** are what PaperClock calls a UI: a class deriving from `View`, with `do_render(parent)`
  building its objects inside the content area, `do_update()` called from the loop while it is
  the top view, and `do_show()` when it shows again after the view over it closed. `Shell` keeps
  a stack of them. Going back destroys the view that is left, so memory is bounded by the depth
  of the stack, not by what was visited. A view that navigates takes the `Shell*`: `push()`,
  `open_speaker_picker()`, `open_library()`, `open_queue()`, and two that act at the next
  `update()`, so a view may call them from its own event: `back()`, and `open_now_playing()`,
  which closes the views over the now-playing screen wherever it sits in the stack, or opens one
  on top while the stack has none. Only `NowPlayingView` says `View::is_now_playing()`.
- **The views.** The speaker picker, the speakers screen, lists the speakers and stores a pick; it
  is reached from now playing, and a touch on a row picks its speaker and goes back. While the list
  has the chosen speaker, every other row carries a group control at its right end: a ring for a
  speaker outside the chosen speaker's group, which a press adds to it, an accent disc with a tick
  for one in it, which a press removes, and a spinner while a change of that speaker is pending,
  which takes the press and sends nothing. A press on a control never picks, and the control takes
  presses over the row's whole height. The chosen speaker's row has no control, only its tick where
  the controls sit: it does not leave its own group from here. The now-playing screen shows the
  cover at 400 px, title, artist and album, previous, play or pause, and next, and under them the
  group's volume: down, the level, up; its top row holds a way back to the view it was opened
  over, the speaker button and the queue button. The bar shows the cover at 80 px, title and
  artist, play or pause, next, and the volume; a touch on its cover or its texts opens the
  now-playing screen over the view on top, Home or a library screen, or, under the queue, which
  now playing opened, goes back to it. Both build from `Cover` and `Controls`, so the rules are in
  one place: a control is live only while the playback lists its command or the volume allows
  the step, and one that is not is `LV_STATE_DISABLED`, which LVGL sends no click. A play or
  pause shows at once; next and previous wait for the speaker's event. A muted group shows as
  muted, and up stays live while it does, also at the top of the range: a press on it unmutes the
  group, and the icon shows unmuted at once (section 4). What came of a press is said for five
  seconds, once, by the now-playing screen or, under any other view, the bar (`PressNotice`): a
  queued item's failure in the backend's words before the rest, then "That did not go through" for
  a command, a change of the volume, of a group or of a favourite, a request to queue or a Clear
  that was not taken, then "Added to the queue".
- **Home** is a `ListView` with no way back: its heading holds "Favourites" at Medium 40, then
  the motion setting and the entry into the library, "Library", which opens the artists. The
  motion setting is a 64 px button whose icon alone shows how it is set: the sensor in the accent
  while motion wakes the screen, struck through in the secondary text's colour while only a touch
  does. A press switches it, and the display stores it (section 4); the write is synchronous on
  the UI task, so it has no on-its-way look. Under the heading every favourite is a tile, 160 px
  wide, four to a line, its picture at 160 over its title, one line at Regular 32; one that is not
  available shows its title dimmed, asks for no picture and opens nothing. A tile asks for its
  picture only while any of it is in the list's box, and lets go of it out of view: the tiles look
  as the list is built, as a scroll of it ends, and at every change of the model. While there are
  none, Home says how a favourite is added, under a heart's outline. Loading, failure with Try
  again, and the pages are a list's, as below. A press on an available tile opens a sheet over
  Home, its backdrop dimmed: a way back and the favourite's title, and under them the presses of
  its item, Play, Shuffle, Add to queue and the favourite toggle, after "Albums" for an artist or
  "Tracks" for an album, which close the sheet and open the library's screen of them over Home,
  its heading's toggle showing the favourite the sheet's showed; a playlist has the presses
  alone. A press beside the sheet closes it, as its way back does. The sheet's toggle shows the
  favourite that marks the item as Home holds it: the tile's own, then what the backend took of
  this display's presses on it. A mark made elsewhere shows on the tiles once they come again, not
  on an open sheet.
- **The library and the queue.** Each is a `ListView`: a heading with a back button and the title
  at Medium 40, and rows of 72 px with their titles at Regular 32. It holds a `PagedList` and builds
  a row for each item held; within a screen of an end with a page past it, it asks for that page,
  and the list says, past its rows, that the page is on its way, or why it did not come with Try
  again. Asked for again (`ListView::refresh`), the pages held come one at a time, the one at the
  top of the list first, each taking its own place, so the rows of the other stay drawn while it
  is on its way; a page that comes shorter than a page ends the list there. Rows carry no covers. Home's library entry opens the artists, as the backend sorts them;
  an "A–Z" button beside them opens a grid of the letters the rows file under, 9 by 3 over the
  list, each as large a target as the grid allows, and a letter brings its first artist to the
  top. A button under it opens the playlists. A
  press on an artist's row opens its albums, newest first, each with its year where it has one; a
  press on an album's row opens its tracks. An artist's row holds its name alone, and an album's
  its title and year, the title cut with dots where the room ends. An artist's and an album's
  Play, Shuffle, Add to queue and favourite toggle end the heading of the screen that opens on it,
  its albums or its tracks, the title cut with dots a gap before them
  (`ListView::create_heading_presses`). A playlist opens nothing, since the backend does not list
  its tracks, so its row ends in them. The toggle is a heart while a favourite marks the item and
  its outline while none does, and each is 64 px (`ItemPresses`). A press on a track's row plays
  its album from that track (section 4), and the row ends in Add to queue. A press on a button
  does not open its row, and a drag that starts on a button or a track's row still scrolls the
  list. A press shows as on its way from the press until the backend answers it, or the time limit
  settles it: a turning spinner in the button's place (`PressButton`), as on the speakers screen's
  group control, or, for a track's row, before its Add to queue; a second press on it sends
  nothing. The heading's presses stay while the list is built again. The artists, albums and
  playlists apply at once what the backend took of this display's presses on the toggles, and ask
  again for the pages they hold whenever `Model::favourites_changes` moves (section 4); an
  artist's albums and an album's tracks ask for the item of their heading as well. The queue
  lists each entry's title and, beside it, its artist. It opens on the page holding the loaded
  track, that row at the top and marked in the accent; while no track of the queue is loaded, a
  line under the heading says it plays from another source, or the screen says the queue is empty.
  While it shows it asks again for the pages it holds when `Model::queue_changes` moves, and once
  the event stream is back after a loss. Clear ends its heading and asks to be confirmed over the
  list.
- **`Theme`** sets up the dark theme once, with `lv_theme_default_init(disp, primary, secondary,
  true, font)`, and holds the palette and the type scale as named constants. Starting values:
  background `#101214`, surface `#1C1F24`, text `#F2F2F2`, secondary text `#A0A6AD`, accent
  `#E8A33D`. The milestones' slices own the look and change these freely.
- **`Messages.h`** holds every string the UI shows, in English, one macro each, as PaperClock
  does.
- **`lv_support`** adds to LVGL's API, among others `lv_label_set_text_lines`, for a label whose
  text changes: LVGL cuts a dotted label to the height it has when the text is set, and one sized
  by its content keeps the height of the text before, so the height is set from the text.

### The build

```
sim/
  CMakeLists.txt    C++20, -Wall -Werror; FetchContent of lvgl at tag v9.2.2 and of cJSON 1.7.19;
                    globs ../main/ui/*.cpp, ../main/ui/fonts/*.c and ../main/api/*.cpp
  lv_conf.h         LVGL's template with the same changes as sdkconfig.defaults, plus LV_USE_LODEPNG 1
  main.cpp          display, tick, touch, scene loop, PNG writer
  SimHost.{h,cpp}   UiHost on fixtures
  CardFolder.h      a new, empty folder of the host's in the TF card's place, removed after use
  scenes.{h,cpp}    the scenes
```

- **Warnings are errors** here, where the firmware build lets some pass: two LVGL enums combined,
  such as `LV_PART_MAIN | LV_STATE_PRESSED`, fail the render's build, so such a selector is cast
  to `lv_style_selector_t`.

- **Display:** `lv_display_create(800, 480)` with one full-frame RGB565 buffer in direct mode and
  a flush callback that only calls `lv_display_flush_ready`.
- **Time:** `lv_tick_set_cb` returns a counter the program advances itself, so a scene renders
  the same on every run.
- **A scene** is a name and a function that builds the screen from fixtures, for example
  `loading`, `speakers`, `now-playing`, `now-playing-radio`, `bar`, `artists`,
  `queue-screen-empty`. One view has several scenes where its states differ. A fixture is a body
  as the backend sends it (`SimHost::answer_speakers(status, body)` and its siblings), and `expect()` in
  `sim/scenes.cpp` fails the render on one that reads wrong.
- **A scene can touch and check.** `host.tap(x, y)` presses for 100 ms as the scene starts, and a
  second touch comes 200 ms after the first; `host.drag(from, to)` touches and moves, and
  `host.wait(ms)` puts the next touch later. The scene's optional `check` runs once it has run,
  with the host and the Shell, and fails the render with the string it returns. A scene runs for
  500 ms unless its `run_ms` asks for longer.
- **Rendering a scene:** build it, advance the clock by its run time in 10 ms steps calling the
  Shell's `update()` and `lv_timer_handler()`, run its check, call `lv_refr_now(disp)`, convert
  the buffer to 24-bit RGB, encode it with
  `lodepng_encode24` and write the file with stdio. LVGL bundles lodepng with its encoder compiled
  in, so no other library is needed; its `lodepng_encode24_file` writes through `lv_fs`, which has
  no driver here.
- **Artwork in scenes** is generated by `SimHost`: a deterministic two-colour pattern at the
  requested size, in the backend's form, read through `main/api/Artwork` as the device reads it.
  No image files are committed. Every `SimHost` has a card that is on: `ArtworkStore` over a
  `CardFolder` of its own, so every scene starts with an empty card and renders the same each
  time. `SimHost(card_folder)` over a folder a first host wrote to is the display after a restart.
  The card answers a read and writes a picture at once; a scene can hold either
  (`hold_card()`, `hold_card_writes()`) and then answer, write or drop it itself.
- **Use:** `render --all --out <dir>`, `render <scene>... --out <dir>`, or `render --list`. It
  exits non-zero when a scene is unknown, an LVGL assertion fires, LVGL logs a warning or an
  error, or a scene's fixture or touch does not do what the scene says.

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

**What it does not show:** timing, scrolling speed, touch under a finger, the panel's colours,
memory use. Those are seen on the display.

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

The first display came up this way. Another display needs steps 3 to 6 only:
[adding-a-display.md](adding-a-display.md) has them as IoT Support does them today.

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
   speaker picker.

Without IoT Support, the same first flash is `idf.py -p <port> flash` from a workstation with
ESP-IDF 5.5.3.

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
  `HttpClient`, and in `CardTask` for a picture read from the TF card.
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
| Artwork waiting to be written to the TF card | 0.625 MiB | `ArtworkLoader::MAX_UNWRITTEN_BYTES`; a picture the cache also holds counts in both |
| **Total** | **about 7.6 MiB** | 0.4 MiB is left |

The build must fail when the app image grows beyond 2.5 MiB, well before the 3 MiB slot is full:
a post-build step in `CMakeLists.txt` that checks the size of `sonos_display.bin`.

### Internal RAM

Task stacks take about 80 KB: the tasks of section 1 but the card's, whose stack is in PSRAM,
esp-mdm's, WiFi's and the system's.
An open TLS connection to the backend holds about 4 KB of internal RAM, its buffers being in PSRAM:
on 2026-11-18 the first display had 102,707 bytes free with no client's connection open and 98,539
with one (section 4).
The panel's two bounce buffers take 32,000 bytes (section 3). The model of the open list takes up
to 40 KB. The TF card keeps what it can in PSRAM: its task's stack, its queue and the work that
waits in it, and FAT's buffers (`CONFIG_FATFS_ALLOC_PREFER_EXTRAM`, which `TfCard.cpp` requires).
What it keeps in internal RAM is its 512-byte transfer buffer, its task's control block, the SPI
driver's code that runs from RAM, and, for each write that waits, its id's text and two
`shared_ptr` control blocks. With it the build keeps 3,544 bytes more in internal RAM than without
it, by `idf.py size`. A mounted card adds the driver's own allocations, among them a DMA buffer of
a block in internal RAM (ESP-IDF's `sdspi_host.c`), which only a display measures. The target is
at least 60 KB of internal RAM free with the largest list open. The firmware logs
`heap_caps_get_free_size` and `heap_caps_get_minimum_free_size` for both kinds of memory once a
minute; the lines reach IoT Support's device log, where every milestone can read them.

### Artwork

The display decodes nothing. An image arrives in the form section 4 names, is read straight into
its PSRAM buffer, checked (a 200, magic, colour format, flags `0`, width and height as requested,
stride 2 × width, length equal to 12 + width × height × 2; a set flag would have LVGL decompress
the pixels), and handed to LVGL as an `lv_image_dsc_t` pointing into that buffer.
`CONFIG_LV_CACHE_DEF_SIZE=0`: LVGL has nothing to decode or cache. `main/api/Artwork` holds the
checks (`read_artwork`), the cache (`ArtworkCache`) and `ArtworkLoader`, which answers a view's
`UiHost::request_artwork`.

| Size | Used for | Bytes each |
|---|---|---|
| 400 × 400 | now playing | 320,000 |
| 160 × 160 | Home's tiles | 51,200 |
| 80 × 80 | list rows, the now-playing bar | 12,800 |

The cache holds images by artwork id and size, capped at 1.5 MiB in total. That is, for example,
two large images and sixteen tiles. An image a view holds is pinned: the view holds the `Artwork`,
a shared pointer, for exactly as long as a widget draws from it. When the cap is reached the
least recently used unpinned images are freed; when the pinned ones leave no room, the new image
is not kept, the view is handed `nullptr`, and its widget keeps the placeholder, a flat tile with
an icon. A picture the backend did not give, or that was not kept, is asked for again at the next
change of the model, never on a timer. A view asks only for what is visible, and its request waits
only while the view lives and wants that picture (its `scope`, as in section 4). Nothing is read
from the card or fetched while the screen is dark: what is asked for meanwhile, and still wanted,
is read or fetched when it wakes.

`ArtworkLoader` answers from the cache first, then from the board's TF card (below), then from the
backend: one read of the card and one download at a time, in the app's own scope, and neither
waits for the other. A request the cache cannot answer waits for its read, and what the card has
no picture for goes on to the downloads. A view that asks for a picture whose read or download is
under way or waits to start waits with it, so a picture is read or downloaded once at a size
however many views ask. A read or a download that has not started when no view wants it any
longer is never made. One under way is taken to its end and its image kept, whether or not a view
still waits for it, so a cover left before it arrived shows from the cache when it is asked for
again; the log then says "Artwork at … px is in with no view waiting for it, kept", or "The
card's artwork at … px is in with no view waiting for it, kept". A download whose answer never
reached the UI task is found at the UI task's next pass, and the views that wait on it are handed
`nullptr`, as for one the backend did not answer; a read lost that way is a picture the card does
not have, and is downloaded.

A picture from the card passes the checks of a download before it is kept. One that fails them is
erased from the card and downloaded; one that does not fit the cache beside what is on screen
hands its views `nullptr` and stays on the card. Every download that passes its checks is also
handed to the card to write, whether or not it fits the cache; a picture from the cache or the
card never is. What is handed over waits in PSRAM until the card's task has written it, and
together it may take at most 640 KiB (`ArtworkLoader::MAX_UNWRITTEN_BYTES`), two now-playing
covers and a few small pictures. A download past that is not handed over, the log says "Artwork at
… px is not kept on the card", and the picture is downloaded again after a restart, or once the
cache has let go of it. Without a card, or with one that is off, every picture is downloaded; a
full card is still read, and is handed nothing.

Home's list is 288 px tall and a line of tiles about 228 with its gap, so at most three lines,
twelve tiles, are in view at once, and Home holds at most twelve pictures at 160: about 600 KB
beside the now-playing screen's cover and the bar's, which stay held while now playing is open over
it, all under the cap. The downloads waiting to start wait in the loader, which gives the artwork
client one at a time. A quick scroll while the first pictures are on their way asks for more; a
download that no view wants any longer by its turn, such as a tile's scrolled out of view, is never
made.

### The TF card

The board's TF card keeps the covers the display downloads, so a cover it has shown once is
drawn again without the network, also after a restart. The key is the artwork id, the size and
the format the backend is asked for (`ARTWORK_FORMAT` in `Application.cpp`, `lvgl`), so a picture
the card holds costs no request at all. The card is used for this alone: a display takes a card
kept for it (`docs/adding-a-display.md`).

- **The mount.** `TfCard` (`main/board/TfCard`) sets up SPI2 on the pins of section 2, with no
  chip-select line, and mounts FAT at `/sd`, one file open at a time. It runs on the card's own
  task, which `do_begin()` starts before the UI task, so a card that is missing, slow or failing
  never stops or holds up the boot. A card FAT cannot mount is mounted a second time with a format
  allowed, and ESP-IDF formats it where FAT finds no file system on it, such as an exFAT card's,
  since ESP-IDF's FAT leaves exFAT out. The format wipes the card and takes allocation units of
  64 KB, so its FAT is small: 4 MB on a card of 32 GB. An empty slot answers nothing, MISO being
  pulled up, and the mount times out. A card that is not mounted gives its SPI bus back.
- **The files.** `ArtworkStore` (`main/api/ArtworkStore`) keeps a picture in
  `/sd/art/<2 hex>/<8 hex>.bin`, named from FNV-1a, 64 bits, of the key
  `<format>/<size>/<artwork id>`: its top byte is the folder and its low 32 bits the name. The
  ids differ in case and FAT's names do not, and FAT's long names are off, so a name is a hash in
  8.3. A file is a 16-byte header (`SDAW`, version 1, the key's length, the picture's length, the
  CRC-32 of the picture, little-endian), then the key, then the picture's bytes as the backend
  answered them. Two keys that share the 40 bits take each other's place at every read; among
  10,000 pictures the chance that such a pair exists is about 1 in 22,000.
- **A write** goes to the one file `/sd/art/write.tmp`, which is closed and then renamed into
  place, so a power cut leaves no picture half written; the start removes the temporary file one
  left. A picture the card holds is not written again, and the card holds nothing else, no index
  and no log, so nothing writes the same spot over and over.
- **A read** checks the file's marker, version, key, length and CRC. A file that is not there is
  a miss; one that does not match is a miss too, and is deleted. Every byte to and from the card
  passes through the 512-byte buffer in internal RAM, a sector at a time: the SD driver is slow
  into and out of PSRAM, where the pictures are.
- **No cleanup.** Nothing is ever evicted. A card with no room, ENOSPC or EACCES at any step of a
  write, stops the writes until the next restart: the temporary file is deleted, one line is
  logged, and reads go on. Any other error of the card turns it off until the next restart, and
  every picture is downloaded. A file a power cut damaged fails its checks and is a miss; a card
  a power cut left with no file system FAT finds is formatted at the next start.
- **The card's task**, `card`, makes every access to the card: the mount, a format, the clearing
  at start, and then the reads, writes and erases the UI task asks for, in the order it asked, from
  a queue of 64. So a read of a picture comes after its write, and the erase of a failed picture
  before its new write. The UI task never waits on it: what the queue has no room for is let go of
  at once, a read then being downloaded and a write not kept. A read's answer reaches the UI task
  through `get_queue().enqueue()`, which waits for room, so none is dropped. The task touches no
  LVGL, model, I2C or NVS. Its state reaches the UI task as an atomic: `is_on()` is false until the
  mount is done, without a card and after an error, and `is_writable()` holds only while nothing
  has stopped the writes. Until the UI task hears that the card is full, downloads are still handed over, and the
  store refuses each.
- **The log.** What the mount found is logged at the mount and again from `do_ready()`, since the
  mount may come before esp-mdm ships the log: "The TF card is mounted: … MB"; "The TF card could
  not be mounted, and is mounted after a format", when the first mount failed and the second,
  which may format, succeeded; "No TF card answers (ESP_ERR_TIMEOUT)"; or "The TF card cannot be
  mounted (…)". A cover from the card is "Read artwork at … px: … bytes in … ms", then "The
  card's artwork at … px is in"; a write is "Artwork at … px is on the card: … bytes in … ms". A
  full card is "The TF card is full, and nothing more is written to it", an error "The TF card is
  off until the next restart", each with the step that failed and its errno.

### Lists

- **At most 200 rows are built at a time.** The firmware asks for pages of 100 and keeps two
  (`PagedList`). Scrolled to within a screen of an end, a list asks for the page past it; once it
  comes, the build drops the page at the far end, or, when a row on screen is in that one, the
  page that came, so the rows on screen stay where they are.
- **The library fits whole.** 94 artists, an artist's albums and an album's tracks are each under
  200, so the A–Z bar jumps within rows that are already there. Over 200 artists, its letters are
  those of the rows built.
- **The queue need not fit**: playing one of Plex's large playlists, one at 12,019 tracks, queues
  it whole. It is windowed the same way, and opens on the page holding the loaded track
  (`locate=current`). A playlist's own tracks are not listed: the backend does not list them.
- **A response body is capped at 256 KB**, about ten times a page of 100 items.

### Fonts

**Pre-rendered bitmap fonts**, not PaperClock's run-time TrueType. A bitmap font costs flash and
no heap, draws without rasterising, and is the same bytes in the simulator and on the device.
The cost is a fixed set of sizes.

- **Faces:** Roboto Regular and Medium for text, Material Design Icons for icons. Both are
  Apache-2.0, and their licence files sit beside the TTFs in `tools/fonts/`. PaperClock's
  FontAwesome files are not copied into this repository.
- **Characters:** Basic Latin `0x20-0x7E`, Latin-1 Supplement `0xA0-0xFF`, Latin Extended-A
  `0x100-0x17F`, which is "Latin script with accents", and the punctuation, signs and sub- and
  superscripts the library's titles hold: dashes and quotes, the bullet, the ellipsis, primes, the
  euro, the modifier apostrophe and a few of maths (`TEXT_RANGES` in `tools/fonts/generate.sh`),
  about 350 glyphs. A character outside them would draw as LVGL's placeholder box, so a text the
  backend sends is read without it (section 4): `can_draw()`, in `main/ui/Theme.cpp`, asks the
  four text fonts. A text font added to the generator takes the same ranges, and a face other
  than Roboto Regular or Medium joins `can_draw()`'s fonts.
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
| Direct render mode shows no stale or torn areas when a view changes and when a list scrolls | Artifacts on screen. Their cause was the restart in every vblank, not direct mode (section 3); the swap is also made safe now (section 2). With that fixed, the picture drifted at power-on, until the next wake (section 3). Holds since `f2217d1`: the operator's "Works" after a power-on and a wake, 2026-11-15 | `LV_DISPLAY_RENDER_MODE_FULL`, ThermostatDisplay's mode; it would not have helped here |
| `CONFIG_LCD_RGB_RESTART_IN_VSYNC` gives a steady picture, and the picture is right again after an update was installed with the screen lit | Failed: on the S3, without bounce buffers, it pins the picture to fb0. Taken out | done: the restart on every wake remains |
| The new I2C driver reaches the expander at `0x24` and `0x38` and the touch controller at `0x5D` or `0x14` | The expander holds: the backlight is switched through it. The touch controller answered at bring-up, when the board had only been reset by esptool, but not after the operator's power-ons: it was probed before it left reset. It is now probed until it answers, and the result is logged where IoT Support gets it (section 2). Holds since `f2217d1`: `Touch: GT911 at 0x5D` after a power-on, and the operator's "Works" | none needed |
| Touch coordinates match what is drawn, corner to corner: the skeleton's placeholder view drew a dot where each touch landed | Holds: the operator's "Works" at bring-up | `swap_xy` or the mirror flags of the touch configuration |
| No brown-out reset in IoT Support's device log after a day of waking and sleeping at 240 MHz | One boot so far, `ESP_RST_POWERON`, then 7 minutes without a reset. The day is still to come | `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160=y` |
| Free internal RAM at the skeleton's placeholder view is above 100 KB | Holds: 144,735 bytes free with the event stream connected, at least 140,011 since boot | lower the UI task's stack to 12 KB; read the per-task high-water marks |
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

Milestone 1 is the first firmware to use the backend, and what only a display shows of it is
still to check there: the backend's connections on the handed root, the speaker choice's NVS read
and write, a `POST` with an empty body and its 204 through `esp_http_client`, the events and every
timing, the covers' fetch and how they draw, and the bar's touch targets under a finger. Also to
watch: that `esp_http_client_read` returns `-ESP_ERR_HTTP_EAGAIN` on a quiet stream as its header
documents, and that the 75-second idle limit holds against the deployed heartbeat.

Milestone 4's changes of groups have reached no real speaker: the operator's first try of the
speakers screen on the display is the first join and the first leave. What only that shows is
still to see there: the `PUT` and `DELETE` and the join's body through `esp_http_client`, how long
the speakers take to report the new groups against the 15-second limit, and what becomes of the
rest of a group when the speaker that leads it leaves.

What only a display shows of milestone 2's library and queue, and of the restarts of section 1, is
still to see there: the "Raw reset reason …" and "The run before …" lines after a restart the
firmware makes itself, such as the one a saved configuration brings, and what they say at the next
watchdog reset; whether a press still waits behind a stalled fetch on weak WiFi; the lists'
scrolling, the A–Z grid and the rows' buttons under a finger; Play, Shuffle, Add and Clear on a
speaker, the queue's mark, and its refetch on the events; up on a muted group; and free internal RAM
with the largest list open, against the 60 KB target.

What only a display shows of milestone 3's favourites and Home is still to see there: a mark's
`POST` and an unmark's `DELETE` through `esp_http_client`, and the lists and Home fetched again on
`favourites_changed`; how long a press shows on its way; the motion setting's NVS write, and
that, switched off, motion neither wakes the screen nor keeps it lit; Home's tiles and their
pictures, how much of a title Regular 32 leaves readable, whether the motion setting's icon reads
as the setting without a label, the sheet under a finger, and free internal RAM with Home's
pictures held.

The bounce buffers (section 3) held at their try-out on 2026-11-17, over three power-on starts, a
sitting of use and three wakes: "Works: The picture stayed put, and scrolling and presses felt as
before". What only a display shows of them is still to see there: that a view that changes and a
list that scrolls show no stale or torn area with the swap of section 2; free internal RAM with
the largest list open, against the 60 KB target, with their 32,000 bytes taken; and whether the
picture stays put with the panel restarted on waking alone, through power-on starts, a wake, a
speaker chosen and the motion setting switched.

Every update restart on the firmware with bounce buffers, from `9986f59`'s to `02b0c66`'s, stalled
until the second stage of the restart's RTC watchdog reset the chip, raw reason 16 (SDSP-20).
The halt before a restart (section 1) is the remedy, from `846062f`. What only a display shows of
it is still to see there: the restart that installs the release after `846062f`, which should read
raw reason 12 at the next boot, with "The run before's restart halted the scan-out" and how the
data cache's preload ended.

The connections kept open (section 4, SDSP-7) were seen on the first display on 2026-11-18, with
`f549185`, the screen dark, through the API client's fetches of the volume after steps made on the
Garage speaker's group from a pod. A request on a connection kept 16 or 44 seconds took 21 ms. The
client closed its connection after 50 seconds idle, and the next opened in 44 to 49 ms, resumed;
one 11 minutes on, past nginx's session, opened in 1,338 ms. The backend logged each fetch 0.18
seconds after the step on a kept connection, and 1.54 seconds after it on the full handshake.
Free internal RAM was 102,707 bytes with no client's connection open and 98,539 with one, at least
90,015 since boot, against 74,879 dark and 62,451 at least on `4ad3694`, whose clients held their
last connections. What only a hand shows is still to see there: the press's connection opened
as the screen wakes, by motion and by touch, and the API's after it, the checks that keep them
open while it is lit; a press's time from its "Sending" line to its answer; whether the picture's
shift in use changes with power save off; and free internal RAM with the largest list open,
against the 60 KB target.

What only a display shows of the library's headings and of the covers kept is still to see there:
a press on a track's line, which should replace the queue with the whole album and start at that
track on the speaker; the headings' presses and the tracks' lines under a finger; and, over the
display's own link, now playing left before its cover arrived and opened again, which should show
the cover without a second download while the log says the cover was kept.

Nothing of the TF card (section 7) has run on a display yet. What only a display shows of it is
still to see there, with a card that holds nothing to keep: the mount's line after a power-on and
after a restart, which shows the card answering in SPI mode while it is selected from the
expander's first write on, and the SD driver's commands sent from a stack in PSRAM; an empty slot,
which should log a timeout; the format of a card FAT cannot mount; covers drawn from the card
after a restart with no download in the log, and how long a read and a write take a sector at a
time; the errno FAT gives a full card; a power cut during a write; and free internal RAM with a
card mounted and the largest list open, against the 60 KB target, with the driver's own
allocations on top of the build's 3,544 bytes.
