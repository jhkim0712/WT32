/**
 * @file ui_setup.c
 * @brief First-time Wi-Fi setup screen: shows a scannable QR code for the
 *        fallback SoftAP (see app_wifi.c) so a phone can join it without
 *        anyone having to type the SSID/password in by hand.
 *
 * Only relevant while no Wi-Fi credentials are stored yet - ui_setup_is_visible()
 * gates this screen out of rotation the moment app_config's wifi_ssid is set
 * (whether that happens via the web UI's Wi-Fi tab or a factory reset).
 */
#include <string.h>
#include <stdio.h>
#include "ui_internal.h"
#include "app_config.h"
#include "app_wifi.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_title;
static lv_obj_t *s_qr;
static lv_obj_t *s_scan_hint;
static lv_obj_t *s_creds_label;
static lv_obj_t *s_step2;
static char s_last_ssid[33] = {0};
static char s_last_pw[APP_CFG_PASS_MAX_LEN] = {0};
static bool s_theme_dark = true; /* sentinel matching the initial style below; forces the first apply_theme() to run */

static void apply_theme(void)
{
    bool dark = ui_theme_is_dark();
    if (dark == s_theme_dark) {
        return;
    }
    s_theme_dark = dark;

    /* The QR code itself stays black-on-white regardless of theme - a
     * scanner needs the contrast, not a matching color scheme. */
    lv_obj_set_style_bg_color(s_scr, ui_theme_color(0x10161f, 0xf5f6f9), 0);
    lv_obj_set_style_text_color(s_title, ui_theme_color(0xffffff, 0x1c1f26), 0);
    lv_obj_set_style_text_color(s_scan_hint, ui_theme_color(0x9e9e9e, 0x5f6368), 0);
    lv_obj_set_style_text_color(s_creds_label, ui_theme_color(0xffffff, 0x1c1f26), 0);
    lv_obj_set_style_text_color(s_step2, ui_theme_color(0x9e9e9e, 0x5f6368), 0);
}

/* Escape the characters the "WIFI:" QR payload treats as separators, per
 * https://github.com/zxing/zxing/wiki/Barcode-Contents - the generated AP
 * SSID never needs it (alnum + '-' only) but a user-chosen ap_password
 * could contain any of these. */
static void escape_qr_field(const char *in, char *out, size_t out_len)
{
    size_t out_i = 0;
    for (size_t in_i = 0; in[in_i] != '\0' && out_i + 2 < out_len; in_i++) {
        char c = in[in_i];
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') {
            out[out_i++] = '\\';
        }
        out[out_i++] = c;
    }
    out[out_i] = '\0';
}

static void update_cb(lv_timer_t *t)
{
    (void)t;
    apply_theme();

    char ssid[33];
    app_wifi_get_ap_ssid(ssid, sizeof(ssid));
    const char *pw = app_config_get()->ap_password;

    if (strcmp(ssid, s_last_ssid) == 0 && strcmp(pw, s_last_pw) == 0) {
        return; /* AP SSID/password unchanged - skip the QR re-render */
    }
    strncpy(s_last_ssid, ssid, sizeof(s_last_ssid) - 1);
    strncpy(s_last_pw, pw, sizeof(s_last_pw) - 1);
    s_last_pw[sizeof(s_last_pw) - 1] = '\0';

    char esc_ssid[sizeof(s_last_ssid) * 2];
    char esc_pw[APP_CFG_PASS_MAX_LEN * 2];
    escape_qr_field(ssid, esc_ssid, sizeof(esc_ssid));
    escape_qr_field(pw, esc_pw, sizeof(esc_pw));

    char qr_data[220];
    if (pw[0]) {
        snprintf(qr_data, sizeof(qr_data), "WIFI:T:WPA;S:%s;P:%s;;", esc_ssid, esc_pw);
    } else {
        snprintf(qr_data, sizeof(qr_data), "WIFI:T:nopass;S:%s;;", esc_ssid);
    }
    lv_qrcode_update(s_qr, qr_data, strlen(qr_data));

    char creds_buf[128];
    if (pw[0]) {
        snprintf(creds_buf, sizeof(creds_buf), "SSID: %s\nPassword: %s", ssid, pw);
    } else {
        snprintf(creds_buf, sizeof(creds_buf), "SSID: %s\n(open network, no password)", ssid);
    }
    lv_label_set_text(s_creds_label, creds_buf);
}

lv_obj_t *ui_setup_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
    /* Dark-theme colors, matching s_theme_dark's initial value above - see
     * apply_theme(), which takes over from here once the theme is resolved. */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x10161f), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_title = lv_label_create(scr);
    lv_label_set_text(s_title, "Wi-Fi Setup");
    lv_obj_set_style_text_font(s_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_title, lv_color_white(), 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 18);

    s_qr = lv_qrcode_create(scr);
    lv_qrcode_set_size(s_qr, 180);
    lv_qrcode_set_dark_color(s_qr, lv_color_black());
    lv_qrcode_set_light_color(s_qr, lv_color_white());
    lv_qrcode_set_quiet_zone(s_qr, true); /* scanners need the white margin around the modules */
    lv_obj_align(s_qr, LV_ALIGN_LEFT_MID, 30, 15);

    s_scan_hint = lv_label_create(scr);
    lv_label_set_text(s_scan_hint, "Scan with your phone's camera");
    lv_obj_set_style_text_font(s_scan_hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_scan_hint, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_align_to(s_scan_hint, s_qr, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    /* Right column, clear of the QR block (which ends around x=210). */
    s_creds_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_creds_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_creds_label, lv_color_white(), 0);
    lv_obj_set_width(s_creds_label, 210);
    lv_obj_align(s_creds_label, LV_ALIGN_LEFT_MID, 240, -30);

    s_step2 = lv_label_create(scr);
    lv_label_set_text(s_step2, "Or connect manually,\nthen open\nhttp://192.168.4.1/");
    lv_obj_set_style_text_font(s_step2, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_step2, lv_palette_main(LV_PALETTE_GREY), 0);
    lv_obj_set_width(s_step2, 210);
    lv_obj_align_to(s_step2, s_creds_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

    lv_timer_create(update_cb, 3000, NULL);
    update_cb(NULL); /* fill in the QR/labels now instead of leaving them blank for 3s */
    return scr;
}

void ui_setup_on_show(void)
{
    update_cb(NULL);
}

bool ui_setup_is_visible(void)
{
    /* Same "configured" test app_wifi.c uses to decide whether to auto-join
     * STA - an empty stored SSID means first-time setup hasn't happened. */
    return app_config_get()->wifi_ssid[0] == '\0';
}
