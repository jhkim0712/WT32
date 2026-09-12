/**
 * @file app_photo.h
 * @brief SD-card photo album: directory scan + decode-to-RGB565 for the
 *        LVGL album screen.
 *
 * Supported formats: 24-bit uncompressed .bmp (always available, zero
 * third-party dependencies), baseline .jpg/.jpeg (via the managed
 * `espressif/esp_jpeg` component), and .png of any bit depth/color type
 * (via the managed `espressif/libpng` component - see README "Photo album"
 * section for caveats on the last two). Images are decoded, then
 * nearest-neighbor scaled to exactly fill the requested canvas (typically
 * the panel's 480x320 resolution). PNG transparency is ignored (flattened
 * to opaque) since photos are shown full-screen with nothing behind them.
 *
 * .gif is also recognized by app_photo_scan()/is included in the count and
 * playback order, but it does NOT go through app_photo_decode_to_canvas() -
 * animated GIFs play through LVGL's own lv_gif widget instead (see
 * app_photo_is_gif() and ui_album.c), a completely different, path-based
 * pipeline that shows them at native size rather than scaled to fill the
 * screen like the other formats.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_PHOTO_PATH_MAX 256

/** (Re-)scan a directory on the SD card for supported image files. */
esp_err_t app_photo_scan(const char *dir_path);

/** @return number of images found by the last app_photo_scan(). */
size_t app_photo_count(void);

/** @return the full path of image @p index, or NULL if out of range. */
const char *app_photo_get_path(size_t index);

/** Shuffle the internal playback order (Fisher-Yates). */
void app_photo_shuffle(void);

/** Restore the natural (scan) order. */
void app_photo_unshuffle(void);

/**
 * @brief Decode image @p index and scale it to canvas_w x canvas_h.
 *
 * @param out_buf receives a heap buffer of canvas_w*canvas_h RGB565 pixels;
 *                the caller owns it and must free() it.
 */
esp_err_t app_photo_decode_to_canvas(size_t index, uint16_t canvas_w, uint16_t canvas_h, uint16_t **out_buf);

/** @return true if @p path has a .gif extension - see the file comment. */
bool app_photo_is_gif(const char *path);

#ifdef __cplusplus
}
#endif
