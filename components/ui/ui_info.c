#include <stdio.h>
#include <inttypes.h>
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_psram.h"
#include "esp_heap_caps.h"
#include "ui_internal.h"
#include "app_wifi.h"
#include "app_ota.h"
#include "app_config.h"
#include "bsp/bsp_board.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_label;
static bool s_theme_dark = true; /* sentinel matching the initial style below; forces the first apply_theme() to run */

static void apply_theme(void)
{
    bool dark = ui_theme_is_dark();
    if (dark == s_theme_dark) {
        return;
    }
    s_theme_dark = dark;

    lv_obj_set_style_bg_color(s_scr, ui_theme_color(0x10161f, 0xf5f6f9), 0);
    lv_obj_set_style_text_color(s_label, ui_theme_color(0xffffff, 0x1c1f26), 0);
}

static void update_cb(lv_timer_t *t)
{
    (void)t;
    apply_theme();

    char ip[16] = "-";
    app_wifi_get_ip_str(ip, sizeof(ip));
    char ap_ssid[33] = {0};
    app_wifi_get_ap_ssid(ap_ssid, sizeof(ap_ssid));
    char fw_version[32];
    app_ota_get_current_version(fw_version, sizeof(fw_version));

    int64_t uptime_s = esp_timer_get_time() / 1000000;
    uint32_t heap_kb = esp_get_free_heap_size() / 1024;
    const char *sd_status = bsp_sdcard_is_mounted() ? "mounted" : "not found";

    char psram_buf[32];
    if (esp_psram_is_initialized()) {
        uint32_t psram_free_kb = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
        snprintf(psram_buf, sizeof(psram_buf), "PSRAM: %" PRIu32 " KB free", psram_free_kb);
    } else {
        snprintf(psram_buf, sizeof(psram_buf), "PSRAM: not found");
    }

    char buf[400];
    /* buf is comfortably larger than every field's actual declared size, but
     * sd_status is a `const char *` (not a fixed array) so GCC can't verify
     * that bound here - silence the truncation warning rather than
     * restructure around it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    if (app_wifi_get_mode() == APP_WIFI_MODE_STA) {
        snprintf(buf, sizeof(buf),
                 "WT32  -  fw %s (%s %s)\n\n"
                 "Wi-Fi: connected (%d dBm)\n"
                 "IP address: %s\n"
                 "Configure at http://%s/\n\n"
                 "SD card: %s\n"
                 "Uptime: %" PRId64 "s   Free heap: %" PRIu32 " KB\n"
                 "%s",
                 fw_version, __DATE__, __TIME__, app_wifi_get_rssi(), ip, ip, sd_status, uptime_s, heap_kb,
                 psram_buf);
    } else {
        const char *ap_pw = app_config_get()->ap_password;
        char pw_line[80]; /* fits the full APP_CFG_PASS_MAX_LEN password plus the surrounding text */
        if (ap_pw[0]) {
            snprintf(pw_line, sizeof(pw_line), "(password: %s)", ap_pw);
        } else {
            snprintf(pw_line, sizeof(pw_line), "(open network, no password)");
        }
        snprintf(buf, sizeof(buf),
                 "WT32  -  fw %s (%s %s)\n\n"
                 "Wi-Fi not set up yet.\n"
                 "Connect to \"%s\"\n%s\n"
                 "then open http://%s/\n\n"
                 "SD card: %s\n"
                 "Uptime: %" PRId64 "s   Free heap: %" PRIu32 " KB\n"
                 "%s",
                 fw_version, __DATE__, __TIME__, ap_ssid, pw_line, ip, sd_status, uptime_s, heap_kb, psram_buf);
    }
#pragma GCC diagnostic pop
    lv_label_set_text(s_label, buf);
}

lv_obj_t *ui_info_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
    /* Dark-theme colors, matching s_theme_dark's initial value above - see
     * apply_theme(), which takes over from here once the theme is resolved. */
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
