# WT32

A small, self-hosted **desk clock / digital photo frame** firmware for the
[WT32-SC01 Plus](docs/WT32-SC01-PLUS_Datasheet-V1.9+EN.pdf) (ESP32-S3 + 3.5"
480x320 touch LCD), written in plain C on top of ESP-IDF and LVGL. Swipe
between a clock, a photo slideshow from a microSD card, a weather screen, and
a device-info screen, all configured from a phone or laptop through a
built-in web page - no app, no cloud account. Before Wi-Fi is set up, a QR
code screen stands in for the clock/album/weather screens so first-time
setup is just a camera scan away.

> 한국어 안내는 [README.ko.md](README.ko.md) 를 참고하세요.

![build](https://github.com/jhkim0712/WT32/actions/workflows/build.yml/badge.svg)
![status](https://img.shields.io/badge/status-early--access-orange)
![license](https://img.shields.io/badge/license-MIT-blue)

## Features

- **Clock screen** - large digital time + date, auto-updated from SNTP.
- **Photo album** - slideshow of `.bmp` / `.jpg` / `.png` images from a
  microSD card, swipe or tap to advance, configurable interval and shuffle.
  `.gif` files play as animated GIFs (native size, centered) instead of
  being stretched to fill the panel like the static formats.
- **Weather screen** - current conditions from OpenWeatherMap (temperature,
  description, humidity, wind), with a day/night condition icon from the
  bundled [Weather Icons](https://github.com/erikflowers/weather-icons)
  font. Shows the last known reading (marked stale) if a fetch fails, rather
  than blanking on a transient network hiccup. Optional - stays hidden from
  the swipe rotation until an API key and city ID are set.
- **Wi-Fi setup screen** - shown automatically in place of the clock/album/
  weather screens until Wi-Fi is configured: a QR code for the fallback
  SoftAP (scan it with a phone's camera to join instantly, no typing) plus
  the SSID/password as plain text. Drops out of rotation the moment the
  device joins a network.
- **Device info screen** - IP address, Wi-Fi signal, SD card status, uptime,
  free heap, firmware version.
- **Swipe navigation** between screens, plus an optional auto-cycle timer.
- **100% web-based configuration** - a small embedded HTML5/CSS/JS page
  served by the device itself:
  - Wi-Fi setup (scan + connect), with an always-available SoftAP + captive
    portal fallback so you're never locked out.
  - Timezone (POSIX TZ string), NTP server, 12h/24h format, hourly chime.
  - Brightness, auto-cycle, mute.
  - Dark/Light theme, or Auto to switch on its own at times you set (e.g.
    light from 07:00, dark from 20:00).
  - Photo album interval/shuffle.
  - Weather: OpenWeatherMap API key + city ID, enable/disable the screen.
  - Hostname, restart, factory reset, fallback AP password.
  - Serial console log level (None through Verbose, default Info) - takes
    effect immediately, no reflash needed.
  - **File manager**: browse, upload, download/view, rename and delete
    anything on the microSD card, right from the browser (no separate app
    needed to load photos onto it) - plus a one-click **format** to fully
    erase and reformat the card if it needs wiping. Upload picks multiple
    files at once or accepts a drag-and-drop of files onto the panel.
  - **Firmware updates**: upload a `.bin` file directly, or check a GitHub
    repo's releases and install with one click - see [Firmware updates](#firmware-updates).
- Reachable by IP or via mDNS (`http://<hostname>.local/`, default
  `wt32.local`).
- Boot / UI chime through the onboard I2S amplifier.

## Hardware

[WT32-SC01 Plus](https://www.wireless-tag.com) (`ZX3D50CE08S-USRC-4832`):
ESP32-S3 (WT32-S3-WROVER-N16R2, 16MB flash + PSRAM), 480x320 ST7796 LCD over
an 8080/i80 parallel bus, FT6336U capacitive touch over I2C, microSD over
SPI, a 2.5W/4R I2S class-D amplifier. See the full datasheet under
[`docs/`](docs/).

### Pinout (from the vendor datasheet)

| Function          | GPIO(s)                                   |
|-------------------|--------------------------------------------|
| LCD data D0-D7    | 9, 46, 3, 8, 18, 17, 16, 15                 |
| LCD DC / WR / RST | 0 / 47 / 4 (RST shared with touch)          |
| LCD backlight     | 45 (PWM, active high)                       |
| Touch SDA/SCL/INT | 6 / 5 / 7 (FT6336U, I2C)                    |
| SD CS/MOSI/CLK/MISO | 41 / 40 / 39 / 38 (SPI)                   |
| I2S BCLK/WS/DOUT  | 36 / 35 / 37                                |
| Extended IO       | 10, 11, 12, 13, 14, 21                      |
| RS485 RX/RTS/TX   | 1 / 2 / 42 (not used by this firmware)       |

See [`components/bsp/include/bsp/bsp_pins.h`](components/bsp/include/bsp/bsp_pins.h)
for the single source of truth used by the code.

## Building

Requires **ESP-IDF v6.0 or newer** (developed and tested against v6.1). C
only - no C++ anywhere in the firmware.

ESP-IDF v6.0 restructured a lot of the peripheral driver components (each
peripheral now lives in its own `esp_driver_xxx` component instead of the
old monolithic `driver`) and removed the built-in `json` component in favor
of the managed `espressif/cjson`; both are already accounted for in this
project's `CMakeLists.txt`/`idf_component.yml` files, so a plain build
should just work. If you need to target an older ESP-IDF (v5.2-v5.5), the
main things to revert are `esp_lcd_i80_bus_config_t::dma_burst_size` (was
`psram_trans_align`/`sram_trans_align`) in
[`bsp_display.c`](components/bsp/bsp_display.c) and the `REQUIRES json` /
cJSON managed-component split in `app_web`/`app_ota`.

```sh
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> -b 921600 flash monitor
```

(`-b 921600` just raises the flashing baud rate - drop it if your USB-serial
adapter doesn't like it. The VS Code ESP-IDF extension has its own
equivalent setting, `idf.flashBaudRate`, in `.vscode/settings.json`.)

The component manager will fetch the managed dependencies
(`lvgl`, `esp_lvgl_port`, `esp_lcd_st7796`, `esp_lcd_touch_ft5x06`,
`esp_jpeg`, `libpng`) on the first build.

## First boot / configuration

1. Flash the firmware and power the board.
2. Until Wi-Fi is configured, the display shows only two screens - swipe
   between them: a **setup screen** with a QR code for the fallback SoftAP
   (scan it with a phone's camera to join instantly, no typing needed) and
   the device-info screen. The SoftAP name looks like `WT32-A1B2C3` (the
   last 3 octets of its MAC address) and has **no password** (open network)
   by default. Joining pops up a captive-portal prompt on most phones
   automatically; otherwise open `http://192.168.4.1/` manually.
3. Open the **Wi-Fi** tab, scan, pick your network, enter its password and
   press *Connect*. On success the device joins your network, the settings
   persist across reboots, and the clock/album/weather screens replace the
   setup screen in the swipe rotation.
4. The SoftAP itself stays available at all times as a fallback, so the web
   UI is always reachable even if the home network is down - either via the
   AP (browse to `http://192.168.4.1/` manually) or via `http://<device-ip>/`
   / `http://wt32.local/` once joined. The automatic captive-portal popup
   only fires while the device *isn't* connected to your Wi-Fi, though -
   once it joins, reconnecting to the fallback AP later (e.g. from a second
   device, to reach the config page without knowing the home-network IP)
   won't nag you with a sign-in prompt every time. You can give the fallback AP a password
   from the **System** tab if an open network next to it makes you uneasy.
5. Set your timezone, NTP server and any other preferences from the other
   tabs.

### Photo album

Copy `.bmp` (24-bit uncompressed), `.jpg`/`.jpeg`, `.png` and/or `.gif` files
into a `photos/` folder at the root of the microSD card (i.e.
`/photos/*.bmp`). The static formats (bmp/jpg/png) are automatically scaled
to fill the 480x320 panel; for best quality, pre-crop/resize them to 480x320
before copying. BMP is the guaranteed-to-work format with zero external
dependencies; JPEG and PNG go through the `espressif/esp_jpeg` and
`espressif/libpng` managed components respectively (see notes below). PNG
transparency is ignored (flattened to opaque) since photos fill the whole
screen with nothing behind them to show through to.

`.gif` files play as animated GIFs instead, through LVGL's own decoder -
a different pipeline from the static formats above, so they're **not**
scaled to fill the panel; they show at their native resolution, centered.
Keep them reasonably small (both in pixel size and file size) for smooth
playback and to avoid using up too much of the panel's frame buffer memory.

## Firmware updates

The **Firmware** tab in the web UI supports two ways to update, both going
through the same on-device validation:

- **Manual upload** - pick a `.bin` file (built with `idf.py build`,
  `build/wt32_firmware.bin`) and click *Upload & install*. A progress bar
  tracks the upload.
- **Check GitHub releases** - fill in a repo as `owner/name` under
  "Check GitHub releases", save it, then *Check for updates*. It looks at
  `https://api.github.com/repos/<owner>/<name>/releases/latest` for a release
  asset named `firmware.bin` (falling back to the first `.bin` asset) and
  offers to install it if its version tag is newer than the running one. If
  you maintain a fork, publish your built `build/wt32_firmware.bin` as
  `firmware.bin` on each GitHub Release and tag it `vX.Y.Z`.

**Validation, so a stray or wrong `.bin` can't brick the device:**
1. `esp_ota_write()`/`esp_ota_end()` (and `esp_https_ota_*` for the GitHub
   path) already reject anything that isn't a well-formed, checksummed
   ESP32 image - a random file or a truncated download never gets this far.
2. On top of that, every image's embedded `esp_app_desc_t.project_name` is
   checked against this project's identity (see `FIRMWARE_PROJECT_NAME` in
   [`components/app_ota/app_ota.c`](components/app_ota/app_ota.c)) - this
   catches a perfectly valid ESP32 firmware image that just isn't *this*
   project's.
3. As a last-resort safety net, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is
   on: a newly installed image is only "confirmed" after `app_main()`
   finishes bringing the UI up. If it crashes or reboots before that, the
   bootloader automatically reverts to the previous working image on the
   next boot.

### Cutting a release

[`.github/workflows/release.yml`](.github/workflows/release.yml) publishes a
GitHub Release automatically - with auto-generated release notes (from the
merged PRs/commits since the last tag) and a ready-to-use `firmware.bin`
attached - whenever a tag matching `vX.Y.Z` is pushed:

```sh
# 1. Bump the version and commit it
echo "1.1.0" > version.txt
git commit -am "Bump version to 1.1.0"
git push

# 2. Tag it (must match version.txt, or the workflow fails on purpose)
git tag v1.1.0
git push origin v1.1.0
```

The release also gets `bootloader.bin`, `partition-table.bin`,
`ota_data_initial.bin` and `flasher_args.json` attached, for anyone flashing
a blank board over serial instead of using the web UI.
[`.github/workflows/build.yml`](.github/workflows/build.yml) is a separate,
simpler workflow that just builds (no release) on every push to `main` and
every PR, to catch a broken build before it gets that far.

The partition table (`partitions.csv`) uses the standard `otadata` +
`ota_0`/`ota_1` dual-app-slot layout (no `factory` partition), so a plain
`idf.py -p <PORT> flash` still works for the very first flash over serial.

## Troubleshooting the display

All of these are tuned in
[`components/bsp/bsp_display.c`](components/bsp/bsp_display.c) - each just
needs a rebuild + reflash to try, no wiring changes:

- **Static/noise, no recognizable image** - almost always i80 bus signal
  integrity, not a logic bug. Try, in order: (1) lower
  `BSP_LCD_PIXEL_CLK_HZ` further (e.g. 4 MHz), (2) flip
  `BSP_LCD_PCLK_ACTIVE_NEG` to `1` (some ST7796 panel batches latch data on
  the opposite WR edge).
- **Colors look inverted/wrong but shapes are fine** - flip the `true` in
  `esp_lcd_panel_invert_color(panel_handle, true)`.
- **Image is mirrored, rotated, or shifted** - adjust the
  `esp_lcd_panel_swap_xy()` / `esp_lcd_panel_mirror()` calls right below it.
- **Backlight comes on but nothing is drawn at all** - check the serial
  monitor log for an `ESP_ERROR_CHECK` abort during `bsp_display_start()`
  (touch controller or panel init failing) rather than a display-tuning
  issue.

## Project layout

```
main/                 app_main(): bring-up order and wiring only
components/
  bsp/                 board support: display + touch (via esp_lvgl_port),
                       SD card, backlight PWM, I2S audio
  app_config/          NVS-backed settings struct (single source of truth)
  app_wifi/            SoftAP + STA Wi-Fi manager, captive-portal DNS, mDNS
  app_time/            timezone + SNTP
  app_photo/           SD card scan + BMP/JPEG decode + resize
  app_weather/         OpenWeatherMap current-conditions polling
  app_web/             REST API + embedded HTML5/CSS/JS config page
  app_ota/             manual/GitHub firmware updates + identity validation
  ui/                  LVGL screens (Wi-Fi setup QR / clock / album /
                       weather / info) + navigation; weather condition icons
                       are a bitmap font generated (lv_font_conv) from
                       Weather Icons
```

## Known limitations / roadmap

- **No RTC battery** - the board has no battery-backed RTC chip, so the
  clock is only accurate after the first successful SNTP sync each boot.
- **Analog clock face** - the `clock_face` setting exists in the config
  struct for forward-compatibility, but only the digital face is implemented
  today.
- **JPEG/PNG decoding** is best-effort: `esp_jpeg` and `libpng`'s APIs can
  shift across releases, so if a component upgrade breaks one of them, BMP
  photos keep working unaffected either way (see
  `components/app_photo/app_photo_jpeg.c` / `app_photo_png.c`). PNG decode
  also pulls in `espressif/zlib` transitively and is the heaviest of the
  three formats (RAM and flash-wise) - prefer BMP/JPEG on very large images.
- **OTA asset naming** - the GitHub update check only looks for an asset
  literally named `firmware.bin` first, else the first `.bin` it finds; if
  your release has multiple boards' binaries, name them so `firmware.bin`
  is the right one (or rename the asset per-repo).
- Ideas welcome: MP3/audio playback from SD, alarms, more clock faces, GIF
  playback.

## Security note

The fallback SoftAP is **open (no password) by default** so the device is
always configurable out of the box - set a password for it from the
**System** tab once you've configured the device, if you'd rather it not
sit open indefinitely. Either way this is a "same-room trust" model, like
most similar gadgets and cheap routers - it's not intended to resist a
determined attacker within Wi-Fi range.

The web UI's file manager (see [Photo album](#photo-album)) has no
authentication either - anyone who can reach the device's IP (its own AP,
or your home network once joined) can read/write/delete anything under
`/sdcard`. Keep that in mind before joining the device to a network you
don't trust everyone on.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for coding style and project
conventions before sending a PR.

## License

MIT - see [LICENSE](LICENSE). Vendor datasheets under `docs/` remain the
property of Wireless-Tag Technology Co., Limited and are included for
reference only. The weather condition icons are a small, converted subset of
Erik Flowers' [Weather Icons](https://github.com/erikflowers/weather-icons)
font (SIL OFL 1.1) - see
[`components/ui/ui_weather_icons.c`](components/ui/ui_weather_icons.c) for
exactly which glyphs and how they were converted.
