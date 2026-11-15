# SonosDisplay

Firmware for a touch-screen Sonos controller in the workshop: a Waveshare ESP32-S3-Touch-LCD-4.3
(800x480 RGB panel, GT911 touch, motion sensor) on which the operator picks music from the Plex
library, plays it and sees the queue. It talks plain HTTP and server-sent events to the backend,
SonosController, and is managed by IoT Support through `esp-libs`' esp-mdm: provisioning, the
device configuration, OTA updates, log shipping. ESP-IDF 5.5.3, C++20, LVGL 9.2.2.

The design is [docs/firmware-design.md](docs/firmware-design.md). Read the section a change touches
before making it.

## Layout

- `main/board/`: the hardware and LVGL's driver interface. Every pin and timing is in
  `BoardConfig.h`.
- `main/backend/`: `HttpClient` and `EventStream`, free of app types.
- `main/api/`: the backend's answers read into `ui/`'s types, and the artwork cache; cJSON, LVGL,
  the C++ standard library and `ui/`'s doors only, so `sim/` builds it too.
- `main/ui/`: the views, LVGL and the C++ standard library only; `Model.h` and `UiHost.h` are its
  two doors.
- `main/Application.{h,cpp}`: the app on esp-mdm's `ApplicationBase`; owns the UI task.
- `sim/`: the native render of `main/ui/` to PNG files.
- `tools/fonts/`: the TTFs, their licences and `generate.sh`, which writes `main/ui/fonts/`.
- `docs/architecture/`: the architecture model, built by `AaC/SonosDisplay`.
- `../esp-libs`: the shared components, beside this repo. `../SonosDisplaySpecs`: the spec repo,
  slices and plans; a separate git repo, committed early and often.

## Gates

- `kc project build`: `idf.py build`; fails past a 2.5 MiB image.
- `kc project test`: renders every scene in `sim/scenes.cpp` to `build-sim/screens/<scene>.png`.
  Read the PNGs a change touches. A single scene: `build-sim/render <scene> --out build-sim/screens`.
- `kc project lint`: `arch-validate` and clang-format.
- Fonts: `cexec frontend tools/fonts/generate.sh`; the output is committed.

A push to `main` is a release: `Firmware/SonosDisplay` uploads the firmware to every display over
the air.

## Rules

[docs/change-discipline.md](docs/change-discipline.md) holds them all. The ones broken most easily:

- Only the UI task touches LVGL, the model, I2C and NVS; other tasks use `get_queue().enqueue()`.
- An LVGL option changed in `sdkconfig.defaults` is changed in `sim/lv_conf.h` in the same commit.
- Public repo: no token, hostname or address of the operator's network, anywhere.
- A green gate proves the build and the screens, never the behaviour on a display.

Procedure docs of the dev workflow: [docs/slice-test-plan.md](docs/slice-test-plan.md) and
[docs/slice-doc-plan.md](docs/slice-doc-plan.md).
