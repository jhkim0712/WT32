/**
 * @file main.c
 * @brief WT32-SC01 Plus firmware entry point.
 *
 * Bring-up order matters here:
 *   1. NVS (everything else persists through it)
 *   2. app_config (so every later step can read user settings)
 *   3. Display + touch (so we have something on screen ASAP)
 *   4. SD card + photo album scan
 *   5. Audio
 *   6. Wi-Fi (SoftAP always, STA best-effort) + SNTP + web server
 *   7. LVGL UI
 */
#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"

#include "app_config.h"
#include "app_wifi.h"
#include "app_time.h"
#include "app_web.h"
#include "app_photo.h"
#include "app_ota.h"
#include "bsp/bsp_board.h"
#include "bsp/bsp_pins.h"
#include "ui.h"

static const char *TAG = "main";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erasing (%s)", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    app_ota_init(); /* just sets up internal state - safe to call this early */

    app_config_load();
    app_config_t *cfg = app_config_get();

    ESP_LOGI(TAG, "Starting display...");
    bsp_display_start();

    if (bsp_sdcard_mount() == ESP_OK) {
        app_photo_scan(BSP_SD_MOUNT_POINT "/photos");
        if (cfg->album_shuffle) {
            app_photo_shuffle();
        }
    } else {
        ESP_LOGW(TAG, "SD card not found - the photo album screen will be empty");
    }

    if (bsp_audio_init() == ESP_OK) {
        bsp_audio_set_mute(cfg->audio_muted);
    } else {
        ESP_LOGW(TAG, "Audio init failed - continuing without sound");
    }

    ESP_LOGI(TAG, "Starting Wi-Fi...");
    ESP_ERROR_CHECK(app_wifi_start());
    app_time_start();
    app_web_start();

    ESP_LOGI(TAG, "Starting UI...");
    ui_init();

    bsp_audio_beep(1568, 100); /* boot chime: a short G6 blip */

    /* Everything came up without crashing - confirm this image so the
     * bootloader stops treating it as "pending verify" (see app_ota.h). */
    app_ota_confirm_boot();

    ESP_LOGI(TAG, "WT32 firmware ready");
}
