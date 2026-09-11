/**
 * @file app_ota.h
 * @brief Firmware updates: manual .bin upload from the web UI, "check
 *        GitHub releases" + one-click install, and a firmware validity
 *        check so a stray/wrong binary can't be flashed.
 *
 * Two independent safety nets protect against a bad upload:
 *   1. `esp_ota_write()`/`esp_ota_end()` (and `esp_https_ota_*`) already
 *      reject anything that isn't a well-formed, checksummed ESP32 app
 *      image - a random file or a corrupt download never gets this far.
 *   2. On top of that, every image is checked against its embedded
 *      `esp_app_desc_t.project_name` (see FIRMWARE_PROJECT_NAME in
 *      app_ota.c) - this catches a perfectly valid ESP32 firmware image
 *      that simply isn't *this* project's, which (1) alone would accept.
 *
 * A third net is passive: `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is on
 * (see sdkconfig.defaults), and app_ota_init() confirms the running image
 * only after the rest of app_main() has brought the UI up - if a newly
 * flashed image crashes before that point, the bootloader automatically
 * reverts to the previous working image on the next boot.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_OTA_STATE_IDLE = 0,
    APP_OTA_STATE_DOWNLOADING,
    APP_OTA_STATE_WRITING,
    APP_OTA_STATE_SUCCESS,
    APP_OTA_STATE_ERROR,
} app_ota_state_t;

typedef struct {
    app_ota_state_t state;
    char message[128];
    int progress_pct; /* 0-100, or -1 if unknown (e.g. server sent no Content-Length) */
} app_ota_status_t;

/** Call once, early in app_main() (right after NVS init, before Wi-Fi/web
 *  start) - sets up internal state only, safe to call before anything else
 *  in this header is used. */
esp_err_t app_ota_init(void);

/** Call once, near the *end* of app_main() after the UI is up and running:
 *  confirms the current image so the bootloader stops treating it as
 *  "pending verify" and cancels the auto-rollback safety net. If a newly
 *  flashed image never reaches this call (it crashes/reboots first), the
 *  bootloader reverts to the previous working image on the next boot. */
void app_ota_confirm_boot(void);

/** Copy the running firmware's version string (from version.txt / esp_app_desc_t). */
void app_ota_get_current_version(char *buf, size_t len);

/**
 * @brief Query `https://api.github.com/repos/<owner>/<repo>/releases/latest`
 *        for the newest release and a `.bin` asset to install.
 *
 * Blocking (a single HTTPS request, a few seconds) - call from an HTTP
 * handler directly, it's not long enough to need a background task.
 */
esp_err_t app_ota_check_github(const char *owner_repo,
                                char *out_version, size_t ver_len,
                                char *out_asset_url, size_t url_len,
                                char *out_notes, size_t notes_len);

/** @return true if @p latest looks newer than @p current (best-effort semver compare). */
bool app_ota_version_is_newer(const char *latest, const char *current);

/**
 * @brief Download and install firmware from a direct .bin URL.
 *
 * Runs in a background task; poll app_ota_get_status() for progress. Fails
 * immediately (returns ESP_ERR_INVALID_STATE) if an OTA is already running.
 */
esp_err_t app_ota_start_from_url(const char *url);

/** Thread-safe snapshot of the current OTA operation's status. */
void app_ota_get_status(app_ota_status_t *out);

/* --- Manual upload (streamed from an HTTP request body) --- */

/** Begin a manual upload. Fails if another OTA is already in progress. */
esp_err_t app_ota_upload_begin(void);

/** Feed the next chunk of the .bin body. Validates the firmware identity
 *  as soon as enough header bytes have arrived; returns an error (and
 *  aborts the write) the first time something looks wrong. */
esp_err_t app_ota_upload_write(const uint8_t *data, size_t len);

/** Finalize a manual upload: verifies the complete image and, on success,
 *  sets it as the boot partition. Call esp_restart() yourself afterwards. */
esp_err_t app_ota_upload_finish(void);

/** Abort an in-progress manual upload (e.g. the client disconnected). */
void app_ota_upload_abort(void);

#ifdef __cplusplus
}
#endif
