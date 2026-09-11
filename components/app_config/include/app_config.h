/**
 * @file app_config.h
 * @brief Single source of truth for user-configurable settings.
 *
 * The whole struct is persisted to NVS as one binary blob (namespace
 * "wt32cfg", key "cfg"). This keeps app_config.c tiny; if you add a field,
 * bump APP_CONFIG_VERSION so old, incompatible blobs are discarded instead
 * of being reinterpreted as garbage.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_CONFIG_VERSION      3

#define APP_CFG_SSID_MAX_LEN    32
#define APP_CFG_PASS_MAX_LEN    64
#define APP_CFG_STR_MAX_LEN     64
#define APP_CFG_TZ_MAX_LEN      64

typedef enum {
    CLOCK_FACE_DIGITAL = 0,
    CLOCK_FACE_ANALOG  = 1,
} clock_face_t;

typedef struct {
    uint32_t version;

    /* --- Wi-Fi / network --- */
    char     wifi_ssid[APP_CFG_SSID_MAX_LEN];
    char     wifi_password[APP_CFG_PASS_MAX_LEN];
    char     hostname[APP_CFG_STR_MAX_LEN];

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

    /* --- Audio --- */
    bool     audio_muted;

    /* --- Misc --- */
    bool     first_boot_done;
} app_config_t;

/** Load config from NVS, or populate sane defaults if none is stored / it is stale. */
esp_err_t app_config_load(void);

/** Persist the current in-RAM config to NVS. */
esp_err_t app_config_save(void);

/** @return pointer to the live, in-RAM configuration (do not free). */
app_config_t *app_config_get(void);

/** Reset to factory defaults (in RAM) - caller must still call app_config_save(). */
void app_config_reset_defaults(app_config_t *cfg);

/** Erase the persisted config blob from NVS entirely. */
esp_err_t app_config_erase(void);

#ifdef __cplusplus
}
#endif
