/**
 * @file app_wifi.c
 * @brief Always-on SoftAP combined with a best-effort STA connection to the
 *        network stored in app_config.
 *
 * The device runs WIFI_MODE_APSTA at all times: the AP radio guarantees the
 * web configurator (app_web) is reachable even when no home network is
 * configured yet, or the configured one is unreachable; the STA radio joins
 * the configured network in the background and keeps retrying forever on
 * disconnect, so the device recovers automatically once the router is back.
 *
 * The captive-portal DNS hijack (dns_server.c), however, only runs while
 * *not* connected to STA - it's what makes phones/PCs auto-pop the "sign in
 * to network" browser the moment they join the AP, which is exactly what we
 * want during first-time setup but becomes an unwanted recurring nag (e.g.
 * Windows repeatedly opening a browser) for anyone who joins the AP again
 * later just to reach the device, while it's already on the home network.
 * The AP keeps broadcasting either way - http://192.168.4.1/ always works
 * manually - only the automatic captive-portal popup is conditional.
 */
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "mdns.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "app_config.h"
#include "app_wifi.h"
#include "dns_server.h"

static const char *TAG = "app_wifi";

#define WIFI_CONNECTED_BIT BIT0

#define AP_CHANNEL          1
#define AP_MAX_CONN         4

static EventGroupHandle_t s_wifi_event_group;
static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static volatile bool s_connected = false;
static bool s_have_creds = false;
static char s_ap_ssid[33] = {0};
static uint32_t s_ap_ip_addr = 0; /* cached at startup, for re-arming dns_server_start() */

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (s_have_creds) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        dns_server_start(s_ap_ip_addr); /* re-arm the captive portal - no-op if already running */
        if (s_have_creds) {
            /* Keep trying forever - the router may come back later. */
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *evt = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        s_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        /* Stop hijacking DNS on the AP now that we're on a real network -
         * otherwise anyone who reconnects to the fallback AP later (e.g. to
         * reach the device without knowing its home-network IP) gets an
         * unwanted "sign in to network" popup every time. The AP itself
         * keeps broadcasting regardless - see the file-level comment. */
        dns_server_stop();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "A client joined the SoftAP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "A client left the SoftAP");
    }
}

static void build_ap_ssid(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    /* "WT32-" + last 3 octets of the AP MAC address, e.g. "WT32-A1B2C3". */
    snprintf(s_ap_ssid, sizeof(s_ap_ssid), "WT32-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

/** Fill in the SoftAP wifi_config_t from app_config's ap_password: an empty
 *  password (the default) means an open network; a non-empty one switches
 *  to WPA2-PSK. esp_wifi rejects WPA2 passwords shorter than 8 characters,
 *  so callers must reject those before getting here (app_web validates the
 *  web UI's input; app_wifi_start()'s stored-config path can't produce an
 *  invalid one because app_web already validated it before saving). */
static void configure_ap(wifi_config_t *ap_config)
{
    memset(ap_config, 0, sizeof(*ap_config));
    ap_config->ap.channel = AP_CHANNEL;
    ap_config->ap.max_connection = AP_MAX_CONN;
    strncpy((char *)ap_config->ap.ssid, s_ap_ssid, sizeof(ap_config->ap.ssid));
    ap_config->ap.ssid_len = strlen(s_ap_ssid);

    app_config_t *app_cfg = app_config_get();
    if (app_cfg->ap_password[0]) {
        ap_config->ap.authmode = WIFI_AUTH_WPA2_PSK;
        strncpy((char *)ap_config->ap.password, app_cfg->ap_password, sizeof(ap_config->ap.password) - 1);
    } else {
        ap_config->ap.authmode = WIFI_AUTH_OPEN;
    }
}

esp_err_t app_wifi_start(void)
{
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    s_wifi_event_group = xEventGroupCreate();
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    build_ap_ssid();

    app_config_t *app_cfg = app_config_get();
    s_have_creds = (app_cfg->wifi_ssid[0] != '\0');

    wifi_config_t ap_config;
    configure_ap(&ap_config);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));

    if (s_have_creds) {
        wifi_config_t sta_config = {0};
        strncpy((char *)sta_config.sta.ssid, app_cfg->wifi_ssid, sizeof(sta_config.sta.ssid));
        strncpy((char *)sta_config.sta.password, app_cfg->wifi_password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    }

    if (app_cfg->hostname[0]) {
        esp_netif_set_hostname(s_sta_netif, app_cfg->hostname);
        esp_netif_set_hostname(s_ap_netif, app_cfg->hostname);
    }

    if (mdns_init() == ESP_OK) {
        mdns_hostname_set(app_cfg->hostname[0] ? app_cfg->hostname : "wt32");
        mdns_instance_name_set("WT32");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    } else {
        ESP_LOGW(TAG, "mdns_init failed - the device will only be reachable by IP address");
    }

    /* Read the AP's (static, self-assigned) IP and arm the captive-portal
     * DNS server *before* esp_wifi_start() - not after - so there's no
     * window where a fast STA connection's IP_EVENT_STA_GOT_IP (which calls
     * dns_server_stop()) could race a not-yet-issued dns_server_start()
     * call and leave the hijack running despite already being connected. */
    esp_netif_ip_info_t ap_ip;
    esp_netif_get_ip_info(s_ap_netif, &ap_ip);
    s_ap_ip_addr = ap_ip.ip.addr;
    dns_server_start(s_ap_ip_addr);

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "SoftAP \"%s\" (%s) always available at " IPSTR,
             s_ap_ssid, ap_config.ap.authmode == WIFI_AUTH_OPEN ? "open, no password" : "password-protected",
             IP2STR(&ap_ip.ip));

    if (s_have_creds) {
        ESP_LOGI(TAG, "Attempting to join \"%s\"...", app_cfg->wifi_ssid);
        xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));
        if (!s_connected) {
            ESP_LOGW(TAG, "Could not join \"%s\" within 15s - will keep retrying in the background",
                     app_cfg->wifi_ssid);
        }
    } else {
        ESP_LOGI(TAG, "No Wi-Fi configured yet - connect to the SoftAP above to set it up");
    }

    return ESP_OK;
}

app_wifi_mode_t app_wifi_get_mode(void)
{
    return s_connected ? APP_WIFI_MODE_STA : APP_WIFI_MODE_AP;
}

bool app_wifi_is_connected(void)
{
    return s_connected;
}

esp_err_t app_wifi_get_ip_str(char *buf, size_t buf_len)
{
    esp_netif_t *netif = s_connected ? s_sta_netif : s_ap_netif;
    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }
    snprintf(buf, buf_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

void app_wifi_get_ap_ssid(char *buf, size_t buf_len)
{
    strncpy(buf, s_ap_ssid, buf_len - 1);
    buf[buf_len - 1] = '\0';
}

esp_err_t app_wifi_set_ap_password(const char *password)
{
    if (password && password[0] && strlen(password) < 8) {
        /* esp_wifi rejects this outright for WPA2-PSK; fail early with a
         * clearer error than whatever esp_wifi_set_config() would return. */
        return ESP_ERR_INVALID_ARG;
    }

    app_config_t *app_cfg = app_config_get();
    strncpy(app_cfg->ap_password, password ? password : "", sizeof(app_cfg->ap_password) - 1);
    app_cfg->ap_password[sizeof(app_cfg->ap_password) - 1] = '\0';

    wifi_config_t ap_config;
    configure_ap(&ap_config);
    return esp_wifi_set_config(WIFI_IF_AP, &ap_config);
}

size_t app_wifi_scan(app_wifi_ap_info_t *out, size_t max_entries)
{
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan failed: %s", esp_err_to_name(err));
        return 0;
    }

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    if (num == 0) {
        return 0;
    }

    wifi_ap_record_t *records = calloc(num, sizeof(wifi_ap_record_t));
    if (!records) {
        return 0;
    }
    esp_wifi_scan_get_ap_records(&num, records);

    size_t count = 0;
    for (int record_i = 0; record_i < num && count < max_entries; record_i++) {
        strncpy(out[count].ssid, (char *)records[record_i].ssid, sizeof(out[count].ssid) - 1);
        out[count].ssid[sizeof(out[count].ssid) - 1] = '\0';
        out[count].rssi = records[record_i].rssi;
        out[count].authmode = records[record_i].authmode;
        count++;
    }
    free(records);
    return count;
}

esp_err_t app_wifi_connect_sta(const char *ssid, const char *password, uint32_t timeout_ms)
{
    wifi_config_t sta_config = {0};
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    if (password) {
        strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);
    }
    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    s_have_creds = true;
    esp_wifi_connect();

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE,
                                            pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_FAIL;
}

int8_t app_wifi_get_rssi(void)
{
    wifi_ap_record_t info;
    if (!s_connected || esp_wifi_sta_get_ap_info(&info) != ESP_OK) {
        return 0;
    }
    return info.rssi;
}
