/**
 * @file app_weather.c
 * @brief See app_weather.h. HTTP/JSON pattern mirrors app_ota's GitHub
 *        "latest release" lookup (esp_http_client + crt bundle + cJSON).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "app_weather.h"
#include "app_config.h"
#include "app_wifi.h"

static const char *TAG = "app_weather";

#define WEATHER_JSON_MAX_LEN 2048
#define POLL_TICK_MS         (5 * 1000)         /* how often the task wakes to check state */
#define FETCH_INTERVAL_MS    (10 * 60 * 1000)   /* how often to actually hit the API */

static SemaphoreHandle_t s_lock;
static app_weather_data_t s_data;
static volatile bool s_force_refresh = false;

static void set_error(const char *msg)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data.have_error = true;
    strncpy(s_data.error, msg, sizeof(s_data.error) - 1);
    s_data.error[sizeof(s_data.error) - 1] = '\0';
    xSemaphoreGive(s_lock);
}

/* Fetch current conditions for `city_id` into a fresh app_weather_data_t and
 * publish it under s_lock. Leaves the previous reading in place (just flags
 * the error) on any failure, so the screen shows stale-but-valid data
 * instead of blanking on a transient network problem. */
static void do_fetch(const char *api_key, const char *city_id)
{
    char url[224];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather?id=%s&appid=%s&units=metric",
             city_id, api_key);

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        set_error("http_init_failed");
        return;
    }

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        set_error("connect_failed");
        return;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);

    char *body = malloc(WEATHER_JSON_MAX_LEN);
    if (!body) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        set_error("out_of_memory");
        return;
    }
    int total = 0;
    while (total < WEATHER_JSON_MAX_LEN - 1) {
        int r = esp_http_client_read(client, body + total, WEATHER_JSON_MAX_LEN - 1 - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }
    body[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (status != 200) {
        ESP_LOGW(TAG, "OpenWeatherMap returned HTTP %d: %s", status, body);
        free(body);
        set_error(status == 401 ? "invalid_api_key" : (status == 404 ? "city_not_found" : "http_error"));
        return;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        set_error("invalid_json");
        return;
    }

    cJSON *weather0 = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "weather"), 0);
    cJSON *desc = weather0 ? cJSON_GetObjectItem(weather0, "description") : NULL;
    cJSON *cond_id = weather0 ? cJSON_GetObjectItem(weather0, "id") : NULL;
    cJSON *cond_icon = weather0 ? cJSON_GetObjectItem(weather0, "icon") : NULL;
    cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    cJSON *wind_obj = cJSON_GetObjectItem(root, "wind");
    cJSON *name = cJSON_GetObjectItem(root, "name");

    if (!cJSON_IsObject(main_obj)) {
        cJSON_Delete(root);
        set_error("unexpected_response");
        return;
    }

    app_weather_data_t fresh = {0};
    fresh.valid = true;
    fresh.have_error = false;
    if (cJSON_IsString(desc)) {
        strncpy(fresh.description, desc->valuestring, sizeof(fresh.description) - 1);
    }
    if (cJSON_IsNumber(cond_id)) {
        fresh.condition_id = cond_id->valueint;
    }
    /* icon codes look like "01d"/"01n" - the trailing letter is day/night.
     * Default to day if the field's missing or shaped unexpectedly. */
    fresh.is_day = true;
    if (cJSON_IsString(cond_icon) && cond_icon->valuestring[0]) {
        size_t len = strlen(cond_icon->valuestring);
        fresh.is_day = cond_icon->valuestring[len - 1] != 'n';
    }
    if (cJSON_IsString(name)) {
        strncpy(fresh.city_name, name->valuestring, sizeof(fresh.city_name) - 1);
    }
    cJSON *item;
    if ((item = cJSON_GetObjectItem(main_obj, "temp")) && cJSON_IsNumber(item)) {
        fresh.temp_c = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(main_obj, "feels_like")) && cJSON_IsNumber(item)) {
        fresh.feels_like_c = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(main_obj, "temp_min")) && cJSON_IsNumber(item)) {
        fresh.temp_min_c = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(main_obj, "temp_max")) && cJSON_IsNumber(item)) {
        fresh.temp_max_c = (float)item->valuedouble;
    }
    if ((item = cJSON_GetObjectItem(main_obj, "humidity")) && cJSON_IsNumber(item)) {
        fresh.humidity_pct = item->valueint;
    }
    if (cJSON_IsObject(wind_obj) && (item = cJSON_GetObjectItem(wind_obj, "speed")) && cJSON_IsNumber(item)) {
        fresh.wind_speed_ms = (float)item->valuedouble;
    }
    cJSON_Delete(root);

    fresh.updated_at = time(NULL);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_data = fresh;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "Weather updated: %s, %.1f\xC2\xB0" "C, %s", fresh.city_name, fresh.temp_c, fresh.description);
}

static void weather_task(void *arg)
{
    (void)arg;
    /* Negative enough that the very first eligible tick fetches immediately. */
    TickType_t last_fetch = (TickType_t)0 - pdMS_TO_TICKS(FETCH_INTERVAL_MS);

    for (;;) {
        app_config_t *cfg = app_config_get();
        bool configured = cfg->weather_enabled && cfg->weather_api_key[0] && cfg->weather_city_id[0];
        TickType_t now = xTaskGetTickCount();
        bool due = (now - last_fetch) >= pdMS_TO_TICKS(FETCH_INTERVAL_MS);

        if (configured && (due || s_force_refresh) && app_wifi_is_connected()) {
            s_force_refresh = false;
            last_fetch = now;
            /* Snapshot the two strings before the blocking HTTP call in case
             * the web UI rewrites app_config_get()'s buffers concurrently. */
            char api_key[APP_CFG_STR_MAX_LEN];
            char city_id[APP_CFG_STR_MAX_LEN];
            strncpy(api_key, cfg->weather_api_key, sizeof(api_key) - 1);
            api_key[sizeof(api_key) - 1] = '\0';
            strncpy(city_id, cfg->weather_city_id, sizeof(city_id) - 1);
            city_id[sizeof(city_id) - 1] = '\0';
            do_fetch(api_key, city_id);
        } else if (!configured) {
            s_force_refresh = false;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_TICK_MS));
    }
}

esp_err_t app_weather_start(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_data, 0, sizeof(s_data));

    if (xTaskCreate(weather_task, "weather", 6144, NULL, tskIDLE_PRIORITY + 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create weather task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void app_weather_get(app_weather_data_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_data;
    xSemaphoreGive(s_lock);
}

void app_weather_request_refresh(void)
{
    s_force_refresh = true;
}
