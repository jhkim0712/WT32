# WT32 SmallTV

A small, self-hosted **desk clock / digital photo frame** firmware for the
[WT32-SC01 Plus](docs/WT32-SC01-PLUS_Datasheet-V1.9+EN.pdf) (ESP32-S3 + 3.5"
480x320 touch LCD), written in plain C on top of ESP-IDF and LVGL. It's a
homage to the **GeekMagic SmallTV Ultra**: swipe between a clock, a photo
slideshow from a microSD card, and a device-info screen, all configured from
a phone or laptop through a built-in web page - no app, no cloud account.

> 한국어 안내는 [README.ko.md](README.ko.md) 를 참고하세요.

![build](https://github.com/jhkim0712/WT32/actions/workflows/build.yml/badge.svg)
![status](https://img.shields.io/badge/status-early--access-orange)
![license](https://img.shields.io/badge/license-MIT-blue)

## Features

- **Clock screen** - large digital time + date, auto-updated from SNTP.
- **Photo album** - slideshow of `.bmp` / `.jpg` images from a microSD card,
  swipe or tap to advance, configurable interval and shuffle.
- **Device info screen** - IP address, Wi-Fi signal, SD card status, uptime,
  free heap, firmware version.
- **Swipe navigation** between screens, plus an optional auto-cycle timer.
- **100% web-based configuration** - a small embedded HTML5/CSS/JS page
  served by the device itself:
  - Wi-Fi setup (scan + connect), with an always-available SoftAP + captive
    portal fallback so you're never locked out.
  - Timezone (POSIX TZ string), NTP server, 12h/24h format, hourly chime.
  - Brightness, auto-cycle, mute.
  - Photo album interval/shuffle.
  - Hostname, restart, factory reset.
  - **Firmware updates**: upload a `.bin` file directly, or check a GitHub
    repo's releases and install with one click - see [Firmware updates](#firmware-updates).
- Reachable by IP or via mDNS (`http://<hostname>.local/`, default
  `wt32-smalltv.local`).
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
idf.py -p <PORT> flash monitor
```

The component manager will fetch the managed dependencies
(`lvgl`, `esp_lvgl_port`, `esp_lcd_st7796`, `esp_lcd_touch_ft5x06`,
`esp_jpeg`) on the first build.

A `.devcontainer/` is included if you prefer building inside the official
`espressif/idf` Docker image via VS Code Dev Containers.

## First boot / configuration

1. Flash the firmware and power the board.
2. The Info screen (swipe to it) shows a SoftAP name like
   `WT32-SmallTV-AB12` and the password `smalltv1234`. Connect a phone or
   laptop to it (most phones will pop up a captive-portal prompt
   automatically; otherwise open `http://192.168.4.1/`).
3. Open the **Wi-Fi** tab, scan, pick your network, enter its password and
   press *Connect*. On success the device joins your network and the
   settings persist across reboots.
4. The SoftAP (and its captive portal) stay available at all times as a
   fallback, so the web UI is always reachable even if the home network is
   down - either via the AP or via `http://<device-ip>/` /
   `http://wt32-smalltv.local/` once joined.
5. Set your timezone, NTP server and any other preferences from the other
   tabs.

### Photo album

Copy `.bmp` (24-bit uncompressed) and/or `.jpg`/`.jpeg` files into a
`photos/` folder at the root of the microSD card (i.e. `/photos/*.bmp`).
Images are automatically scaled to fill the 480x320 panel; for best quality,
pre-crop/resize them to 480x320 before copying. BMP is the guaranteed-to-work
format with zero external dependencies; JPEG goes through the
`espressif/esp_jpeg` managed component (see notes below).

## Firmware updates

The **Firmware** tab in the web UI supports two ways to update, both going
through the same on-device validation:

- **Manual upload** - pick a `.bin` file (built with `idf.py build`,
  `build/wt32_smalltv.bin`) and click *Upload & install*. A progress bar
  tracks the upload.
- **Check GitHub releases** - fill in a repo as `owner/name` under
  "Check GitHub releases", save it, then *Check for updates*. It looks at
  `https://api.github.com/repos/<owner>/<name>/releases/latest` for a release
  asset named `firmware.bin` (falling back to the first `.bin` asset) and
  offers to install it if its version tag is newer than the running one. If
  you maintain a fork, publish your built `build/wt32_smalltv.bin` as
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
  app_web/             REST API + embedded HTML5/CSS/JS config page
  app_ota/             manual/GitHub firmware updates + identity validation
  ui/                  LVGL screens (clock / album / info) + navigation
```

## Known limitations / roadmap

- **No RTC battery** - the board has no battery-backed RTC chip, so the
  clock is only accurate after the first successful SNTP sync each boot.
- **Analog clock face** - the `clock_face` setting exists in the config
  struct for forward-compatibility, but only the digital face is implemented
  today.
- **JPEG decoding** is best-effort: `esp_jpeg`'s API has shifted across
  releases, so if a firmware/component upgrade breaks JPEG decoding, BMP
  photos will still work unaffected (see
  `components/app_photo/app_photo_jpeg.c`).
- **OTA asset naming** - the GitHub update check only looks for an asset
  literally named `firmware.bin` first, else the first `.bin` it finds; if
  your release has multiple boards' binaries, name them so `firmware.bin`
  is the right one (or rename the asset per-repo).
- Ideas welcome: weather screen, MP3/audio playback from SD, alarms, more
  clock faces, GIF playback.

## Security note

The fallback SoftAP uses a fixed default password (`smalltv1234`) so the
device is always configurable out of the box. This is a "same-room trust"
model, like most similar SmallTV-style gadgets and cheap routers - treat it
accordingly (it's not intended to resist a determined attacker within Wi-Fi
range).

## License

MIT - see [LICENSE](LICENSE). Vendor datasheets under `docs/` remain the
property of Wireless-Tag Technology Co., Limited and are included for
reference only.
