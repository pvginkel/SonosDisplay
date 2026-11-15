# Change discipline

The rules every change to SonosDisplay keeps. The design they protect is in
[firmware-design.md](firmware-design.md); a change that finds one of its decisions wrong changes the
decision there too, and says so.

## Change it, don't wrap it

The firmware has one backend, SonosController, and the contract between them is the backend's
OpenAPI document as the deployed backend serves it. When the contract changes, the firmware follows:
no fallbacks for an older backend, no second code path kept "in case". The backend is deployed
before the firmware that uses it, so the firmware never has to speak to an older one.

Inside the firmware the same holds. A renamed class, a moved file, a replaced view is changed
everywhere in one change. Dead code is deleted, not commented out, and nothing is kept for a future
that has not been planned.

## No defensive caveats, and one boundary

Code does not guard against what cannot happen. `ESP_ERROR_CHECK` is for a programming error or a
board that cannot start; an LVGL assertion restarts the device on purpose.

What comes from outside the firmware is the exception, and it is checked where it enters: an HTTP
response, a JSON body, an event frame, an artwork header, IoT Support's device configuration. Bad
data from there never crashes or restarts the display. It is logged and shown: the view says what
is wrong, and the device stays usable. A backend that is down never restarts the device. An answer
the API's document does not allow is refused whole; one it allows is never refused for a field the
display does not show.

## The structure's rules

- **One UI task.** Only it touches LVGL, the model, I2C and NVS. Every other task hands it a
  closure through `get_queue().enqueue(...)`. No LVGL lock is added.
- **`main/ui/` includes LVGL and the C++ standard library only.** Its doors to the rest are
  `Model.h` and `UiHost.h`. `kc project test` fails on anything else, and that is what lets `sim/`
  render the screens.
- **`main/backend/` knows nothing of `ui/` or `Application`**, so it can move to `esp-libs` as it
  is when a second firmware needs it.
- **`main/api/` is where the backend's answers are read**, into `ui/`'s types. It includes cJSON,
  LVGL, the C++ standard library and `ui/`'s two doors only, so `sim/` builds it and the scenes
  read their fixtures as the device reads the backend; `kc project test` fails on an ESP-IDF
  include there.
- **`main/board/` is the only code that talks to the hardware.** `ScreenPower` is the only code
  that switches the backlight, and never twice within a second.
- **Memory.** Anything large or numerous goes to PSRAM: response bodies, artwork, JSON trees,
  LVGL's heap. At most 200 list rows are built at a time. The image stays under 2.5 MiB; the build
  fails past it.
- **LVGL is configured twice**, in `sdkconfig.defaults` and in `sim/lv_conf.h`. An LVGL option
  changed in one is changed in the other in the same commit.
- **Generated files are generated.** `main/ui/fonts/` comes from `tools/fonts/generate.sh`; an icon
  or a size is added there, never by editing the output.

## Style

`main/.clang-format` (Google, four spaces, 120 columns) over `main/` and `sim/`, checked by
`kc project lint`. PascalCase files and classes, snake_case methods, `_member` fields,
`LOG_TAG(Class)` at the top of each `.cpp`, `esp_err_t` returns with `esp-support`'s `error.h`
macros, no exceptions. Each file includes what it uses. Every string the UI shows is in
`ui/Messages.h`.

## What "tested" means here

The gates are `kc project build` (the firmware compiles and fits), `kc project test` (every screen
renders to a PNG with no LVGL warning, every scene's fixtures and checks hold, and `ui/` and `api/`
keep their include rules) and `kc project lint`
(formatting and the architecture model). They prove the code builds and the screens draw. They do
not prove timing, touch under a finger, the panel's colours, memory on the device, or anything the
backend and the speaker do. Those are proven on a display, by the operator. Never describe device
behaviour as verified on the strength of a green gate or a rendered PNG.

A slice that changes a screen adds or updates the scenes in `sim/scenes.cpp` so that every state
it adds renders.

## This is a public repo

`github.com/pvginkel/SonosDisplay` is world-readable. No token, secret, hostname, IP address or
other address of the operator's network goes into it: not in code, comments, fixtures, scene data
or commit messages. The backend's address reaches a display through IoT Support's device
configuration, never through the build.
