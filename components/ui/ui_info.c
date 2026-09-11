#include <stdio.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "ui_internal.h"
#include "app_wifi.h"
#include "bsp/bsp_board.h"

#define FW_VERSION "1.0.0"

static lv_obj_t *s_label;

static void update_cb(lv_timer_t *t)
{
    (void)t;
    char ip[16] = "-";
    app_wifi_get_ip_str(ip, sizeof(ip));
    char ap_ssid[33] = {0};
    app_wifi_get_ap_ssid(ap_ssid, sizeof(ap_ssid));

    int64_t uptime_s = esp_timer_get_time() / 1000000;
    uint32_t heap_kb = esp_get_free_heap_size() / 1024;
    const char *sd_status = bsp_sdcard_is_mounted() ? "mounted" : "not found";

    char buf[400];
    if (app_wifi_get_mode() == APP_WIFI_MODE_STA) {
        snprintf(buf, sizeof(buf),
                 "WT32 SmallTV  -  fw %s\n\n"
                 "Wi-Fi: connected (%d dBm)\n"
                 "IP address: %s\n"
                 "Configure at http://%s/\n\n"
                 "SD card: %s\n"
                 "Uptime: %" PRId64 "s   Free heap: %" PRIu32 " KB",
                 FW_VERSION, app_wifi_get_rssi(), ip, ip, sd_status, uptime_s, heap_kb);
    } else {
        snprintf(buf, sizeof(buf),
                 "WT32 SmallTV  -  fw %s\n\n"
                 "Wi-Fi not set up yet.\n"
                 "Connect to \"%s\"\n(password: smalltv1234)\n"
                 "then open http://%s/\n\n"
                 "SD card: %s\n"
                 "Uptime: %" PRId64 "s   Free heap: %" PRIu32 " KB",
                 FW_VERSION, ap_ssid, ip, sd_status, uptime_s, heap_kb);
    }
    lv_label_set_text(s_label, buf);
}

lv_obj_t *ui_info_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10161f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_label, lv_color_white(), 0);
    lv_obj_set_width(s_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(s_label);

    lv_timer_create(update_cb, 3000, NULL);
    return scr;
}

void ui_info_on_show(void)
{
    update_cb(NULL);
}
