/**
 * @file app_photo.h
 * @brief SD-card photo album: directory scan + decode-to-RGB565 for the
 *        LVGL album screen.
 *
 * Supported formats: 24-bit uncompressed .bmp (always available, zero
 * third-party dependencies) and baseline .jpg/.jpeg (via the managed
 * `espressif/esp_jpeg` component - see README "Photo album" section for
 * caveats). Images are decoded, then nearest-neighbor scaled to exactly
 * fill the requested canvas (typically the panel's 480x320 resolution).
 */
#pragma once

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

#ifdef __cplusplus
}
#endif
