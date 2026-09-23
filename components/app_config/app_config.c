#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "app_config.h"

static const char *TAG = "app_config";
static const char *NVS_NAMESPACE = "wt32cfg";

/* One NVS key per field (<=15 chars, the NVS limit). Adding a field to
 * app_config_t just means adding its key here plus a load_xxx()/save call
 * below - a device that hasn't seen the new key yet simply gets the
 * default from app_config_reset_defaults(), every other stored setting is
 * untouched. See the doc comment in app_config.h. */
#define KEY_WIFI_SSID    "wifi_ssid"
#define KEY_WIFI_PASS    "wifi_pass"
#define KEY_HOSTNAME     "hostname"
#define KEY_AP_PASS      "ap_pass"
#define KEY_TZ_POSIX     "tz_posix"
#define KEY_NTP_SERVER   "ntp_server"
#define KEY_TIME_24H     "time_24h"
#define KEY_CLOCK_FACE   "clock_face"
#define KEY_CHIME_EN     "chime_en"
#define KEY_BRIGHTNESS   "bright"
#define KEY_AUTO_CYCLE   "auto_cyc"
#define KEY_CYCLE_SEC    "cyc_sec"
#define KEY_THEME        "theme"
#define KEY_THEME_DAY    "theme_day"
#define KEY_THEME_NIGHT  "theme_night"
#define KEY_ALB_INT      "alb_int"
#define KEY_ALB_SHUFFLE  "alb_shuf"
#define KEY_FLICKR_FEEDS "flickr_feeds"
#define KEY_WTHR_EN      "wthr_en"
#define KEY_WTHR_KEY     "wthr_key"
#define KEY_WTHR_CITY    "wthr_city"
#define KEY_AUD_MUTED    "aud_mute"
#define KEY_GH_REPO      "gh_repo"
#define KEY_FIRST_BOOT   "first_boot"
#define KEY_LOG_LEVEL    "log_level"

static app_config_t s_cfg;

void app_config_reset_defaults(app_config_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));

    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_password[0] = '\0';
    cfg->ap_password[0] = '\0'; /* open SoftAP by default - see app_config.h */
    strncpy(cfg->hostname, "wt32", sizeof(cfg->hostname) - 1);

    strncpy(cfg->tz_posix, "UTC0", sizeof(cfg->tz_posix) - 1);
    strncpy(cfg->ntp_server, "pool.ntp.org", sizeof(cfg->ntp_server) - 1);
    cfg->time_24h = true;
    cfg->clock_face = CLOCK_FACE_DIGITAL;
    cfg->chime_enabled = false;

    cfg->brightness = 80;
    cfg->auto_cycle_enabled = true;
    cfg->cycle_seconds = 10;
    cfg->display_theme = DISPLAY_THEME_DARK; /* matches the UI's original always-dark look */
    strncpy(cfg->theme_day_start, "07:00", sizeof(cfg->theme_day_start) - 1);
    strncpy(cfg->theme_night_start, "20:00", sizeof(cfg->theme_night_start) - 1);

    cfg->album_interval_s = 8;
    cfg->album_shuffle = false;
    cfg->flickr_feeds[0] = '\0';

    cfg->weather_enabled = false;
    cfg->weather_api_key[0] = '\0';
    cfg->weather_city_id[0] = '\0';

    cfg->audio_muted = false;

    cfg->github_repo[0] = '\0'; /* e.g. "yourname/wt32" - set from the web UI's Firmware tab */

    cfg->first_boot_done = false;

    cfg->log_level = APP_LOG_LEVEL_INFO; /* matches sdkconfig.defaults' CONFIG_LOG_DEFAULT_LEVEL_INFO */
}

/* --- small per-type helpers: leave *out untouched (i.e. keep whatever
 * default app_config_reset_defaults() already put there) if the key isn't
 * present in NVS yet, so partial/older stored configs load fine. --- */

static void load_str(nvs_handle_t h, const char *key, char *out, size_t out_len)
{
    size_t len = out_len;
    esp_err_t ret = nvs_get_str(h, key, out, &len);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_str(%s) failed: %s", key, esp_err_to_name(ret));
    }
}

static void load_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    esp_err_t ret = nvs_get_u8(h, key, out);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_u8(%s) failed: %s", key, esp_err_to_name(ret));
    }
}

static void load_bool(nvs_handle_t h, const char *key, bool *out)
{
    uint8_t v = *out ? 1 : 0;
    load_u8(h, key, &v);
    *out = (v != 0);
}

static void load_u16(nvs_handle_t h, const char *key, uint16_t *out)
{
    esp_err_t ret = nvs_get_u16(h, key, out);
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "get_u16(%s) failed: %s", key, esp_err_to_name(ret));
    }
}

esp_err_t app_config_load(void)
{
    app_config_reset_defaults(&s_cfg);

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "No stored config found, using defaults (%s)", esp_err_to_name(ret));
        return ESP_OK;
    }

    load_str(handle, KEY_WIFI_SSID, s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid));
    load_str(handle, KEY_WIFI_PASS, s_cfg.wifi_password, sizeof(s_cfg.wifi_password));
    load_str(handle, KEY_HOSTNAME, s_cfg.hostname, sizeof(s_cfg.hostname));
    load_str(handle, KEY_AP_PASS, s_cfg.ap_password, sizeof(s_cfg.ap_password));

    load_str(handle, KEY_TZ_POSIX, s_cfg.tz_posix, sizeof(s_cfg.tz_posix));
    load_str(handle, KEY_NTP_SERVER, s_cfg.ntp_server, sizeof(s_cfg.ntp_server));
    load_bool(handle, KEY_TIME_24H, &s_cfg.time_24h);
    {
        uint8_t face = (uint8_t)s_cfg.clock_face;
        load_u8(handle, KEY_CLOCK_FACE, &face);
        s_cfg.clock_face = (clock_face_t)face;
    }
    load_bool(handle, KEY_CHIME_EN, &s_cfg.chime_enabled);

    load_u8(handle, KEY_BRIGHTNESS, &s_cfg.brightness);
    load_bool(handle, KEY_AUTO_CYCLE, &s_cfg.auto_cycle_enabled);
    load_u16(handle, KEY_CYCLE_SEC, &s_cfg.cycle_seconds);
    {
        uint8_t theme = (uint8_t)s_cfg.display_theme;
        load_u8(handle, KEY_THEME, &theme);
        s_cfg.display_theme = (display_theme_t)theme;
    }
    load_str(handle, KEY_THEME_DAY, s_cfg.theme_day_start, sizeof(s_cfg.theme_day_start));
    load_str(handle, KEY_THEME_NIGHT, s_cfg.theme_night_start, sizeof(s_cfg.theme_night_start));

    load_u16(handle, KEY_ALB_INT, &s_cfg.album_interval_s);
    load_bool(handle, KEY_ALB_SHUFFLE, &s_cfg.album_shuffle);
    load_str(handle, KEY_FLICKR_FEEDS, s_cfg.flickr_feeds, sizeof(s_cfg.flickr_feeds));

    load_bool(handle, KEY_WTHR_EN, &s_cfg.weather_enabled);
    load_str(handle, KEY_WTHR_KEY, s_cfg.weather_api_key, sizeof(s_cfg.weather_api_key));
    load_str(handle, KEY_WTHR_CITY, s_cfg.weather_city_id, sizeof(s_cfg.weather_city_id));

    load_bool(handle, KEY_AUD_MUTED, &s_cfg.audio_muted);

    load_str(handle, KEY_GH_REPO, s_cfg.github_repo, sizeof(s_cfg.github_repo));

    load_bool(handle, KEY_FIRST_BOOT, &s_cfg.first_boot_done);
    {
        uint8_t level = (uint8_t)s_cfg.log_level;
        load_u8(handle, KEY_LOG_LEVEL, &level);
        s_cfg.log_level = (app_log_level_t)level;
    }

    nvs_close(handle);
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

    /* Each nvs_set_* is its own atomic write; only the final nvs_commit()
     * flushes them to flash. Keep going on a single field's failure so one
     * bad key doesn't stop the rest of the config from being saved, but
     * remember it happened so we can report a non-OK overall result. */
    esp_err_t first_err = ESP_OK;
#define CHECK(expr) do { esp_err_t _e = (expr); if (_e != ESP_OK && first_err == ESP_OK) first_err = _e; } while (0)

    CHECK(nvs_set_str(handle, KEY_WIFI_SSID, s_cfg.wifi_ssid));
    CHECK(nvs_set_str(handle, KEY_WIFI_PASS, s_cfg.wifi_password));
    CHECK(nvs_set_str(handle, KEY_HOSTNAME, s_cfg.hostname));
    CHECK(nvs_set_str(handle, KEY_AP_PASS, s_cfg.ap_password));

    CHECK(nvs_set_str(handle, KEY_TZ_POSIX, s_cfg.tz_posix));
    CHECK(nvs_set_str(handle, KEY_NTP_SERVER, s_cfg.ntp_server));
    CHECK(nvs_set_u8(handle, KEY_TIME_24H, s_cfg.time_24h ? 1 : 0));
    CHECK(nvs_set_u8(handle, KEY_CLOCK_FACE, (uint8_t)s_cfg.clock_face));
    CHECK(nvs_set_u8(handle, KEY_CHIME_EN, s_cfg.chime_enabled ? 1 : 0));

    CHECK(nvs_set_u8(handle, KEY_BRIGHTNESS, s_cfg.brightness));
    CHECK(nvs_set_u8(handle, KEY_AUTO_CYCLE, s_cfg.auto_cycle_enabled ? 1 : 0));
    CHECK(nvs_set_u16(handle, KEY_CYCLE_SEC, s_cfg.cycle_seconds));
    CHECK(nvs_set_u8(handle, KEY_THEME, (uint8_t)s_cfg.display_theme));
    CHECK(nvs_set_str(handle, KEY_THEME_DAY, s_cfg.theme_day_start));
    CHECK(nvs_set_str(handle, KEY_THEME_NIGHT, s_cfg.theme_night_start));

    CHECK(nvs_set_u16(handle, KEY_ALB_INT, s_cfg.album_interval_s));
    CHECK(nvs_set_u8(handle, KEY_ALB_SHUFFLE, s_cfg.album_shuffle ? 1 : 0));
    CHECK(nvs_set_str(handle, KEY_FLICKR_FEEDS, s_cfg.flickr_feeds));

    CHECK(nvs_set_u8(handle, KEY_WTHR_EN, s_cfg.weather_enabled ? 1 : 0));
    CHECK(nvs_set_str(handle, KEY_WTHR_KEY, s_cfg.weather_api_key));
    CHECK(nvs_set_str(handle, KEY_WTHR_CITY, s_cfg.weather_city_id));

    CHECK(nvs_set_u8(handle, KEY_AUD_MUTED, s_cfg.audio_muted ? 1 : 0));

    CHECK(nvs_set_str(handle, KEY_GH_REPO, s_cfg.github_repo));

    CHECK(nvs_set_u8(handle, KEY_FIRST_BOOT, s_cfg.first_boot_done ? 1 : 0));
    CHECK(nvs_set_u8(handle, KEY_LOG_LEVEL, (uint8_t)s_cfg.log_level));
#undef CHECK

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (first_err != ESP_OK) {
        ESP_LOGE(TAG, "One or more config fields failed to save: %s", esp_err_to_name(first_err));
        return first_err;
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit config: %s", esp_err_to_name(ret));
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
