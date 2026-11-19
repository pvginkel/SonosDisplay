# Adding another display

How a second Waveshare ESP32-S3-Touch-LCD-4.3 goes from the bench to controlling a speaker. Every
display runs the same firmware. Displays differ only in their device configuration in IoT Support
and in the speaker each is set to. SonosController serves any number of displays and keeps nothing
per display, so nothing is done there.

## 1. The board

- **A Waveshare ESP32-S3-Touch-LCD-4.3**, such as one of the old thermostat displays.
- **The motion sensor** on the board's "Sensor" header (GPIO 6, active high). Without one, only a
  touch wakes the screen.
- **The brown-out capacitor.** Switching the backlight browns this board out. The operator's
  boards have a capacitor added for it, and the firmware switches the backlight at most once a
  second. Nothing in the repositories records the capacitor's value or place: fit it as on the
  earlier boards.
- **A TF card** in the board's slot, kept for this display alone. The display keeps the covers it
  downloads on it, and draws them from it again without the network, also after a restart. A card
  on which the display finds no file system it reads, such as an exFAT card, is formatted, which
  wipes it: put in no card that holds something to keep. Without a card the display works all
  the same, and downloads each cover again after a restart.

## 2. The device in IoT Support

The model `sonos_display` and its firmware are already in IoT Support. Every push to `main`
uploads the firmware through the Jenkins job `Firmware/SonosDisplay`. A new display only needs a
device.

1. Open an existing display, such as `sonos_display_garage`, and press **Duplicate** on its
   Configuration tab. This makes a new `sonos_display` device with the same configuration. **New
   Device** with the model `sonos_display` also works, but then the JSON is typed in from scratch.
2. In the configuration JSON, change:
   - `deviceName`: the name IoT Support shows;
   - `deviceEntityId`: unique per display, such as `sonos_display_<place>`. The display's log is
     filed under it.

   Keep `enableOTA: true`, or the display takes no updates. Keep `backendUrl`, SonosController's
   base URL: the scheme and host, with no path. `screenOffSeconds` is optional: how long the screen
   stays lit after the last touch or motion, from 10 to 3600, 60 when left out.
3. **Save.** IoT Support creates the device and its Keycloak client.

## 3. Flashing and provisioning

These are done on the new device's Configuration tab, in Chrome or Edge on a desktop (Web Serial).
The display is connected by its USB-C port marked UART. The board's automatic download circuit
means no button is held.

1. **Flash Device**, then Connect, and pick the port. It writes the model's current firmware
   (bootloader, partition table, OTA data and app) and resets the board. Until the next step the
   board restarts over and over, because esp-mdm stops at start without credentials. That is
   expected.
2. **Provision Device**, then Connect again. It writes the board's `nvs` partition: the device's
   credentials, IoT Support's addresses, and the WiFi network IoT Support gives every device.
3. When it reports "Provisioning complete! Please reset the device manually", unplug the display
   and plug it in again. Only a power-on lights the screen at boot.

**Check:** the screen shows "Starting", then "Checking for an update", and the device's Logs tab
in IoT Support fills with the boot. "No backend address in this display's IoT Support
configuration" means `backendUrl` is missing or malformed. Correct it and Save: saving a
configuration restarts the display.

## 4. On the display

- **The speaker.** A new display opens on **Speakers**, "Pick the speaker this display controls".
  Touch the speaker. The choice is stored on the display.
- **The group.** The same list is reached again through the now-playing bar: touch the bar, then
  the speaker button, between back and the queue, on the now-playing screen. With a speaker
  chosen, every other speaker has a circle at its right end. Touch the circle to add that speaker
  to this display's group. Touch it again, now filled with a tick, to take it out.
- **Motion wake** is on at first. The sensor button in Home's heading, left of Library, switches it
  off, so that only a touch wakes the screen. This is stored on the display too.

Provisioning again rewrites the whole `nvs` partition. A display provisioned a second time asks
for its speaker again, and motion wakes it again.
