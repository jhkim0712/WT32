/**
 * @file app_photo_jpeg.c
 * @brief Baseline JPEG decode straight into the caller's canvas, via the
 *        TJpgDec decoder that the managed `espressif/esp_jpeg` component
 *        selects (the ESP32-S3's ROM copy by default, see CONFIG_JD_USE_ROM).
 *
 * Deliberately bypasses esp_jpeg_decode() itself: that API only knows how to
 * write the whole decoded image into one outbuf, so showing e.g. a 640x427
 * Flickr "_z" photo needed a 546KB RGB565 buffer (plus the whole file read
 * into PSRAM first) on top of the permanent 480x320 canvas - more contiguous
 * PSRAM than this board has left once LVGL, Wi-Fi/TLS and the canvas are
 * all in. Driving TJpgDec directly instead gets us its per-block output
 * callback, which nearest-neighbor scales each decoded block into the
 * canvas as it arrives, and its input callback, which streams the file off
 * the SD card - so the only allocation left is TJpgDec's own ~3KB work pool.
 */
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_rom_caps.h"
#include "sdkconfig.h"

#if CONFIG_JD_USE_ROM
#include "rom/tjpgd.h"
/* The ROM copy is an older TJpgDec with different callback signatures, and
 * always outputs RGB888 (JD_FORMAT 0). */
typedef unsigned int jd_in_len_t;
typedef unsigned int jd_out_ret_t;
#else
#include "tjpgd.h"
typedef size_t jd_in_len_t;
typedef int jd_out_ret_t;
#endif

#include "app_photo_internal.h"

static const char *TAG = "app_photo_jpeg";

/* TJpgDec's documented minimum work pool, independent of image size (same
 * value esp_jpeg itself uses). Too big for the stack of whatever LVGL task
 * calls this, so it's malloc'd - small enough to land in internal RAM. */
#define JPEG_WORK_POOL_SIZE 3100

typedef struct {
    FILE *f;
    uint16_t *canvas;
    uint16_t canvas_w, canvas_h;
    uint32_t x_ratio, y_ratio; /* 16.16 fixed point, same mapping as app_photo_resize.c */
} jpeg_ctx_t;

static jd_in_len_t in_cb(JDEC *jd, uint8_t *buf, jd_in_len_t len)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)jd->device;
    if (!buf) {
        /* TJpgDec's "skip len bytes" request */
        return fseek(ctx->f, (long)len, SEEK_CUR) == 0 ? len : 0;
    }
    return (jd_in_len_t)fread(buf, 1, len, ctx->f);
}

/* First canvas pixel whose source coordinate (d * ratio) >> 16 is >= src,
 * i.e. the exact inverse of app_photo_resize.c's forward mapping. */
static uint32_t first_dst(uint32_t src, uint32_t ratio)
{
    return (uint32_t)((((uint64_t)src << 16) + ratio - 1) / ratio);
}

static jd_out_ret_t out_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)jd->device;
    uint32_t rect_w = rect->right - rect->left + 1;

    uint32_t y_end = first_dst((uint32_t)rect->bottom + 1, ctx->y_ratio);
    if (y_end > ctx->canvas_h) {
        y_end = ctx->canvas_h;
    }
    uint32_t x_start = first_dst(rect->left, ctx->x_ratio);
    uint32_t x_end = first_dst((uint32_t)rect->right + 1, ctx->x_ratio);
    if (x_end > ctx->canvas_w) {
        x_end = ctx->canvas_w;
    }

    for (uint32_t y = first_dst(rect->top, ctx->y_ratio); y < y_end; y++) {
        uint32_t src_row = ((y * ctx->y_ratio) >> 16) - rect->top;
        uint16_t *dst = ctx->canvas + (size_t)y * ctx->canvas_w;
        for (uint32_t x = x_start; x < x_end; x++) {
            uint32_t src_i = src_row * rect_w + (((x * ctx->x_ratio) >> 16) - rect->left);
#if JD_FORMAT == 1
            dst[x] = ((const uint16_t *)bitmap)[src_i];
#else
            /* No byte swap here, same as the PNG/BMP decoders - the panel's
             * one required swap is applied to the whole framebuffer by
             * esp_lvgl_port's `.flags.swap_bytes` (see bsp_display.c). */
            const uint8_t *px = (const uint8_t *)bitmap + src_i * 3;
            dst[x] = (uint16_t)(((px[0] & 0xF8) << 8) | ((px[1] & 0xFC) << 3) | (px[2] >> 3));
#endif
        }
    }
    return 1; /* continue decoding */
}

esp_err_t app_photo_jpeg_decode_to_canvas(const char *path, uint16_t *canvas, uint16_t canvas_w, uint16_t canvas_h)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return ESP_ERR_NOT_FOUND;
    }

    void *pool = malloc(JPEG_WORK_POOL_SIZE);
    if (!pool) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    jpeg_ctx_t ctx = {
        .f = f,
        .canvas = canvas,
        .canvas_w = canvas_w,
        .canvas_h = canvas_h,
    };
    JDEC jd;
    esp_err_t ret = ESP_OK;

    JRESULT res = jd_prepare(&jd, in_cb, pool, JPEG_WORK_POOL_SIZE, &ctx);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "%s: failed to parse JPEG header (TJpgDec error %d)", path, res);
        ret = ESP_FAIL;
        goto out;
    }

    /* Pick the smallest decode scale (largest divisor, up to TJpgDec's 1/8)
     * whose result still covers the canvas in both dimensions - no memory
     * reason to any more, but it's free speed and the scaled-down source
     * looks the same after nearest-neighbor sampling. */
    uint8_t scale = 0;
    while (scale < 3 && (jd.width >> (scale + 1)) >= canvas_w && (jd.height >> (scale + 1)) >= canvas_h) {
        scale++;
    }
    /* TJpgDec's scaled output covers exactly (width >> scale) x
     * (height >> scale) - see its mcu_output() - so every canvas pixel maps
     * inside some block and gets written exactly once. */
    uint32_t src_w = jd.width >> scale;
    uint32_t src_h = jd.height >> scale;
    if (src_w == 0 || src_h == 0) {
        ESP_LOGW(TAG, "%s: %ux%u is too small to decode", path, (unsigned)jd.width, (unsigned)jd.height);
        ret = ESP_FAIL;
        goto out;
    }
    ctx.x_ratio = (src_w << 16) / canvas_w;
    ctx.y_ratio = (src_h << 16) / canvas_h;

    res = jd_decomp(&jd, out_cb, scale);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "%s: JPEG decode failed (TJpgDec error %d)", path, res);
        ret = ESP_FAIL;
    }

out:
    free(pool);
    fclose(f);
    return ret;
}
