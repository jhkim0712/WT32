/**
 * @file app_wifi.h
 * @brief Wi-Fi manager: tries the stored STA credentials, falls back to a
 *        SoftAP + captive portal so the device is always reachable for
 *        configuration through app_web.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_wifi_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_WIFI_MODE_AP = 0,   /* No (working) credentials stored: SoftAP + captive portal */
    APP_WIFI_MODE_STA = 1,  /* Joined the configured network                            */
} app_wifi_mode_t;

typedef struct {
    char    ssid[33];
    int8_t  rssi;
    wifi_auth_mode_t authmode;
} app_wifi_ap_info_t;

/** Bring up Wi-Fi: attempt STA using the stored config, else start the SoftAP portal. */
esp_err_t app_wifi_start(void);

/** @return the current operating mode. */
app_wifi_mode_t app_wifi_get_mode(void);

/** @return true if associated to a network and holding an IP address. */
bool app_wifi_is_connected(void);

/** Copy the current IPv4 address (STA or AP) as a dotted string into buf. */
esp_err_t app_wifi_get_ip_str(char *buf, size_t buf_len);

/** Copy the SoftAP SSID (e.g. "WT32-A1B2C3", the last 3 octets of its MAC
 *  address) into buf. Valid in any mode. */
void app_wifi_get_ap_ssid(char *buf, size_t buf_len);

/** Blocking scan for nearby access points. @return number of entries written to out. */
size_t app_wifi_scan(app_wifi_ap_info_t *out, size_t max_entries);

/**
 * @brief Try to join a network with the given credentials.
 *
 * On success the device switches to (or stays in) STA mode; the caller is
 * responsible for persisting the credentials to app_config on ESP_OK.
 */
esp_err_t app_wifi_connect_sta(const char *ssid, const char *password, uint32_t timeout_ms);

/** @return current RSSI in dBm, or 0 if not connected. */
int8_t app_wifi_get_rssi(void);

/**
 * @brief Change the fallback SoftAP's password and apply it immediately.
 *
 * @param password NULL or "" for an open network (the default); otherwise
 *                 a WPA2-PSK password, which esp_wifi requires to be at
 *                 least 8 characters (returns ESP_ERR_INVALID_ARG if not).
 *
 * On ESP_OK the caller is responsible for persisting app_config (this
 * function only updates the in-RAM copy and the running Wi-Fi config).
 */
esp_err_t app_wifi_set_ap_password(const char *password);

#ifdef __cplusplus
}
#endif
