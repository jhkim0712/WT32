#include <stdlib.h>
#include <string.h>
#include "app_photo_internal.h"

uint16_t *app_photo_resize_rgb565(const uint16_t *src, uint16_t src_w, uint16_t src_h,
                                   uint16_t dst_w, uint16_t dst_h)
{
    uint16_t *dst = malloc((size_t)dst_w * dst_h * sizeof(uint16_t));
    if (!dst) {
        return NULL;
    }

    if (src_w == dst_w && src_h == dst_h) {
        memcpy(dst, src, (size_t)dst_w * dst_h * sizeof(uint16_t));
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
