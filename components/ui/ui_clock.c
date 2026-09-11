#include <stdio.h>
#include <time.h>
#include "ui_internal.h"
#include "app_config.h"
#include "app_time.h"

static lv_obj_t *s_time_label;
static lv_obj_t *s_date_label;
static lv_obj_t *s_status_label;

static void update_cb(lv_timer_t *t)
{
    (void)t;
    struct tm now;
    app_time_get_local(&now);
    app_config_t *cfg = app_config_get();

    char time_buf[16];
    if (cfg->time_24h) {
        strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &now);
    } else {
        strftime(time_buf, sizeof(time_buf), "%I:%M:%S %p", &now);
    }
    lv_label_set_text(s_time_label, time_buf);

    char date_buf[48];
    strftime(date_buf, sizeof(date_buf), "%A, %B %d", &now);
    lv_label_set_text(s_date_label, date_buf);

    lv_label_set_text(s_status_label, app_time_is_synced() ? "" : LV_SYMBOL_WARNING " waiting for NTP sync...");
}

lv_obj_t *ui_clock_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b0f1a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_time_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_time_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(0x00e5ff), 0);
    lv_obj_align(s_time_label, LV_ALIGN_CENTER, 0, -30);

    s_date_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_date_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_date_label, lv_color_white(), 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 30);

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xffb300), 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_timer_create(update_cb, 1000, NULL);
    return scr;
}

void ui_clock_on_show(void)
{
    update_cb(NULL);
}
