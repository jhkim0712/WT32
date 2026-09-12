/**
 * @file app_photo_png.c
 * @brief PNG decode via the managed `espressif/libpng` component, using
 *        libpng's "simplified API" (png_image_*) added in libpng 1.6 - it
 *        handles bit depth, palette, grayscale and interlacing normalization
 *        internally, so we only ever deal with plain 8-bit RGBA output.
 *
 * Like app_photo_jpeg.c, this is intentionally isolated: if a future libpng
 * component upgrade shifts this API, this is the only file that needs
 * patching - BMP (app_photo_bmp.c) and JPEG are unaffected either way.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
     * grayscale, 16-bit, interlaced, ...) down to plain 8-bit RGBA. */
    image.format = PNG_FORMAT_RGBA;

    size_t rgba_size = PNG_IMAGE_SIZE(image);
    if (rgba_size == 0 || rgba_size > PHOTO_MAX_DECODE_BYTES * 2) {
        /* *2 because this is the RGBA (4 bytes/px) staging buffer, not the
         * final RGB565 (2 bytes/px) one that PHOTO_MAX_DECODE_BYTES caps. */
        ESP_LOGW(TAG, "%s: %ux%u is too large to decode", path, image.width, image.height);
        png_image_free(&image);
        return ESP_ERR_NO_MEM;
    }

    /* Decode buffers scale with image size and can reach into the megabytes
     * (rgba_size especially, at 4 bytes/px) - PSRAM has room for that, the
     * ~400KB of internal DRAM left over after LVGL/Wi-Fi/lwIP does not, so
     * ask for it explicitly rather than let the general allocator pick. */
    png_bytep rgba = heap_caps_malloc(rgba_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rgba) {
        png_image_free(&image);
        return ESP_ERR_NO_MEM;
    }

    if (!png_image_finish_read(&image, NULL /* background */, rgba, 0 /* row_stride */, NULL /* colormap */)) {
        ESP_LOGW(TAG, "%s: %s", path, image.message);
        free(rgba);
        png_image_free(&image);
        return ESP_FAIL;
    }

    uint32_t width = image.width;
    uint32_t height = image.height;
    png_image_free(&image);

    size_t rgb565_size = (size_t)width * height * sizeof(uint16_t);
    if (rgb565_size > PHOTO_MAX_DECODE_BYTES) {
        ESP_LOGW(TAG, "%s: %ux%u is too large to decode (limit %d bytes)", path, width, height,
                  PHOTO_MAX_DECODE_BYTES);
        free(rgba);
        return ESP_ERR_NO_MEM;
    }

    uint16_t *pixels = heap_caps_malloc(rgb565_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        free(rgba);
        return ESP_ERR_NO_MEM;
    }

    /* RGBA8888 -> RGB565, dropping alpha (photos are shown opaque/full-screen,
     * so there's no backdrop to composite semi-transparent pixels against). */
    for (size_t pixel_i = 0; pixel_i < (size_t)width * height; pixel_i++) {
        uint8_t r = rgba[pixel_i * 4 + 0];
        uint8_t g = rgba[pixel_i * 4 + 1];
        uint8_t b = rgba[pixel_i * 4 + 2];
        pixels[pixel_i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }
    free(rgba);

    *out_buf = pixels;
    *out_w = (uint16_t)width;
    *out_h = (uint16_t)height;
    return ESP_OK;
}
