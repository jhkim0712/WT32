/**
 * @file app_web.c
 * @brief REST API + embedded HTML5/CSS/JS configuration page.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
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
#include "app_ota.h"
#include "bsp/bsp_board.h"

static const char *TAG = "app_web";

#define MAX_BODY_LEN 4096
#define OTA_UPLOAD_CHUNK 2048

/* Embedded web assets - symbol names are derived from the EMBED_FILES path
 * in CMakeLists.txt ("webapp/index.html" -> _binary_webapp_index_html_*). */
extern const uint8_t webapp_index_html_start[] asm("_binary_webapp_index_html_start");
extern const uint8_t webapp_index_html_end[]   asm("_binary_webapp_index_html_end");
extern const uint8_t webapp_style_css_start[]  asm("_binary_webapp_style_css_start");
extern const uint8_t webapp_style_css_end[]    asm("_binary_webapp_style_css_end");
extern const uint8_t webapp_app_js_start[]     asm("_binary_webapp_app_js_start");
extern const uint8_t webapp_app_js_end[]       asm("_binary_webapp_app_js_end");

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

static void delayed_restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
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
    cJSON_AddNumberToObject(root, "album_interval_s", cfg->album_interval_s);
    cJSON_AddBoolToObject(root, "album_shuffle", cfg->album_shuffle);
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
/* /api/wifi/*                                                             */
/* ---------------------------------------------------------------------- */

static esp_err_t wifi_scan_get_handler(httpd_req_t *req)
{
    static app_wifi_ap_info_t list[20];
    size_t n = app_wifi_scan(list, sizeof(list) / sizeof(list[0]));

    cJSON *arr = cJSON_CreateArray();
    for (size_t i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "ssid", list[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", list[i].rssi);
        cJSON_AddBoolToObject(o, "secure", list[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, o);
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
/* /api/system/*                                                          */
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

/* ---------------------------------------------------------------------- */
/* /api/ota/*  - firmware updates                                        */
/* ---------------------------------------------------------------------- */

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
            fail_reason = (wret == ESP_ERR_INVALID_ARG) ? "not_a_wt32_smalltv_image" : "flash_write_failed";
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
    config.max_uri_handlers = 20;
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
        {.uri = "/api/status",                .method = HTTP_GET,  .handler = status_get_handler},
        {.uri = "/api/config",                .method = HTTP_GET,  .handler = config_get_handler},
        {.uri = "/api/config",                .method = HTTP_POST, .handler = config_post_handler},
        {.uri = "/api/wifi/scan",             .method = HTTP_GET,  .handler = wifi_scan_get_handler},
        {.uri = "/api/wifi/connect",          .method = HTTP_POST, .handler = wifi_connect_post_handler},
        {.uri = "/api/system/restart",        .method = HTTP_POST, .handler = restart_post_handler},
        {.uri = "/api/system/factory_reset",  .method = HTTP_POST, .handler = factory_reset_post_handler},
        {.uri = "/api/ota/status",            .method = HTTP_GET,  .handler = ota_status_get_handler},
        {.uri = "/api/ota/check",             .method = HTTP_GET,  .handler = ota_check_get_handler},
        {.uri = "/api/ota/install",           .method = HTTP_POST, .handler = ota_install_post_handler},
        {.uri = "/api/ota/upload",            .method = HTTP_POST, .handler = ota_upload_post_handler},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, captive_redirect_handler);

    ESP_LOGI(TAG, "Web UI + REST API started");
    return ESP_OK;
}
