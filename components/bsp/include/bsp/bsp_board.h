/**
 * @file bsp_board.h
 * @brief Board bring-up: display + touch (LVGL ready), backlight, SD card, audio.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the display (ST7796 over 8080/i80 bus) and the capacitive
 *        touch panel (FT6336U over I2C), then hand both to esp_lvgl_port.
 *
 * After this call returns, LVGL is running its own task and
 * bsp_display_lock()/bsp_display_unlock() must be used around any LVGL API
 * call made from outside that task.
 *
 * @return the LVGL display object (also obtainable later via lv_display_get_default())
 */
lv_display_t *bsp_display_start(void);

/** Take the LVGL mutex. @param timeout_ms 0 = wait forever. @return true if locked. */
bool bsp_display_lock(uint32_t timeout_ms);

/** Release the LVGL mutex taken by bsp_display_lock(). */
void bsp_display_unlock(void);

/** Set backlight brightness. @param percent 0-100. */
void bsp_display_set_backlight(uint8_t percent);

/** Mount the microSD card (SPI mode, FAT filesystem) at BSP_SD_MOUNT_POINT. */
esp_err_t bsp_sdcard_mount(void);

/** Unmount the microSD card. */
esp_err_t bsp_sdcard_unmount(void);

/** @return true if the microSD card is currently mounted. */
bool bsp_sdcard_is_mounted(void);

/**
 * Erase and reformat the microSD card as FAT (wipes everything on it).
 * The card must already be mounted (bsp_sdcard_mount() returned ESP_OK) -
 * ESP-IDF formats it in place and remounts it at BSP_SD_MOUNT_POINT.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not currently mounted.
 */
esp_err_t bsp_sdcard_format(void);

/** Initialize the I2S audio output (amplifier feed). */
esp_err_t bsp_audio_init(void);

/** Play a short square-wave beep. Blocks until the tone has been queued. */
esp_err_t bsp_audio_beep(uint32_t freq_hz, uint32_t duration_ms);

/** Play a 16-bit PCM .wav file from the filesystem (mono or stereo). */
esp_err_t bsp_audio_play_wav(const char *path);

/** Mute/unmute the I2S output without tearing down the driver. */
void bsp_audio_set_mute(bool mute);

#ifdef __cplusplus
}
#endif
