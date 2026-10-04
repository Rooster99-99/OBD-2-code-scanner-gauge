# OBD-II Dash Gauge — CYD firmware v0.2.0

Live engine gauges, a shift light, and a check-engine code reader on the 2.8" CYD (ESP32-2432S028), talking to a Bluetooth ELM327 OBD-II dongle.

## Build and flash (PlatformIO)

1. Install **VS Code** and the **PlatformIO** extension.
2. Unzip this folder and open it in VS Code (File → Open Folder → `obd-gauge`).
3. Plug in the CYD with a USB-C **data** cable.
4. Click the PlatformIO ✓ (Build), then → (Upload). The first build downloads the ESP32 tools and libraries, which takes a few minutes.
5. Open the Serial Monitor (plug icon) at 115200 to see logs.

### One-file firmware (.bin) for flashing without PlatformIO
Every build also writes `release/obd-gauge-cyd-full.bin`. It holds the bootloader, partition table and app together, so it flashes at address **0x0** in one step. This is the file to keep for flashing kits or sending to customers.

To flash it from a browser (Chrome or Edge):
1. Open https://espressif.github.io/esptool-js/ and click **Connect**, then choose the CYD's port.
2. Set Flash Address to `0x0`, choose the `-full.bin` file, and click **Program**.

The plain `.pio/build/cyd/firmware.bin` is the app only and goes at `0x10000`, which is only useful for updating a board that already has this firmware on it.

### If the screen is wrong
The USB-C CYD boards ship with different display chips. Switch builds in the PlatformIO toolbar (env picker) or change `default_envs` in `platformio.ini`:

| Symptom | Fix |
| --- | --- |
| White or blank screen | Try env `cyd_ili9341`, then `cyd_st7789` |
| Colors inverted (white background) | Settings → Display → Inverted |
| Red and blue swapped (ST7789 build) | Add `-DTFT_RGB_ORDER=TFT_BGR` to that env's build_flags |
| Taps land in the wrong spot | Watch `[touch] raw …` lines in the Serial Monitor and adjust `TOUCH_*` values in `src/config.h` |

## Using it

It boots in **demo mode** with simulated data, so you can try every screen at your desk. Tap the arrows in the top bar to change screens. The dot next to the left arrow shows the connection: blue is demo, yellow is connecting, green is live, and red means no dongle or the car isn't answering.

- **Gauge** — one big value with a bar, and three small values. Tap the big value or any small one to change what it shows.
- **Quad** — four values. Tap a tile to change it.
- **Shift light** — an RPM bar that flashes red at your shift point. The RGB LED on the back mirrors it on every screen.
- **Trouble codes** — READ lists stored codes with short descriptions. CLEAR asks you to confirm first, and it's blocked while the car is moving.
- **Settings** — units, shift point, coolant warning temperature, brightness, color inversion, demo on/off, and dongle pairing.

**Warnings:** the screen border and LED flash red when coolant passes your warning temperature, or when voltage leaves 12.0–15.2 V with the engine running.

**Moving lock:** above about 5 mph, settings changes and code clearing are disabled.

## Look and branding (v0.2.0)

The neon theme uses colors taken from the logo: magenta, cyan, electric blue, and orange on deep indigo. Big numbers glow, and the gauge bar fades from cyan through magenta to hot pink as it fills.

**Change the boot logo:**
1. Install Pillow once: `pip install pillow`
2. From the project folder, run: `python3 tools/make_splash.py path/to/your_logo.png`
3. Build and upload.

Any size or shape works. The image is scaled to fill the 320x240 screen and cropped. Add `--offset 0.5` to keep the middle of the image, or `--offset 1.0` to keep the bottom or right side. The default (`0.0`) keeps the top, which suits square logos with the subject up high. Add `--preview check.png` to see the result before flashing.

**Change the boot title:** edit `SPLASH_TITLE` in `src/config.h`.

The previews in `assets/` show roughly how the splash and gauge screen look. They're approximate: the real screen uses the display's own fonts.

## Pairing a dongle

1. Plug the dongle into the car's OBD port (under the dash, driver's side) and turn the ignition on.
2. On the gauge, go to Settings → Bluetooth dongle → SCAN (about 8 seconds).
3. Tap your dongle (often named `OBDII`, `V-LINK`, or `Android-Vlink`). Demo mode turns off and the gauge connects.
4. It remembers the dongle and reconnects automatically whenever the car starts.

The PIN defaults to `1234`. If your dongle uses `0000`, change `BT_PIN` in `src/config.h`.

**Dongle compatibility:** this build uses **Bluetooth Classic** (SPP) dongles, which covers most ELM327 units. BLE-only dongles (and Wi-Fi ones) won't show up in the scan. Avoid the cheapest "v1.5" clones, which are slow and flaky.

## Auto-brightness
Settings → Brightness → Auto shows the live light-sensor reading. Note the number in a bright room and in the dark, then put them in `LDR_BRIGHT_RAW` and `LDR_DARK_RAW` in `src/config.h`.

## Files
- `src/obd.cpp` — Bluetooth, dongle, and polling (runs on its own core so the screen never freezes)
- `src/ui.cpp` — screens, touch, LED, and backlight
- `src/pids.cpp` — the value list, unit conversion, and trouble-code descriptions
- `src/settings.cpp` — saved settings
- `src/config.h` — pins, tuning constants, boot title
- `src/splash_img.h` — the boot logo (generated by `tools/make_splash.py`)

## Libraries
TFT_eSPI (Bodmer), XPT2046_Touchscreen (Paul Stoffregen), and ELMduino (PowerBroker2). PlatformIO installs these automatically.
