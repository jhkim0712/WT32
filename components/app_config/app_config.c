#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"

static const char *TAG = "app_config";
static const char *NVS_NAMESPACE = "wt32cfg";
static const char *NVS_KEY = "cfg";

static app_config_t s_cfg;

void app_config_reset_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->version = APP_CONFIG_VERSION;

    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_password[0] = '\0';
    strncpy(cfg->hostname, "wt32-smalltv", sizeof(cfg->hostname) - 1);

    strncpy(cfg->tz_posix, "UTC0", sizeof(cfg->tz_posix) - 1);
    strncpy(cfg->ntp_server, "pool.ntp.org", sizeof(cfg->ntp_server) - 1);
    cfg->time_24h = true;
    cfg->clock_face = CLOCK_FACE_DIGITAL;
    cfg->chime_enabled = false;

    cfg->brightness = 80;
    cfg->auto_cycle_enabled = true;
    cfg->cycle_seconds = 10;

    cfg->album_interval_s = 8;
    cfg->album_shuffle = false;

    cfg->audio_muted = false;

    cfg->first_boot_done = false;
}

esp_err_t app_config_load(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No stored config found, using defaults (%s)", esp_err_to_name(ret));
        app_config_reset_defaults(&s_cfg);
        return ESP_OK;
    }

    app_config_t loaded;
    size_t len = sizeof(loaded);
    ret = nvs_get_blob(handle, NVS_KEY, &loaded, &len);
    nvs_close(handle);

    if (ret != ESP_OK || len != sizeof(loaded) || loaded.version != APP_CONFIG_VERSION) {
        ESP_LOGW(TAG, "Stored config missing/incompatible, resetting to defaults");
        app_config_reset_defaults(&s_cfg);
        return ESP_OK;
    }

    s_cfg = loaded;
    ESP_LOGI(TAG, "Config loaded from NVS (host=%s)", s_cfg.hostname);
    return ESP_OK;
}

esp_err_t app_config_save(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY, &s_cfg, sizeof(s_cfg));
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save config: %s", esp_err_to_name(ret));
    }
    return ret;
}

app_config_t *app_config_get(void)
{
    return &s_cfg;
}

esp_err_t app_config_erase(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = nvs_erase_all(handle);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }
    nvs_close(handle);
    return ret;
}
