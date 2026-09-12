#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "app_photo_internal.h"

static const char *TAG = "app_photo_resize";

uint16_t *app_photo_resize_rgb565(const uint16_t *src, uint16_t src_w, uint16_t src_h,
                                   uint16_t dst_w, uint16_t dst_h)
{
    /* Runs on every photo shown (the fixed 480x320 canvas is ~300KB) - PSRAM
     * has room for that, the ~400KB of internal DRAM left over after
     * LVGL/Wi-Fi/lwIP does not, so ask for it explicitly rather than let the
     * general allocator pick. */
    size_t dst_size = (size_t)dst_w * dst_h * sizeof(uint16_t);
    uint16_t *dst = heap_caps_malloc(dst_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dst) {
        ESP_LOGW(TAG, "out of PSRAM allocating the %ux%u canvas (%u bytes needed, %u free)", dst_w, dst_h,
                 (unsigned)dst_size, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return NULL;
    }

    if (src_w == dst_w && src_h == dst_h) {
        memcpy(dst, src, dst_size);
        return dst;
    }

    /* Fixed-point (16.16) nearest-neighbor scaling - keeps this dependency-free
     * and fast enough for a slideshow (not a real-time video path). */
    uint32_t x_ratio = ((uint32_t)src_w << 16) / dst_w;
    uint32_t y_ratio = ((uint32_t)src_h << 16) / dst_h;

    for (uint16_t y = 0; y < dst_h; y++) {
        uint16_t src_y = (uint16_t)((y * y_ratio) >> 16);
        if (src_y >= src_h) {
            src_y = src_h - 1;
        }
        const uint16_t *src_row = src + (size_t)src_y * src_w;
        uint16_t *dst_row = dst + (size_t)y * dst_w;
        for (uint16_t x = 0; x < dst_w; x++) {
            uint16_t src_x = (uint16_t)((x * x_ratio) >> 16);
            if (src_x >= src_w) {
                src_x = src_w - 1;
            }
            dst_row[x] = src_row[src_x];
        }
    }
    return dst;
}
