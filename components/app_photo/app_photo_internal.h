/**
 * @file app_photo_internal.h
 * @brief Shared helpers between the format-specific decoders (private header,
 *        not installed under include/ - only used inside this component).
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/** Refuse to decode anything whose native RGB565 buffer would exceed this
 *  many bytes (protects PSRAM on very large/misplaced source images). */
#define PHOTO_MAX_DECODE_BYTES (3 * 1024 * 1024)

esp_err_t app_photo_bmp_decode_native(const char *path, uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h);
esp_err_t app_photo_jpeg_decode_native(const char *path, uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h);

/** Nearest-neighbor resize src (src_w x src_h) into a freshly malloc'd
 *  dst_w x dst_h RGB565 buffer. Returns NULL on allocation failure. */
uint16_t *app_photo_resize_rgb565(const uint16_t *src, uint16_t src_w, uint16_t src_h,
                                   uint16_t dst_w, uint16_t dst_h);
