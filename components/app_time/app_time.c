#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_netif_sntp.h"

#include "app_config.h"
#include "app_time.h"

static const char *TAG = "app_time";
static bool s_started = false;
static volatile bool s_synced = false;

/*
 * sntp_get_sync_status() (used by lwip's SNTP client) clears its internal
 * flag back to "reset" the moment it's read once it reports COMPLETED, so
 * polling it from a UI timer only sees "synced" on the one poll right after
 * a sync and reports "not synced" again afterwards - even though the clock
 * is still correct. Latch our own flag from the sync callback instead: it is
 * invoked on the initial sync and every periodic resync, and stays true.
 */
static void time_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_synced = true;
    ESP_LOGI(TAG, "SNTP time synced");
}

esp_err_t app_time_start(void)
{
    app_config_t *cfg = app_config_get();
    app_time_set_timezone(cfg->tz_posix);

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(cfg->ntp_server);
    config.start = true;
    config.sync_cb = time_sync_cb;
    esp_err_t ret = esp_netif_sntp_init(&config);
    if (ret == ESP_OK) {
        s_started = true;
    }
    return ret;
}

void app_time_set_timezone(const char *tz_posix)
{
    if (tz_posix && tz_posix[0]) {
        setenv("TZ", tz_posix, 1);
        tzset();
    }
}

void app_time_set_server(const char *ntp_server)
{
    if (!ntp_server || !ntp_server[0]) {
        return;
    }
    if (s_started) {
        esp_netif_sntp_deinit();
    }
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(ntp_server);
    config.start = true;
    config.sync_cb = time_sync_cb;
    if (esp_netif_sntp_init(&config) == ESP_OK) {
        s_started = true;
    } else {
        ESP_LOGW(TAG, "Failed to restart SNTP against %s", ntp_server);
    }
}

bool app_time_is_synced(void)
{
    return s_synced;
}

void app_time_get_local(struct tm *out)
{
    time_t now;
    time(&now);
    localtime_r(&now, out);
}
