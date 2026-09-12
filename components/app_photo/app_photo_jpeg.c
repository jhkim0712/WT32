/**
 * @file app_photo_jpeg.c
 * @brief Baseline JPEG decode via the managed `espressif/esp_jpeg` component.
 *
 * This file is intentionally isolated: esp_jpeg's public API has changed
 * shape across releases, so if `idf.py build` fails here after a component
 * upgrade, this is the only place that needs patching - the BMP path in
 * app_photo_bmp.c is unaffected and always works. Check the installed
 * version's README under managed_components/espressif__esp_jpeg for the
 * exact struct/enum names if you land on a release where this doesn't
 * match.
 */
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "jpeg_decoder.h"

#include "app_photo_internal.h"

static const char *TAG = "app_photo_jpeg";

esp_err_t app_photo_jpeg_decode_native(const char *path, uint16_t target_w, uint16_t target_h,
                                        uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsize <= 0) {
        fclose(f);
        return ESP_FAIL;
    }

    /* Both the raw file buffer and (below) the decoded RGB565 buffer scale
     * with image size and can reach into the megabytes - PSRAM has room for
     * that, the ~400KB of internal DRAM left over after LVGL/Wi-Fi/lwIP does
     * not, so ask for it explicitly rather than let the general allocator
     * pick. */
    uint8_t *jpg_data = heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpg_data) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }
    size_t read_len = fread(jpg_data, 1, fsize, f);
    fclose(f);
    if (read_len != (size_t)fsize) {
        free(jpg_data);
        return ESP_FAIL;
    }

    esp_jpeg_image_cfg_t cfg = {
        .indata = jpg_data,
        .indata_size = (uint32_t)fsize,
        .outbuf = NULL,
        .outbuf_size = 0,
        .out_format = JPEG_IMAGE_FORMAT_RGB565,
        .out_scale = JPEG_IMAGE_SCALE_0,
        .flags = {
            .swap_color_bytes = 1, /* match LV_COLOR_16_SWAP in sdkconfig.defaults */
        },
    };
    esp_jpeg_image_output_t out_img = {0};

    /* First pass at scale 0 just to learn the native resolution - cheap,
     * esp_jpeg_get_image_info() never touches outbuf/outbuf_size. */
    esp_err_t ret = esp_jpeg_get_image_info(&cfg, &out_img);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s: failed to parse JPEG header (%s)", path, esp_err_to_name(ret));
        free(jpg_data);
        return ret;
    }

    /* Pick the smallest decode scale (largest divisor) whose result still
     * covers the requested canvas in both dimensions, so we never decode a
     * multi-megapixel phone photo at full native resolution just to shrink
     * it back down to a 480x320 screen. */
    if (target_w > 0 && target_h > 0) {
        if (out_img.width >= target_w * 8 && out_img.height >= target_h * 8) {
            cfg.out_scale = JPEG_IMAGE_SCALE_1_8;
        } else if (out_img.width >= target_w * 4 && out_img.height >= target_h * 4) {
            cfg.out_scale = JPEG_IMAGE_SCALE_1_4;
        } else if (out_img.width >= target_w * 2 && out_img.height >= target_h * 2) {
            cfg.out_scale = JPEG_IMAGE_SCALE_1_2;
        }
    }

    if (cfg.out_scale != JPEG_IMAGE_SCALE_0) {
        /* Re-query at the chosen scale: output_len/width/height above were
         * for scale 0 (native) and don't reflect the shrunk size. */
        cfg.priv.read = 0;
        ret = esp_jpeg_get_image_info(&cfg, &out_img);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "%s: failed to re-parse JPEG header at scale (%s)", path, esp_err_to_name(ret));
            free(jpg_data);
            return ret;
        }
    }

    size_t out_size = out_img.output_len;
    if (out_size == 0 || out_size > PHOTO_MAX_DECODE_BYTES) {
        ESP_LOGW(TAG, "%s: %dx%d is too large to decode (limit %d bytes)", path, out_img.width, out_img.height,
                  PHOTO_MAX_DECODE_BYTES);
        free(jpg_data);
        return ESP_ERR_NO_MEM;
    }

    uint16_t *pixels = heap_caps_malloc(out_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        free(jpg_data);
        return ESP_ERR_NO_MEM;
    }

    cfg.outbuf = (uint8_t *)pixels;
    cfg.outbuf_size = (uint32_t)out_size;
    ret = esp_jpeg_decode(&cfg, &out_img);
    free(jpg_data);

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "%s: JPEG decode failed (%s)", path, esp_err_to_name(ret));
        free(pixels);
        return ret;
    }

    *out_buf = pixels;
    *out_w = (uint16_t)out_img.width;
    *out_h = (uint16_t)out_img.height;
    return ESP_OK;
}
