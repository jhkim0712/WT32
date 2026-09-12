/**
 * @file app_config.h
 * @brief Single source of truth for user-configurable settings.
 *
 * Each field is persisted to NVS under its own key (namespace "wt32cfg"),
 * not as one monolithic blob. This makes schema evolution safe: a firmware
 * update that adds a field just introduces a new key - app_config_load()
 * leaves any key that isn't found (yet) at the default set by
 * app_config_reset_defaults(), instead of discarding every other setting
 * the user already configured (Wi-Fi credentials included). When adding a
 * field, just add its NVS key + load/save calls in app_config.c; no version
 * bump or migration step needed.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CFG_SSID_MAX_LEN    32
#define APP_CFG_PASS_MAX_LEN    64
#define APP_CFG_STR_MAX_LEN     64
#define APP_CFG_TZ_MAX_LEN      64

typedef enum {
    CLOCK_FACE_DIGITAL = 0,
    CLOCK_FACE_ANALOG  = 1,
} clock_face_t;

typedef struct {
    /* --- Wi-Fi / network --- */
    char     wifi_ssid[APP_CFG_SSID_MAX_LEN];
    char     wifi_password[APP_CFG_PASS_MAX_LEN];
    char     hostname[APP_CFG_STR_MAX_LEN];
    /* Fallback SoftAP password. Empty (the default) = open network, no
     * password - easiest for first-time setup. Set from the web UI to
     * require a password (WPA2-PSK) on the SoftAP instead. */
    char     ap_password[APP_CFG_PASS_MAX_LEN];

    /* --- Time --- */
    char     tz_posix[APP_CFG_TZ_MAX_LEN];  /* e.g. "KST-9" or "PST8PDT,M3.2.0,M11.1.0" */
    char     ntp_server[APP_CFG_STR_MAX_LEN];
    bool     time_24h;
    clock_face_t clock_face;
    bool     chime_enabled;   /* hourly chime */

    /* --- Display --- */
    uint8_t  brightness;          /* 0-100 */
    bool     auto_cycle_enabled;  /* rotate through screens automatically */
    uint16_t cycle_seconds;       /* dwell time per screen when cycling */

    /* --- Photo album --- */
    uint16_t album_interval_s;
    bool     album_shuffle;

    /* --- Weather (OpenWeatherMap) --- */
    bool     weather_enabled;
    char     weather_api_key[APP_CFG_STR_MAX_LEN];  /* OpenWeatherMap API key */
    char     weather_city_id[APP_CFG_STR_MAX_LEN];  /* OpenWeatherMap numeric city/region ID */

    /* --- Audio --- */
    bool     audio_muted;

    /* --- Firmware updates --- */
    char     github_repo[APP_CFG_STR_MAX_LEN]; /* "owner/name", used by the web UI's "Check for updates" */

    /* --- Misc --- */
    bool     first_boot_done;
} app_config_t;

/** Load config from NVS (per-key); any key not yet stored keeps its default. */
esp_err_t app_config_load(void);

/** Persist the current in-RAM config to NVS, one key per field. */
esp_err_t app_config_save(void);

/** @return pointer to the live, in-RAM configuration (do not free). */
app_config_t *app_config_get(void);

/** Reset to factory defaults (in RAM) - caller must still call app_config_save(). */
void app_config_reset_defaults(app_config_t *cfg);

/** Erase all persisted config keys from NVS entirely. */
esp_err_t app_config_erase(void);

#ifdef __cplusplus
}
#endif
