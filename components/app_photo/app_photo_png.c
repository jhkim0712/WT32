/**
 * @file app_photo_png.c
 * @brief PNG decode via the managed `espressif/libpng` component, using
 *        libpng's "simplified API" (png_image_*) added in libpng 1.6 - it
 *        handles bit depth, palette, grayscale and interlacing normalization
 *        internally, so we only ever deal with plain 8-bit RGB output.
 *
 * Like app_photo_jpeg.c, this is intentionally isolated: if a future libpng
 * component upgrade shifts this API, this is the only file that needs
 * patching - BMP (app_photo_bmp.c) and JPEG are unaffected either way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <png.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "app_photo_internal.h"

static const char *TAG = "app_photo_png";

esp_err_t app_photo_png_decode_native(const char *path, uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h)
{
    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (!png_image_begin_read_from_file(&image, path)) {
        ESP_LOGW(TAG, "%s: %s", path, image.message);
        return ESP_FAIL;
    }

    /* Ask libpng to normalize whatever the source format is (palette,
     * grayscale, 16-bit, alpha, interlaced, ...) down to plain 8-bit RGB -
     * no alpha channel, since photos are shown opaque/full-screen with
     * nothing behind them to composite against anyway. RGB (3 bytes/px)
     * rather than RGBA (4) also shrinks the one buffer this function needs -
     * see below. */
    image.format = PNG_FORMAT_RGB;

    size_t rgb_size = PNG_IMAGE_SIZE(image);
    if (rgb_size == 0 || rgb_size > PHOTO_MAX_DECODE_BYTES * 3 / 2) {
        /* *3/2 because this is the RGB (3 bytes/px) staging buffer, not the
         * final RGB565 (2 bytes/px) one that PHOTO_MAX_DECODE_BYTES caps. */
        ESP_LOGW(TAG, "%s: %ux%u is too large to decode", path, image.width, image.height);
        png_image_free(&image);
        return ESP_ERR_NO_MEM;
    }

    /* One buffer, sized for the (larger) RGB staging data and converted to
     * the (smaller) RGB565 result in place below, instead of decoding into
     * one buffer and converting into a second one. Halves this function's
     * peak PSRAM use, which matters here: PSRAM is tight enough on this
     * board (LVGL's own full-frame double buffer alone already holds
     * ~600KB of it) that needing two buffers at once - even for an image
     * already at the panel's own 480x320 resolution - could fail with
     * ESP_ERR_NO_MEM despite the final result comfortably fitting. */
    png_bytep buf = heap_caps_malloc(rgb_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        ESP_LOGW(TAG, "%s: out of PSRAM decoding %" PRIu32 "x%" PRIu32 " (%u bytes needed, %u free)", path,
                 (uint32_t)image.width, (uint32_t)image.height, (unsigned)rgb_size,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        png_image_free(&image);
        return ESP_ERR_NO_MEM;
    }

    if (!png_image_finish_read(&image, NULL /* background */, buf, 0 /* row_stride */, NULL /* colormap */)) {
        ESP_LOGW(TAG, "%s: %s", path, image.message);
        free(buf);
        png_image_free(&image);
        return ESP_FAIL;
    }

    uint32_t width = image.width;
    uint32_t height = image.height;
    png_image_free(&image);

    /* RGB888 -> RGB565, in place inside the same buffer (no separate
     * output allocation): pixel i's 2-byte output at offset 2*i is always
     * at or behind its own 3-byte input at offset 3*i, and every earlier
     * iteration only ever wrote to offsets below the current pixel's
     * input bytes, so this can never read data an earlier write already
     * overwrote. */
    uint16_t *pixels = (uint16_t *)buf;
    for (size_t pixel_i = 0; pixel_i < (size_t)width * height; pixel_i++) {
        uint8_t r = buf[pixel_i * 3 + 0];
        uint8_t g = buf[pixel_i * 3 + 1];
        uint8_t b = buf[pixel_i * 3 + 2];
        pixels[pixel_i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    /* The RGB565 result only needs 2/3 of the RGB staging allocation - this
     * photo may stay on screen for a while (see ui_album.c), so it's worth
     * reclaiming that last third for whatever else wants PSRAM meanwhile.
     * If the shrink fails for any reason the original, larger allocation is
     * still entirely valid data, so just keep using it. */
    void *shrunk = heap_caps_realloc(buf, (size_t)width * height * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (shrunk) {
        pixels = shrunk;
    }

    *out_buf = pixels;
    *out_w = (uint16_t)width;
    *out_h = (uint16_t)height;
    return ESP_OK;
}
