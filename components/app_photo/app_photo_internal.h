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
/** JPEG decodes straight into the caller's canvas_w x canvas_h RGB565
 *  canvas, scaling as it goes - unlike BMP/PNG, no native-resolution buffer
 *  is ever allocated (see app_photo_jpeg.c's file comment for why). */
esp_err_t app_photo_jpeg_decode_to_canvas(const char *path, uint16_t *canvas, uint16_t canvas_w, uint16_t canvas_h);
esp_err_t app_photo_png_decode_native(const char *path, uint16_t **out_buf, uint16_t *out_w, uint16_t *out_h);

/** Nearest-neighbor resize src (src_w x src_h) into the caller's dst_w x
 *  dst_h RGB565 buffer @p dst. */
void app_photo_resize_rgb565(const uint16_t *src, uint16_t src_w, uint16_t src_h,
                             uint16_t *dst, uint16_t dst_w, uint16_t dst_h);
