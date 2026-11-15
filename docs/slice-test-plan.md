# Slice test plan

How a slice is proven once its phases are merged. The run loop's test phase executes this
document.

**The push is the release.** A push to `main` runs the Jenkins job `Firmware/SonosDisplay`, which
builds the firmware and uploads it over the air through IoT Support. IoT Support then tells every
display of the model `sonos_display` to restart and install it. There is no dev stage. So
everything that can be verified is verified before the push, and what only a display can show is
owed to the operator after it.

No gate is known red. A red gate is the slice's.

## 1. The gates

Run, from the repo root, each on its own and without a pipe (`kc project` prints its output only on
failure, and a pipe reports the last command's status):

```sh
kc project build
kc project test
kc project lint
```

- `build` ends with the image size against its 2.5 MiB limit. Record the size; a slice that adds
  more than about 100 KB says why in its close-out.
- `test` writes `build-sim/screens/<scene>.png`, one per scene. A missing scene, an LVGL warning or
  an ESP-IDF include in `main/ui/` fails it.
- `lint` runs `arch-validate` (it needs outbound HTTPS; exit 2 is the service unreachable, not an
  invalid model) and clang-format.

## 2. The screens

Read every PNG of a scene the slice added or changed, and check it against the slice's
requirements:

- what the requirement says is on the screen is there, in the dark theme;
- no label is cut off or runs into another, and no glyph shows as LVGL's placeholder box (a
  character outside the fonts' ranges);
- the now-playing bar, once it exists, is on every screen that should carry it;
- touch targets are large enough to hit in a workshop: about 64 pixels or more.

A screen the requirements describe and no scene renders is a finding: the slice owes the scene.
List the changed PNGs in the verdict; the close-out attaches them.

## 3. The diff

Read the slice's diff against the rules in [change-discipline.md](change-discipline.md), in
particular: nothing but the UI task touches LVGL, the model, I2C or NVS; data from the backend is
checked where it enters and never crashes the display; large buffers are in PSRAM and lists are
windowed; an LVGL option changed in one configuration is changed in the other.

## 4. The backend, read-only

Where the slice's code calls a backend endpoint, call it once from the pod with `curl` against the
deployed SonosController and compare the response with what the firmware parses: field names,
types, paging, the error envelope. The backend's address is SonosController's own; find it from
that project, and never write it into this repo. GET requests only. A command that changes what
the speaker does is run only against the Garage speaker, at low volume, and leaves its queue,
position and volume as they were found; Zitkuil is a living space and is not touched.

## 5. Push, then follow the build

Push what the slice committed. `/dev:run-slice` asks the operator first while auto pilot is off,
since this push deploys.

Then follow the build to its end:

```sh
timeout 3000 track_build.py Firmware/SonosDisplay --hash "$(git rev-parse HEAD)"
```

Green means the firmware is uploaded and the displays have been told to install it. A red build is
a blocking finding: name the stage. `Build firmware` red is the slice's code or its build files.
`Deploy firmware` red is the upload: IoT Support refused it or could not be reached, and the
displays keep the previous firmware. A slice that changed `docs/architecture/` also follows
`AaC/SonosDisplay` the same way.

## 6. The operator gate

What only a display shows is owed to the operator. Close the test phase by writing, in the verdict
summary, what to check on the display after it has installed this build, one line per item: which
screen, what to do (touch, wait, scroll), and what should happen. Typical items are touch accuracy
on a new control, scrolling a long list, the screen waking and sleeping, artwork appearing, and
the display coming back by itself after the update. The device's boot log, with the firmware
version it runs, is on its Logs tab in IoT Support.

Mark the `verification.json` items that need a display as owed to the operator, each with the check
that settles it. Do not mark them verified. If the operator has already checked and reported back
within the run, record what they reported as the evidence.

## Findings

A failure on a display is never a flake. A gate that cannot run (no network for the LVGL download
or for `arch-validate`, Jenkins unreachable) is `blocked`, with the command and its output, not
skipped.
