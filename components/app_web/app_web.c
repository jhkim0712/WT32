/**
 * @file app_web.c
 * @brief REST API + embedded HTML5/CSS/JS configuration page.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "app_web.h"
#include "app_config.h"
#include "app_wifi.h"
#include "app_time.h"
#include "app_photo.h"
#include "app_weather.h"
#include "app_ota.h"
#include "app_files.h"
#include "bsp/bsp_board.h"
#include "bsp/bsp_pins.h" /* BSP_SD_MOUNT_POINT */

static const char *TAG = "app_web";

#define MAX_BODY_LEN 4096
#define OTA_UPLOAD_CHUNK 2048

/* Embedded web assets - the build system's EMBED_FILES symbol is derived
 * from just the base filename (directory stripped), not the full
 * "webapp/..." path passed in CMakeLists.txt - see
 * tools/cmake/scripts/data_file_embed_asm.cmake in ESP-IDF. */
extern const uint8_t webapp_index_html_start[] asm("_binary_index_html_start");
extern const uint8_t webapp_index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t webapp_style_css_start[]  asm("_binary_style_css_start");
extern const uint8_t webapp_style_css_end[]    asm("_binary_style_css_end");
extern const uint8_t webapp_app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t webapp_app_js_end[]       asm("_binary_app_js_end");

/* ---------------------------------------------------------------------- */
/* Helpers                                                                 */
/* ---------------------------------------------------------------------- */

static char *read_body(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > MAX_BODY_LEN) {
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        return NULL;
    }
    int received = 0;
    while (received < req->content_len) {
        int ret = httpd_req_recv(req, buf + received, req->content_len - received);
        if (ret <= 0) {
            free(buf);
            return NULL;
        }
        received += ret;
    }
    buf[req->content_len] = '\0';
    return buf;
}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    char *json = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t ret = httpd_resp_sendstr(req, json ? json : "{}");
    free(json);
    cJSON_Delete(root);
    return ret;
}

static esp_err_t send_ok(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t send_json_error(httpd_req_t *req, const char *status, const char *error)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char buf[192];
    /* "error" is always one of this file's own short literal error codes -
     * GCC can't see that from a `const char *` parameter, so silence the
     * truncation warning instead of restructuring around it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(buf, sizeof(buf), "{\"ok\":false,\"error\":\"%s\"}", error);
#pragma GCC diagnostic pop
    return httpd_resp_sendstr(req, buf);
}

static void delayed_restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/** In-place percent-decode (the frontend always calls encodeURIComponent() on
 *  path/name values before putting them in a query string - this undoes
 *  that so filenames with spaces, parentheses, non-ASCII characters, etc.
 *  work). Leaves anything that isn't a valid "%XX" escape untouched. */
static void url_decode(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (r[0] == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char hex[3] = {r[1], r[2], '\0'};
            *w++ = (char)strtol(hex, NULL, 16);
            r += 2;
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

/* ---------------------------------------------------------------------- */
/* Static assets                                                           */
/* ---------------------------------------------------------------------- */

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, (const char *)webapp_index_html_start,
                            webapp_index_html_end - webapp_index_html_start);
}

static esp_err_t style_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    return httpd_resp_send(req, (const char *)webapp_style_css_start,
                            webapp_style_css_end - webapp_style_css_start);
}

static esp_err_t script_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    return httpd_resp_send(req, (const char *)webapp_app_js_start,
                            webapp_app_js_end - webapp_app_js_start);
}

/* index.html links an inline SVG favicon, but some browsers request
 * /favicon.ico directly regardless - without this it falls through to the
 * 404 handler below, which still works (redirects to "/") but logs an IDF
 * "URI not found" warning on every single page load. A quiet empty
 * response is simpler than embedding an actual .ico asset just for this. */
static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* ---------------------------------------------------------------------- */
/* /api/status                                                             */
/* ---------------------------------------------------------------------- */

static esp_err_t status_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    char ip[16] = "-";
    app_wifi_get_ip_str(ip, sizeof(ip));
    char ap_ssid[33] = {0};
    app_wifi_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    bool connected = app_wifi_is_connected();

    cJSON_AddStringToObject(root, "mode", connected ? "sta" : "ap");
    cJSON_AddBoolToObject(root, "connected", connected);
    cJSON_AddStringToObject(root, "ip", ip);
    cJSON_AddStringToObject(root, "ap_ssid", ap_ssid);
    cJSON_AddNumberToObject(root, "rssi", app_wifi_get_rssi());
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_free", esp_get_free_heap_size());
    cJSON_AddBoolToObject(root, "sd_mounted", bsp_sdcard_is_mounted());
    cJSON_AddNumberToObject(root, "photo_count", (double)app_photo_count());
    cJSON_AddBoolToObject(root, "time_synced", app_time_is_synced());
    char fw_version[32];
    app_ota_get_current_version(fw_version, sizeof(fw_version));
    cJSON_AddStringToObject(root, "fw_version", fw_version);

    struct tm now;
    app_time_get_local(&now);
    char time_buf[32];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &now);
    cJSON_AddStringToObject(root, "time", time_buf);

    return send_json(req, root);
}

/* ---------------------------------------------------------------------- */
/* /api/config                                                             */
/* ---------------------------------------------------------------------- */

static const char *clock_face_to_str(clock_face_t f)
{
    return f == CLOCK_FACE_ANALOG ? "analog" : "digital";
}

static const char *display_theme_to_str(display_theme_t t)
{
    switch (t) {
        case DISPLAY_THEME_LIGHT: return "light";
        case DISPLAY_THEME_AUTO:  return "auto";
        default:                  return "dark";
    }
}

static esp_err_t config_get_handler(httpd_req_t *req)
{
    app_config_t *cfg = app_config_get();
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "hostname", cfg->hostname);
    cJSON_AddStringToObject(root, "wifi_ssid", cfg->wifi_ssid);
    cJSON_AddStringToObject(root, "tz_posix", cfg->tz_posix);
    cJSON_AddStringToObject(root, "ntp_server", cfg->ntp_server);
    cJSON_AddBoolToObject(root, "time_24h", cfg->time_24h);
    cJSON_AddStringToObject(root, "clock_face", clock_face_to_str(cfg->clock_face));
    cJSON_AddBoolToObject(root, "chime_enabled", cfg->chime_enabled);
    cJSON_AddNumberToObject(root, "brightness", cfg->brightness);
    cJSON_AddBoolToObject(root, "auto_cycle_enabled", cfg->auto_cycle_enabled);
    cJSON_AddNumberToObject(root, "cycle_seconds", cfg->cycle_seconds);
    cJSON_AddStringToObject(root, "display_theme", display_theme_to_str(cfg->display_theme));
    cJSON_AddStringToObject(root, "theme_day_start", cfg->theme_day_start);
    cJSON_AddStringToObject(root, "theme_night_start", cfg->theme_night_start);
    cJSON_AddNumberToObject(root, "album_interval_s", cfg->album_interval_s);
    cJSON_AddBoolToObject(root, "album_shuffle", cfg->album_shuffle);
    cJSON_AddBoolToObject(root, "weather_enabled", cfg->weather_enabled);
    cJSON_AddStringToObject(root, "weather_api_key", cfg->weather_api_key);
    cJSON_AddStringToObject(root, "weather_city_id", cfg->weather_city_id);
    cJSON_AddBoolToObject(root, "audio_muted", cfg->audio_muted);
    cJSON_AddStringToObject(root, "github_repo", cfg->github_repo);

    return send_json(req, root);
}

static esp_err_t config_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    app_config_t *cfg = app_config_get();
    cJSON *item;

    if ((item = cJSON_GetObjectItem(root, "hostname")) && cJSON_IsString(item)) {
        strncpy(cfg->hostname, item->valuestring, sizeof(cfg->hostname) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "tz_posix")) && cJSON_IsString(item)) {
        strncpy(cfg->tz_posix, item->valuestring, sizeof(cfg->tz_posix) - 1);
        app_time_set_timezone(cfg->tz_posix);
    }
    if ((item = cJSON_GetObjectItem(root, "ntp_server")) && cJSON_IsString(item)) {
        strncpy(cfg->ntp_server, item->valuestring, sizeof(cfg->ntp_server) - 1);
        app_time_set_server(cfg->ntp_server);
    }
    if ((item = cJSON_GetObjectItem(root, "time_24h")) && cJSON_IsBool(item)) {
        cfg->time_24h = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "clock_face")) && cJSON_IsString(item)) {
        cfg->clock_face = (strcmp(item->valuestring, "analog") == 0) ? CLOCK_FACE_ANALOG : CLOCK_FACE_DIGITAL;
    }
    if ((item = cJSON_GetObjectItem(root, "chime_enabled")) && cJSON_IsBool(item)) {
        cfg->chime_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "brightness")) && cJSON_IsNumber(item)) {
        int v = (int)item->valuedouble;
        cfg->brightness = (uint8_t)(v < 0 ? 0 : (v > 100 ? 100 : v));
        bsp_display_set_backlight(cfg->brightness);
    }
    if ((item = cJSON_GetObjectItem(root, "auto_cycle_enabled")) && cJSON_IsBool(item)) {
        cfg->auto_cycle_enabled = cJSON_IsTrue(item);
    }
    if ((item = cJSON_GetObjectItem(root, "cycle_seconds")) && cJSON_IsNumber(item)) {
        cfg->cycle_seconds = (uint16_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(root, "display_theme")) && cJSON_IsString(item)) {
        if (strcmp(item->valuestring, "light") == 0) {
            cfg->display_theme = DISPLAY_THEME_LIGHT;
        } else if (strcmp(item->valuestring, "auto") == 0) {
            cfg->display_theme = DISPLAY_THEME_AUTO;
        } else {
            cfg->display_theme = DISPLAY_THEME_DARK;
        }
    }
    if ((item = cJSON_GetObjectItem(root, "theme_day_start")) && cJSON_IsString(item)) {
        strncpy(cfg->theme_day_start, item->valuestring, sizeof(cfg->theme_day_start) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "theme_night_start")) && cJSON_IsString(item)) {
        strncpy(cfg->theme_night_start, item->valuestring, sizeof(cfg->theme_night_start) - 1);
    }
    if ((item = cJSON_GetObjectItem(root, "album_interval_s")) && cJSON_IsNumber(item)) {
        cfg->album_interval_s = (uint16_t)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(root, "album_shuffle")) && cJSON_IsBool(item)) {
        cfg->album_shuffle = cJSON_IsTrue(item);
        if (cfg->album_shuffle) {
            app_photo_shuffle();
        } else {
            app_photo_unshuffle();
        }
    }
    if ((item = cJSON_GetObjectItem(root, "weather_enabled")) && cJSON_IsBool(item)) {
        cfg->weather_enabled = cJSON_IsTrue(item);
        app_weather_request_refresh();
    }
    if ((item = cJSON_GetObjectItem(root, "weather_api_key")) && cJSON_IsString(item)) {
        strncpy(cfg->weather_api_key, item->valuestring, sizeof(cfg->weather_api_key) - 1);
        app_weather_request_refresh();
    }
    if ((item = cJSON_GetObjectItem(root, "weather_city_id")) && cJSON_IsString(item)) {
        strncpy(cfg->weather_city_id, item->valuestring, sizeof(cfg->weather_city_id) - 1);
        app_weather_request_refresh();
    }
    if ((item = cJSON_GetObjectItem(root, "audio_muted")) && cJSON_IsBool(item)) {
        cfg->audio_muted = cJSON_IsTrue(item);
        bsp_audio_set_mute(cfg->audio_muted);
    }
    if ((item = cJSON_GetObjectItem(root, "github_repo")) && cJSON_IsString(item)) {
        strncpy(cfg->github_repo, item->valuestring, sizeof(cfg->github_repo) - 1);
    }

    cJSON_Delete(root);
    app_config_save();
    ESP_LOGI(TAG, "Config updated via web UI");
    return send_ok(req);
}

/* ---------------------------------------------------------------------- */
/* /api/weather                                                            */
/* ---------------------------------------------------------------------- */

static esp_err_t weather_get_handler(httpd_req_t *req)
{
    app_config_t *cfg = app_config_get();
    app_weather_data_t w;
    app_weather_get(&w);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "enabled", cfg->weather_enabled);
    cJSON_AddBoolToObject(root, "configured", cfg->weather_api_key[0] && cfg->weather_city_id[0]);
    cJSON_AddBoolToObject(root, "valid", w.valid);
    cJSON_AddBoolToObject(root, "have_error", w.have_error);
    cJSON_AddStringToObject(root, "error", w.error);
    cJSON_AddStringToObject(root, "description", w.description);
    cJSON_AddStringToObject(root, "city_name", w.city_name);
    cJSON_AddNumberToObject(root, "temp_c", w.temp_c);
    cJSON_AddNumberToObject(root, "feels_like_c", w.feels_like_c);
    cJSON_AddNumberToObject(root, "temp_min_c", w.temp_min_c);
    cJSON_AddNumberToObject(root, "temp_max_c", w.temp_max_c);
    cJSON_AddNumberToObject(root, "humidity_pct", w.humidity_pct);
    cJSON_AddNumberToObject(root, "wind_speed_ms", w.wind_speed_ms);
    cJSON_AddNumberToObject(root, "updated_at", (double)w.updated_at);

    return send_json(req, root);
}

/* ---------------------------------------------------------------------- */
/* /api/wifi/... routes                                                   */
/* ---------------------------------------------------------------------- */

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    static app_wifi_ap_info_t aps[20];
    size_t ap_count = app_wifi_scan(aps, sizeof(aps) / sizeof(aps[0]));

    cJSON *arr = cJSON_CreateArray();
    for (size_t ap_i = 0; ap_i < ap_count; ap_i++) {
        cJSON *ap_json = cJSON_CreateObject();
        cJSON_AddStringToObject(ap_json, "ssid", aps[ap_i].ssid);
        cJSON_AddNumberToObject(ap_json, "rssi", aps[ap_i].rssi);
        cJSON_AddBoolToObject(ap_json, "secure", aps[ap_i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, ap_json);
    }
    return send_json(req, arr);
}

static esp_err_t wifi_connect_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass_item = cJSON_GetObjectItem(root, "password");
    if (!cJSON_IsString(ssid_item) || strlen(ssid_item->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing ssid");
        return ESP_FAIL;
    }

    char ssid[33];
    char password[65];
    strncpy(ssid, ssid_item->valuestring, sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    strncpy(password, cJSON_IsString(pass_item) ? pass_item->valuestring : "", sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Web UI requested connection to \"%s\"", ssid);
    esp_err_t ret = app_wifi_connect_sta(ssid, password, 15000);

    if (ret == ESP_OK) {
        app_config_t *cfg = app_config_get();
        strncpy(cfg->wifi_ssid, ssid, sizeof(cfg->wifi_ssid) - 1);
        strncpy(cfg->wifi_password, password, sizeof(cfg->wifi_password) - 1);
        app_config_save();
    }

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, ret == ESP_OK ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"connect_failed\"}");
}

/* ---------------------------------------------------------------------- */
/* /api/system/... routes                                                 */
/* ---------------------------------------------------------------------- */

static esp_err_t restart_post_handler(httpd_req_t *req)
{
    esp_err_t ret = send_ok(req);
    xTaskCreate(delayed_restart_task, "restart", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    return ret;
}

static esp_err_t factory_reset_post_handler(httpd_req_t *req)
{
    esp_err_t ret = send_ok(req);
    app_config_erase();
    xTaskCreate(delayed_restart_task, "restart", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    return ret;
}

/* Body: {"password": "..."} - "" (or omitted) means "no password" (open). */
static esp_err_t ap_password_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }

    cJSON *pw_item = cJSON_GetObjectItem(root, "password");
    const char *password = cJSON_IsString(pw_item) ? pw_item->valuestring : "";

    esp_err_t ret = app_wifi_set_ap_password(password);
    cJSON_Delete(root);

    if (ret == ESP_ERR_INVALID_ARG) {
        return send_json_error(req, "400 Bad Request", "password_too_short");
    }
    if (ret != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "could_not_apply");
    }
    app_config_save();
    return send_ok(req);
}

/* ---------------------------------------------------------------------- */
/* /api/ota/... routes - firmware updates                                 */
/* ---------------------------------------------------------------------- */

static esp_err_t ota_status_get_handler(httpd_req_t *req)
{
    static const char *state_names[] = {"idle", "downloading", "writing", "success", "error"};
    app_ota_status_t st;
    app_ota_get_status(&st);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "state", state_names[st.state]);
    cJSON_AddStringToObject(root, "message", st.message);
    cJSON_AddNumberToObject(root, "progress_pct", st.progress_pct);

    char cur_ver[32];
    app_ota_get_current_version(cur_ver, sizeof(cur_ver));
    cJSON_AddStringToObject(root, "current_version", cur_ver);

    return send_json(req, root);
}

/* Blocking (a single HTTPS request) - see app_ota_check_github(). */
static esp_err_t ota_check_get_handler(httpd_req_t *req)
{
    app_config_t *cfg = app_config_get();
    if (cfg->github_repo[0] == '\0') {
        return send_json_error(req, "400 Bad Request", "no_repo_configured");
    }

    char latest_version[32] = {0};
    char asset_url[256] = {0};
    char notes[512] = {0};
    esp_err_t ret = app_ota_check_github(cfg->github_repo, latest_version, sizeof(latest_version),
                                          asset_url, sizeof(asset_url), notes, sizeof(notes));
    if (ret != ESP_OK) {
        return send_json_error(req, "502 Bad Gateway", "github_unreachable_or_no_asset");
    }

    char cur_ver[32];
    app_ota_get_current_version(cur_ver, sizeof(cur_ver));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddStringToObject(root, "current_version", cur_ver);
    cJSON_AddStringToObject(root, "latest_version", latest_version);
    cJSON_AddBoolToObject(root, "update_available", app_ota_version_is_newer(latest_version, cur_ver));
    cJSON_AddStringToObject(root, "asset_url", asset_url);
    cJSON_AddStringToObject(root, "notes", notes);
    return send_json(req, root);
}

static esp_err_t ota_install_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid JSON");
        return ESP_FAIL;
    }
    cJSON *url_item = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(url_item) || strlen(url_item->valuestring) == 0) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing url");
        return ESP_FAIL;
    }
    char url[256];
    strncpy(url, url_item->valuestring, sizeof(url) - 1);
    url[sizeof(url) - 1] = '\0';
    cJSON_Delete(root);

    ESP_LOGI(TAG, "Web UI requested install from %s", url);
    esp_err_t ret = app_ota_start_from_url(url);
    if (ret == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "update_in_progress");
    }
    if (ret != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "could_not_start");
    }
    return send_ok(req);
}

/* Raw .bin body (application/octet-stream), streamed straight into flash. */
static esp_err_t ota_upload_post_handler(httpd_req_t *req)
{
    if (req->content_len <= 0) {
        return send_json_error(req, "400 Bad Request", "empty_body");
    }

    esp_err_t ret = app_ota_upload_begin();
    if (ret == ESP_ERR_INVALID_STATE) {
        return send_json_error(req, "409 Conflict", "update_in_progress");
    }
    if (ret != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "could_not_start");
    }

    uint8_t *buf = malloc(OTA_UPLOAD_CHUNK);
    if (!buf) {
        app_ota_upload_abort();
        return send_json_error(req, "500 Internal Server Error", "out_of_memory");
    }

    int remaining = req->content_len;
    const char *fail_reason = NULL;

    while (remaining > 0) {
        int to_read = remaining < OTA_UPLOAD_CHUNK ? remaining : OTA_UPLOAD_CHUNK;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0) {
            fail_reason = "connection_error";
            break;
        }
        esp_err_t wret = app_ota_upload_write(buf, r);
        if (wret != ESP_OK) {
            fail_reason = (wret == ESP_ERR_INVALID_ARG) ? "not_a_wt32_image" : "flash_write_failed";
            break;
        }
        remaining -= r;
    }
    free(buf);

    if (fail_reason) {
        app_ota_upload_abort();
        return send_json_error(req, "400 Bad Request", fail_reason);
    }

    if (app_ota_upload_finish() != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "validation_or_finalize_failed");
    }

    esp_err_t send_ret = send_ok(req);
    ESP_LOGI(TAG, "Manual firmware upload accepted, restarting");
    xTaskCreate(delayed_restart_task, "restart", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    return send_ret;
}

/* ---------------------------------------------------------------------- */
/* /api/files/... routes - SD card file manager                          */
/* ---------------------------------------------------------------------- */

#define FILES_IO_CHUNK 2048

static const char *guess_content_type(const char *name)
{
    size_t len = strlen(name);
    if (len > 4 && strcasecmp(name + len - 4, ".jpg") == 0) return "image/jpeg";
    if (len > 5 && strcasecmp(name + len - 5, ".jpeg") == 0) return "image/jpeg";
    if (len > 4 && strcasecmp(name + len - 4, ".png") == 0) return "image/png";
    if (len > 4 && strcasecmp(name + len - 4, ".bmp") == 0) return "image/bmp";
    if (len > 4 && strcasecmp(name + len - 4, ".gif") == 0) return "image/gif";
    if (len > 4 && strcasecmp(name + len - 4, ".txt") == 0) return "text/plain";
    if (len > 5 && strcasecmp(name + len - 5, ".json") == 0) return "application/json";
    return "application/octet-stream";
}

static esp_err_t files_list_get_handler(httpd_req_t *req)
{
    char path[APP_FILES_PATH_MAX] = "/";
    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char tmp[APP_FILES_PATH_MAX];
        if (httpd_query_key_value(query, "path", tmp, sizeof(tmp)) == ESP_OK) {
            url_decode(tmp);
            strncpy(path, tmp, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
        }
    }

    static app_files_entry_t entries[128];
    size_t count = 0;
    if (app_files_list(path, entries, sizeof(entries) / sizeof(entries[0]), &count) != ESP_OK) {
        return send_json_error(req, "404 Not Found", "list_failed");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "path", path);
    cJSON *arr = cJSON_AddArrayToObject(root, "entries");
    for (size_t entry_i = 0; entry_i < count; entry_i++) {
        cJSON *entry_json = cJSON_CreateObject();
        cJSON_AddStringToObject(entry_json, "name", entries[entry_i].name);
        cJSON_AddBoolToObject(entry_json, "is_dir", entries[entry_i].is_dir);
        cJSON_AddNumberToObject(entry_json, "size", (double)entries[entry_i].size);
        cJSON_AddItemToArray(arr, entry_json);
    }
    return send_json(req, root);
}

static esp_err_t files_download_get_handler(httpd_req_t *req)
{
    char query[512];
    char path[APP_FILES_PATH_MAX];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "path", path, sizeof(path)) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "missing_path");
    }
    url_decode(path);

    char abs_path[APP_FILES_PATH_MAX];
    if (!app_files_resolve(path, abs_path, sizeof(abs_path))) {
        return send_json_error(req, "400 Bad Request", "invalid_path");
    }

    FILE *f = fopen(abs_path, "rb");
    if (!f) {
        return send_json_error(req, "404 Not Found", "not_found");
    }
    httpd_resp_set_type(req, guess_content_type(path));

    uint8_t *buf = malloc(FILES_IO_CHUNK);
    if (!buf) {
        fclose(f);
        return send_json_error(req, "500 Internal Server Error", "out_of_memory");
    }

    esp_err_t ret = ESP_OK;
    size_t r;
    while ((r = fread(buf, 1, FILES_IO_CHUNK, f)) > 0) {
        if (httpd_resp_send_chunk(req, (const char *)buf, r) != ESP_OK) {
            ret = ESP_FAIL;
            break;
        }
    }
    free(buf);
    fclose(f);
    if (ret == ESP_OK) {
        httpd_resp_send_chunk(req, NULL, 0); /* terminate the chunked response */
    }
    return ret;
}

/* Raw file body (any content-type) - target directory ("path") and
 * filename ("name", no "/" allowed) come from the query string. */
static esp_err_t files_upload_post_handler(httpd_req_t *req)
{
    char dir_path[APP_FILES_PATH_MAX] = "/";
    char name[APP_FILES_NAME_MAX] = {0};
    char query[512];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char tmp[APP_FILES_PATH_MAX];
        if (httpd_query_key_value(query, "path", tmp, sizeof(tmp)) == ESP_OK) {
            url_decode(tmp);
            strncpy(dir_path, tmp, sizeof(dir_path) - 1);
            dir_path[sizeof(dir_path) - 1] = '\0';
        }
        if (httpd_query_key_value(query, "name", name, sizeof(name)) == ESP_OK) {
            url_decode(name);
        }
    }

    if (name[0] == '\0' || strchr(name, '/') != NULL) {
        return send_json_error(req, "400 Bad Request", "invalid_name");
    }
    if (req->content_len <= 0) {
        return send_json_error(req, "400 Bad Request", "empty_body");
    }

    /* dir_path (APP_FILES_PATH_MAX) + "/" + name (APP_FILES_NAME_MAX) really
     * can exceed sizeof(rel_path) in the worst case - that's fine, it's
     * checked (and rejected) right below via the returned length, not
     * relied upon to always fit. */
    char rel_path[APP_FILES_PATH_MAX];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    int written = snprintf(rel_path, sizeof(rel_path), "%s/%s", dir_path, name);
#pragma GCC diagnostic pop
    if (written < 0 || (size_t)written >= sizeof(rel_path)) {
        return send_json_error(req, "400 Bad Request", "path_too_long");
    }

    char abs_path[APP_FILES_PATH_MAX];
    if (!app_files_resolve(rel_path, abs_path, sizeof(abs_path))) {
        return send_json_error(req, "400 Bad Request", "invalid_path");
    }

    FILE *f = fopen(abs_path, "wb");
    if (!f) {
        return send_json_error(req, "500 Internal Server Error", "could_not_create_file");
    }

    uint8_t *buf = malloc(FILES_IO_CHUNK);
    if (!buf) {
        fclose(f);
        remove(abs_path);
        return send_json_error(req, "500 Internal Server Error", "out_of_memory");
    }

    int remaining = req->content_len;
    bool failed = false;
    while (remaining > 0) {
        int to_read = remaining < FILES_IO_CHUNK ? remaining : FILES_IO_CHUNK;
        int r = httpd_req_recv(req, (char *)buf, to_read);
        if (r <= 0 || fwrite(buf, 1, r, f) != (size_t)r) {
            failed = true;
            break;
        }
        remaining -= r;
    }
    free(buf);
    fclose(f);

    if (failed) {
        remove(abs_path);
        return send_json_error(req, "400 Bad Request", "upload_failed");
    }

    ESP_LOGI(TAG, "Uploaded %s (%d bytes)", abs_path, (int)req->content_len);
    return send_ok(req);
}

static esp_err_t files_delete_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *path_item = root ? cJSON_GetObjectItem(root, "path") : NULL;
    if (!cJSON_IsString(path_item)) {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "missing_path");
    }
    char path[APP_FILES_PATH_MAX];
    strncpy(path, path_item->valuestring, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    cJSON_Delete(root);

    if (app_files_delete(path) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "delete_failed");
    }
    return send_ok(req);
}

static esp_err_t files_rename_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *from_item = root ? cJSON_GetObjectItem(root, "from") : NULL;
    cJSON *to_item = root ? cJSON_GetObjectItem(root, "to") : NULL;
    if (!cJSON_IsString(from_item) || !cJSON_IsString(to_item)) {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "missing_from_or_to");
    }
    char from[APP_FILES_PATH_MAX], to[APP_FILES_PATH_MAX];
    strncpy(from, from_item->valuestring, sizeof(from) - 1);
    from[sizeof(from) - 1] = '\0';
    strncpy(to, to_item->valuestring, sizeof(to) - 1);
    to[sizeof(to) - 1] = '\0';
    cJSON_Delete(root);

    if (app_files_rename(from, to) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "rename_failed");
    }
    return send_ok(req);
}

static esp_err_t files_mkdir_post_handler(httpd_req_t *req)
{
    char *body = read_body(req);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing/oversized body");
        return ESP_FAIL;
    }
    cJSON *root = cJSON_Parse(body);
    free(body);
    cJSON *path_item = root ? cJSON_GetObjectItem(root, "path") : NULL;
    if (!cJSON_IsString(path_item)) {
        cJSON_Delete(root);
        return send_json_error(req, "400 Bad Request", "missing_path");
    }
    char path[APP_FILES_PATH_MAX];
    strncpy(path, path_item->valuestring, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';
    cJSON_Delete(root);

    if (app_files_mkdir(path) != ESP_OK) {
        return send_json_error(req, "400 Bad Request", "mkdir_failed");
    }
    return send_ok(req);
}

/* ---------------------------------------------------------------------- */
/* /api/sdcard/... routes                                                 */
/* ---------------------------------------------------------------------- */

static esp_err_t sdcard_format_post_handler(httpd_req_t *req)
{
    if (!bsp_sdcard_is_mounted()) {
        return send_json_error(req, "400 Bad Request", "not_mounted");
    }
    if (bsp_sdcard_format() != ESP_OK) {
        return send_json_error(req, "500 Internal Server Error", "format_failed");
    }
    /* The format just wiped /photos along with everything else - recreate it
     * (best-effort: a failure here just means the next photo upload has to
     * create the folder itself) and reset the in-memory album list, which
     * otherwise still points at files that no longer exist. */
    app_files_mkdir("/photos");
    app_photo_scan(BSP_SD_MOUNT_POINT "/photos");
    return send_ok(req);
}

/* ---------------------------------------------------------------------- */
/* Captive portal: redirect anything unrecognized back to "/"              */
/* ---------------------------------------------------------------------- */

static esp_err_t captive_redirect_handler(httpd_req_t *req, httpd_err_code_t err)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */

esp_err_t app_web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 28; /* routes[] below is at 24 - keep a few spare */
    config.lru_purge_enable = true;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.stack_size = 8192; /* OTA writes + JSON parsing want a bit more than the 4KB default */

    httpd_handle_t server = NULL;
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    static const httpd_uri_t routes[] = {
        {.uri = "/",                          .method = HTTP_GET,  .handler = index_get_handler},
        {.uri = "/style.css",                 .method = HTTP_GET,  .handler = style_get_handler},
        {.uri = "/app.js",                    .method = HTTP_GET,  .handler = script_get_handler},
        {.uri = "/favicon.ico",               .method = HTTP_GET,  .handler = favicon_get_handler},
        {.uri = "/api/status",                .method = HTTP_GET,  .handler = status_get_handler},
        {.uri = "/api/config",                .method = HTTP_GET,  .handler = config_get_handler},
        {.uri = "/api/config",                .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/api/weather",               .method = HTTP_GET,  .handler = weather_get_handler},
        {.uri = "/api/wifi/scan",             .method = HTTP_GET,  .handler = wifi_scan_get_handler},
        {.uri = "/api/wifi/connect",          .method = HTTP_POST, .handler = wifi_connect_post_handler},
        {.uri = "/api/system/restart",        .method = HTTP_POST, .handler = restart_post_handler},
        {.uri = "/api/system/factory_reset",  .method = HTTP_POST, .handler = factory_reset_post_handler},
        {.uri = "/api/system/ap_password",    .method = HTTP_POST, .handler = ap_password_post_handler},
        {.uri = "/api/ota/status",            .method = HTTP_GET,  .handler = ota_status_get_handler},
        {.uri = "/api/ota/check",             .method = HTTP_GET,  .handler = ota_check_get_handler},
        {.uri = "/api/ota/install",           .method = HTTP_POST, .handler = ota_install_post_handler},
        {.uri = "/api/ota/upload",            .method = HTTP_POST, .handler = ota_upload_post_handler},
        {.uri = "/api/files/list",            .method = HTTP_GET,  .handler = files_list_get_handler},
        {.uri = "/api/files/download",        .method = HTTP_GET,  .handler = files_download_get_handler},
        {.uri = "/api/files/upload",          .method = HTTP_POST, .handler = files_upload_post_handler},
        {.uri = "/api/files/delete",          .method = HTTP_POST, .handler = files_delete_post_handler},
        {.uri = "/api/files/rename",          .method = HTTP_POST, .handler = files_rename_post_handler},
        {.uri = "/api/files/mkdir",           .method = HTTP_POST, .handler = files_mkdir_post_handler},
        {.uri = "/api/sdcard/format",         .method = HTTP_POST, .handler = sdcard_format_post_handler},
    };
    for (size_t route_i = 0; route_i < sizeof(routes) / sizeof(routes[0]); route_i++) {
        httpd_register_uri_handler(server, &routes[route_i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_redirect_handler);

    ESP_LOGI(TAG, "Web UI + REST API started");
    return ESP_OK;
}
