# A touch-screen Sonos controller for the workshop

The brief for the Pilot project that builds it. Written 2026-11-15 from a design conversation
with the operator; their rulings are quoted where a later reader could doubt them.

## What it is for

The operator has a Sonos speaker in the garage, a hobby woodworking shop, and wants to control it
from a touch screen on the wall: pick music from the Plex library, play it, see and clear the
queue. The screen is one of the displays built earlier for a radiator thermostat, which are no
longer in use. There are several of them, and a second one may go up elsewhere later.

Two things are built:

- **SonosDisplay** (`pvginkel/SonosDisplay`): the firmware for the display.
- **SonosController** (`pvginkel/SonosController`): a Python backend that talks to Sonos and Plex
  and serves the display. It is generic enough that a web UI could be added later;
  no web UI is built in this project.

**The result that establishes it:** the backend runs in the prd cluster, a display in the garage
runs the firmware, and the operator can do everything under "What is agreed" from that display.

**Who works on it:** the operator alone.

## What is agreed

The scope every task holds to. A change to what this section says comes to the operator.

**On the display**

- **Home:** the favourites as artwork tiles, an entry into the library, and a now-playing bar that
  stays at the bottom of every screen (play/pause, next, volume).
- **Library:** artists with an A–Z jump bar, then an artist's albums, then an album's tracks; and
  the playlists. No search and no on-screen keyboard ("I prefer to not have a search option").
- **On every artist, album and playlist:** Play, Shuffle, Add to queue, and a favourite toggle.
  On a track: Play and Add to queue.
- **Where those controls sit.** An artist's and an album's controls sit at the top right of the
  screen that opens on it, not on its line in the list. A playlist opens no list of its own, so
  its controls stay on its line. A track has no Play button: pressing its line plays it. The
  operator, 2026-11-17, at the picture shift's try-out: "I think the controls inline look very
  busy. I would do this differently: I would show these controls at the top right of the screen,
  on the thing opened. So instead of having the controls on e.g. the album line, I would move
  those controls into the album screen, at the top right. This gives more space for the album
  name. The space available now is pretty cramped. The play button on the track line can go
  completely. If the track line is pressed, that can itself be the play action. I see that for
  playlists you don't have a sub list. That's fine. Keep the controls where they are for that."
- **Play replaces the queue; Add to queue appends.**
- **Playing a track plays its album from that track.** It replaces the queue with the whole
  album and starts at the track, as the Sonos app does. The operator, 2026-11-17: "in the Sonos
  app, if I start a single track, it replaces the queue with the album, and starts it at that
  track. So if I jus go into an album and press track 4, the whole album is loaded into the
  queue, and the first three tracks are skipped. I think that's good behavior."
- **Playing an artist plays their albums in release-year order, each in track order.** This is
  how the operator uses the speaker today, so it is the plain Play button, one tap from the
  artist: "Don't shuffle artists by default. Play all in album release year is basically how I
  use the device, so it must still be easy to do that." Shuffle is a separate button.
- **Now playing:** large artwork, title, artist, album, previous, play/pause, next, volume. The
  speaker will be mounted out of reach, so volume on the display matters.
- **Queue:** the list with the current track marked, and Clear.
- **Speakers:** pick the speaker this display controls, and add or remove other speakers from its
  group. The choice is stored on the display itself.
- **Whatever else plays on the speaker** (radio, something started from the Sonos app) is shown
  and can be paused, skipped and turned up or down. The library only ever offers Plex.
- **The screen** is off until the motion sensor or a touch wakes it, and goes off again after a
  while, as the thermostat firmware does today.
- **Waking on motion can be switched off** in a setting on the display, so that only a touch
  wakes it. The operator, 2026-11-16: "The I just want a setting to disable the option sensor. I
  considered and rejected the 2 second option myself. The problem is the sensitivity, not
  anything else. I may be able to do something about that, but that's hardware, not software."
  No clock screen and no motion filtering in the firmware: the sensor's sensitivity is a hardware
  matter for the operator.
- **Dark theme.** Latin script with accents only.

**In the backend**

- **Plex is the only source of music.** Using another library would mean changing the app, and
  that is intended: "if I ever want to get music from an alternative library, I would have to
  change the app. That is desirable."
- **Favourites** (artists, albums, playlists) are stored in the backend and shared by every
  display and any later client.
- **Several displays** are served at once; the backend keeps nothing per display.
- **Generic endpoints**, not shaped around the firmware: "Don't bind it too much to the firmware.
  Assume that maybe I'd add a web UI at some point." What is specific to the device is kept to
  named extras, such as artwork in a form the display draws directly.
- **Artwork is prepared by the backend**, dithering included. The display decodes no images.
- **No authentication:** "The backend can be unauthenticated. Sonos is also, so this does not
  widen security at all."

**Left out:** search, seeking within a track, repeat, a web UI, any source but Plex.

## The design, in short

These are the owner's picks, made to keep the device simple. A task that finds one of them wrong
changes it and says so in its report.

- **The queue lives on the speaker, not in the backend.** The backend holds no playback state of
  its own, so music keeps playing when the backend is down, and whatever the Sonos app does shows
  up on the display.
- **Music is queued through the Plex service that is linked in Sonos**, the way the Sonos app
  does it. The alternative, queueing direct addresses on the Plex server, also works and is the
  fallback (see "What is already known").
- **The backend pushes state changes** to its clients over the template's server-sent events, fed
  by the speakers' own event notifications. The display does not poll.
- **Favourites are the backend's only stored data**, in PostgreSQL as the template provides it.
- **The contract between the two repos is the backend's OpenAPI document**, as the deployed
  backend serves it. Neither repo reads the other's code.
- **The firmware is a new codebase on `esp-libs`**, the way PaperClock is, with the display,
  touch, backlight and motion-sensor code carried over from ThermostatDisplay.

## What is already known

Surveyed or tried on 2026-11-15. Tasks start from this and do not redo it.

**The display hardware** (from `pvginkel/ThermostatDisplay`)

- A Waveshare ESP32-S3-Touch-LCD-4.3: 800×480, 16-bit colour, GT911 capacitive touch, PSRAM.
- The backlight is on or off only, switched through a CH422G expander. There is no dimming.
- The motion sensor is a digital input on GPIO6.
- Known quirks, each with a workaround in that code: the picture drifts while flash is written
  (during OTA), and switching the backlight too fast browns the board out.
- That firmware runs LVGL 8.2 and has every image decoder switched off. Its dithering only
  smooths one generated background gradient.

**The firmware conventions** (from `pvginkel/PaperClock` and `pvginkel/esp-libs`)

- ESP-IDF 5.5.3, C++, LVGL 9.2. `esp-libs` is checked out beside the firmware repo, not as a
  submodule.
- `esp-libs/esp-mdm` holds an application base class that does provisioning from NVS, WiFi,
  the Keycloak client-credentials login, the device's configuration, coredump upload, OTA, MQTT
  and log shipping. The device management service is IoT Support (`pvginkel/IoTSupport`).
- Jenkins builds firmware with the shared library's `espFirmware.build` and `espFirmware.upload`;
  PaperClock's job is `Firmware/PaperClock`.
- PaperClock has no HTTP backend: its data arrives over MQTT. SonosDisplay is the first firmware
  that talks HTTP to an application backend.

**The backend template** (from `pvginkel/ModernAppTemplate`)

- Three Copier templates: the root, the backend and the frontend. The backend is Flask, with
  the database, OIDC, S3 and server-sent events each a switch.
- The root template has no backend-only option: its pipeline and dev setup assume a frontend.
- The template assumes one replica, which is what a listener for speaker events needs.
- Its testing rules forbid mocks of the infrastructure it owns. Real speakers and a real Plex are
  not available in CI.

**Sonos and Plex, tried on the real speakers** (the spike, a script this repository held until
commissioning; its history keeps it)

- A pod in the cluster reaches the speakers and Plex directly.
- There are two rooms: Garage, and Zitkuil, which is three devices acting as one room.
- The music library is called `Muziek`: 94 artists, 853 albums. The playlists include Plex's
  automatic ones, among them one of 12,019 tracks and several empty ones.
- **Through the Plex service in Sonos:** an album, a playlist and a whole artist each went onto
  the queue in one call, as the same items the Sonos app creates. Playback started at once with
  correct title, artist, album, duration and artwork.
- **As direct addresses on the Plex server:** it plays, with title, artist, album and artwork,
  but the track length is unknown to the speaker, the Plex token is stored in every queue entry,
  and it takes one call per track.
- The speaker, Plex address and token used were in the spike's `.env`, which was never committed.

## The plan

Tasks 1 to 6 lay the ground and mostly run side by side. Tasks 7 to 10 are milestones: each ends
with the backend deployed and the display updated, so the operator can use what it added. Within
a milestone the backend's part comes first. Research is part of the task that needs it, and its
questions are listed there.

1. **The SonosController environment.** The public repository `pvginkel/SonosController` exists,
   empty, created on 2026-11-15. It gets a KubeCoder environment of its own, which Pilot creates
   through its fleet access: "You don't need me to create the SonosController environment. Pilot
   app has KubeCoder fleet access." Waits on nothing.

2. **A backend-only switch in ModernAppTemplate.** The root template gets a switch that leaves
   the frontend out of the generated repository (its pipeline, dev setup and environment
   configuration), with the changelog entry the template's conventions ask for. A web UI later is
   then a template update, not a restructuring. The operator agreed to this over a hand-trimmed
   repository. Works in `pvginkel/ModernAppTemplate`. Waits on nothing.

3. **The backend's skeleton, deployed.** Generated from the template with the database and
   server-sent events on, OIDC and S3 off; onboarded for KubeCoder and the dev workflow; its
   deploy repository and Jenkins job in place; one replica answering its health check in prd.
   Research first:
   - which existing app is the nearest example (ZigbeeControl and DHCPApp control devices too);
   - how those apps test against things CI does not have, which decides how Sonos and Plex are
     stood in for in this backend's tests;
   - how a new app gets its deploy repository, Helm chart, database, hostname and Jenkins job.

   Waits on 1 and 2.

4. **How the new firmware is built: research and a design note.** Answers, with the owner's
   decision on each:
   - how a new codebase is laid out on `esp-libs` after PaperClock's example, and what of
     ThermostatDisplay is carried over (panel, touch, expander, backlight, motion sensor) and
     ported from LVGL 8.2 to 9.2;
   - how the two known hardware quirks are handled in the new code;
   - how the firmware calls an HTTP backend and holds its event stream open, and whether that
     code belongs in `esp-libs` or in the app;
   - where the display gets the backend's address from (IoT Support's device configuration, or a
     build setting);
   - how the screens are checked without a device. PaperClock and ThermostatDisplay have a
     Windows-only simulator; the aim here is a build that renders each screen to an image in the
     environment, so a change to a screen can be shown before it is flashed;
   - how a new device model is registered in IoT Support and a fresh display is provisioned;
   - the memory budget: artwork in PSRAM, list lengths, fonts for Latin with accents.

   Waits on nothing.

5. **The firmware's skeleton on a display.** The repository onboarded for KubeCoder (with the
   ESP-IDF toolchain and `esp-libs` beside it) and the dev workflow; a firmware that boots,
   provisions through IoT Support, shows a placeholder screen in the dark theme, wakes on motion
   and touch, and takes an OTA update; its Jenkins job; the screen-rendering build of task 4. The
   operator flashes and provisions the first display. Waits on 4.

6. **Sonos and Plex behaviour settled; the API designed.** Tried on the real speakers, starting
   from the spike:
   - how the speakers' event notifications reach a pod (the address the backend advertises, the
     port, how the service is exposed), and what the operator has to open for it;
   - how a room of several devices (Zitkuil) and a group of rooms are presented and controlled;
   - what order the Plex service gives an artist's tracks, and how release-year order is
     guaranteed if that is not it;
   - how shuffle is done so that Play stays in order;
   - what a very large playlist does to the queue, and which playlists the library shows;
   - which stored identifier makes a favourite survive a Plex rescan;
   - the artwork forms: an ordinary image at a requested size, and the form the display draws
     directly (16-bit, dithered, at the display's sizes).

   Its result is the API's design in SonosController's record: generic endpoints for speakers and
   groups, playback and volume, the queue, the library, favourites, artwork and the event
   stream. Waits on nothing; tasks 7 to 10 build on it.

7. **Milestone 1: now playing.** The display shows what the chosen speaker plays, with artwork,
   and controls play/pause, previous, next and volume; it updates when something changes
   elsewhere. A simple speaker picker is part of it, since the display needs a speaker.
   - Backend: speakers, playback state, transport, volume, the event stream, artwork.
   - Operator: the firewall opening task 6 names. The speakers' address reservations are
     already made (see "What needs the operator's hands").
   - Firmware: the backend client, the now-playing screen and bar, the speaker picker.

   Waits on 3, 5 and 6.

8. **Milestone 2: the library and the queue.** Browsing artists, albums, tracks and playlists;
   Play, Shuffle and Add to queue on each; the queue screen with Clear.
   - Backend: the library from Plex, queueing through the Plex service, the queue. It needs the
     Plex token as a secret, from the operator.
   - Firmware: the library screens and the queue screen.

   Waits on 7.

9. **Milestone 3: favourites and the home screen.** Favourite toggles on artists, albums and
   playlists; the home screen showing them as tiles. Waits on 8.

10. **Milestone 4: groups.** Adding and removing speakers from this display's group, on the
    speakers screen, with the volume acting on the group. Waits on 7.

11. **Commissioning.** The display in the garage, checked against "What is agreed" item by item
    on the real speaker; a short guide in SonosDisplay for adding another display; the spike
    removed. Waits on 8 to 10.

Milestones are filed as slices in the repository they change and are planned and run by the dev
workflow.

## What needs the operator's hands

- **Before task 6:** restarting the speakers. The operator made two DHCP reservations on
  2026-11-15 so the backend can always find the system, and the speakers take them only after a
  restart: "I still need to restart the devices so they aren't yet reachable." The two addresses
  are in the project's brief in Pilot, not in this public repository. Until the restart the
  speakers answer on their old addresses, which the spike's `.env` held.
- **Task 5:** flashing and provisioning the first display.
- **Task 7:** the opening that lets the speakers reach the backend's event port, once task 6 has
  named it.
- **Task 8:** a Plex token for the backend, stored as a secret.
- **Each milestone:** trying it on the display.

## Rules

- **The design is the agent's.** The operator gave the reins: "I want to give you basically all
  the reins", and on details: "This kind of stuff I'm going to ask you to just do with your gut."
  And: "I'm not interested in getting the UI agreed in minute detail." Screen layout, wording,
  endpoint shapes and technical choices are decided by the task that meets them, not put to the
  operator.
- **What does come to the operator:** something for their hands; a change to what "What is
  agreed" says; anything that would cost money or touch another project's repository beyond
  task 2.
- **It is a simple device.** "If it has the features I want, it'll be good enough, quickly
  enough." A task that sees a way to do less and still meet "What is agreed" takes it.
- **Live tests use the Garage speaker only**, at low volume, and leave its queue, position and
  volume as they were found. Zitkuil is a living space.
- **The repositories are public.** No token, and no address or hostname of the operator's
  network, goes into a commit.
- **Research on smaller models.** The operator, for this work: "Delegate to lower models for
  research tasks."

## Context

- **Repositories:**
  - `pvginkel/SonosDisplay`: the firmware; holds this plan.
  - `pvginkel/SonosController`: the backend.
  - `pvginkel/ThermostatDisplay`: the old firmware for this hardware; the source of the
    hardware code. Its vendor documentation for the board is under `documentation/`.
  - `pvginkel/PaperClock` and `pvginkel/esp-libs`: the reference for firmware conventions.
  - `pvginkel/IoTSupport`: device management, provisioning and firmware distribution.
  - `pvginkel/ModernAppTemplate`: the template the backend is generated from, with its guide in
    the operator's dev guides.
- **Environment:** `pvginkel-sonosdisplay-1c23a1` holds SonosDisplay. It has no toolchain yet;
  task 5 adds what the firmware needs.
- **The spike** listed speakers, browsed Plex and queued music both ways. Task 6 started from it.
  Commissioning removed it from SonosDisplay; the repository's history keeps it.
