/**
 * @file app_photo_bmp.c
 * @brief Minimal 24-bit uncompressed BMP decoder (no third-party deps).
 *
 * This is the guaranteed-to-work path for the photo album: any image editor
 * or `ffmpeg`/`ImageMagick` can export a plain 24-bit BMP.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_heap_caps.h"

#include "app_photo_internal.h"

static const char *TAG = "app_photo_bmp";

typedef struct __attribute__((packed)) {
    uint16_t bf_type;
    uint32_t bf_size;
    uint16_t bf_reserved1;
    uint16_t bf_reserved2;
    uint32_t bf_off_bits;
} bmp_file_header_t;

typedef struct __attribute__((packed)) {
    uint32_t bi_size;
    int32_t  bi_width;
    int32_t  bi_height;
    uint16_t bi_planes;
    uint16_t bi_bit_count;
    uint32_t bi_compression;
    uint32_t bi_size_image;
    int32_t  bi_x_ppm;
    int32_t  bi_y_ppm;
    uint32_t bi_clr_used;
    uint32_t bi_clr_important;
} bmp_info_header_t;

esp_err_t app_photo_bmp_decode_native(const char *path, uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    bmp_file_header_t fh;
    bmp_info_header_t ih;
    if (fread(&fh, sizeof(fh), 1, f) != 1 || fh.bf_type != 0x4D42 /* "BM" */) {
        ESP_LOGW(TAG, "%s: not a BMP file", path);
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }
    if (fread(&ih, sizeof(ih), 1, f) != 1) {
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }
    if (ih.bi_bit_count != 24 || ih.bi_compression != 0) {
        ESP_LOGW(TAG, "%s: only uncompressed 24-bit BMP is supported (got %u bpp, compression %u)",
                  path, ih.bi_bit_count, (unsigned)ih.bi_compression);
        fclose(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    int width = ih.bi_width;
    bool bottom_up = ih.bi_height > 0;
    int height = ih.bi_height > 0 ? ih.bi_height : -ih.bi_height;
    if (width <= 0 || height <= 0) {
        fclose(f);
        return ESP_ERR_INVALID_ARG;
    }
    if ((size_t)width * height * sizeof(uint16_t) > PHOTO_MAX_DECODE_BYTES) {
        ESP_LOGW(TAG, "%s: %dx%d is too large to decode (limit %d bytes)", path, width, height,
                  PHOTO_MAX_DECODE_BYTES);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    /* The decoded buffer scales with image size and can reach into the
     * megabytes - PSRAM has room for that, the ~400KB of internal DRAM left
     * over after LVGL/Wi-Fi/lwIP does not, so ask for it explicitly rather
     * than let the general allocator pick. */
    uint16_t *buf = heap_caps_malloc((size_t)width * height * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    int row_bytes = width * 3;
    int row_padded = (row_bytes + 3) & ~3;
    uint8_t *row = malloc(row_padded); /* one scanline, a few KB at most - fine in internal RAM */
    if (!row) {
        free(buf);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    fseek(f, fh.bf_off_bits, SEEK_SET);
    esp_err_t ret = ESP_OK;
    for (int y = 0; y < height; y++) {
        if (fread(row, 1, row_padded, f) != (size_t)row_padded) {
            ret = ESP_FAIL;
            break;
        }
        int dst_row = bottom_up ? (height - 1 - y) : y;
        uint16_t *dst = buf + (size_t)dst_row * width;
        for (int x = 0; x < width; x++) {
            uint8_t b = row[x * 3 + 0];
            uint8_t g = row[x * 3 + 1];
            uint8_t r = row[x * 3 + 2];
            dst[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }

    free(row);
    fclose(f);

    if (ret != ESP_OK) {
        free(buf);
        return ret;
    }

    *out_buf = buf;
    *out_w = (uint16_t)width;
    *out_h = (uint16_t)height;
    return ESP_OK;
}
