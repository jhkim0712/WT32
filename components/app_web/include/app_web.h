/**
 * @file app_web.h
 * @brief Web-based configuration UI: a small REST API (esp_http_server +
 *        cJSON) plus a single embedded HTML5/CSS/JS page. Reachable both
 *        through the always-on SoftAP (captive portal) and, once joined,
 *        the home network / mDNS hostname.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Start the HTTP server and register all routes. Call after app_wifi_start(). */
esp_err_t app_web_start(void);

#ifdef __cplusplus
}
#endif
