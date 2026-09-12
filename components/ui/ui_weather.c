#include <stdio.h>
#include <time.h>
#include "ui_internal.h"
#include "app_config.h"
#include "app_weather.h"
#include "ui_weather_icons.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_icon_label;
static lv_obj_t *s_temp_label;
static lv_obj_t *s_desc_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_status_label;
static bool s_theme_dark = true; /* sentinel matching the initial style below; forces the first apply_theme() to run */

static void apply_theme(void)
{
    bool dark = ui_theme_is_dark();
    if (dark == s_theme_dark) {
        return;
    }
    s_theme_dark = dark;

    lv_obj_set_style_bg_color(s_scr, ui_theme_color(0x0b1a1a, 0xf1f8f7), 0);
    lv_obj_set_style_text_color(s_icon_label, ui_theme_color(0xffffff, 0x1c1f26), 0);
    lv_obj_set_style_text_color(s_temp_label, ui_theme_color(0x35d0c3, 0x0d8f82), 0);
    lv_obj_set_style_text_color(s_desc_label, ui_theme_color(0xffffff, 0x1c1f26), 0);
    lv_obj_set_style_text_color(s_detail_label, ui_theme_color(0xb7d6d2, 0x4d6b67), 0);
    lv_obj_set_style_text_color(s_status_label, ui_theme_color(0xffb300, 0xa66a00), 0);
}

static void update_cb(lv_timer_t *t)
{
    (void)t;
    apply_theme();

    app_config_t *cfg = app_config_get();

    if (!cfg->weather_enabled) {
        lv_obj_add_flag(s_icon_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_temp_label, "");
        lv_label_set_text(s_desc_label, "Weather is off");
        lv_label_set_text(s_detail_label, "");
        lv_label_set_text(s_status_label, "Turn it on in the web UI's Weather tab");
        return;
    }
    if (!cfg->weather_api_key[0] || !cfg->weather_city_id[0]) {
        lv_obj_add_flag(s_icon_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_temp_label, "");
        lv_label_set_text(s_desc_label, "Weather not set up");
        lv_label_set_text(s_detail_label, "");
        lv_label_set_text(s_status_label, "Set an API key and city ID in the web UI's Weather tab");
        return;
    }

    app_weather_data_t w;
    app_weather_get(&w);

    if (!w.valid) {
        lv_obj_add_flag(s_icon_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_temp_label, "");
        lv_label_set_text(s_desc_label, "Loading weather...");
        lv_label_set_text(s_detail_label, "");
        lv_label_set_text(s_status_label,
                           w.have_error ? "Waiting for a successful fetch..." : "");
        return;
    }

    lv_label_set_text(s_icon_label, ui_weather_icon_glyph(ui_weather_icon_from_owm_id(w.condition_id), w.is_day));
    lv_obj_remove_flag(s_icon_label, LV_OBJ_FLAG_HIDDEN);

    char temp_buf[16];
    snprintf(temp_buf, sizeof(temp_buf), "%.0f\xC2\xB0" "C", w.temp_c);
    lv_label_set_text(s_temp_label, temp_buf);

    /* Comfortably larger than description (APP_WEATHER_DESC_MAX=48) +
     * separator + city_name (APP_WEATHER_CITY_NAME_MAX=48) could ever
     * produce, but GCC can't prove that bound through the ternaries -
     * sized to what its own truncation warning reports rather than fought
     * with a narrower buffer. */
    char desc_buf[128];
    snprintf(desc_buf, sizeof(desc_buf), "%s%s%s",
             w.description[0] ? w.description : "-",
             w.city_name[0] ? "  -  " : "",
             w.city_name[0] ? w.city_name : "");
    lv_label_set_text(s_desc_label, desc_buf);

    char detail_buf[96];
    snprintf(detail_buf, sizeof(detail_buf),
             "Feels like %.0f\xC2\xB0" "C   H/L %.0f\xC2\xB0/%.0f\xC2\xB0\n"
             "Humidity %d%%   Wind %.1f m/s",
             w.feels_like_c, w.temp_max_c, w.temp_min_c, w.humidity_pct, w.wind_speed_ms);
    lv_label_set_text(s_detail_label, detail_buf);

    if (w.have_error) {
        lv_label_set_text(s_status_label, LV_SYMBOL_WARNING " showing last known reading");
    } else {
        struct tm tm_now;
        localtime_r(&w.updated_at, &tm_now);
        char stamp[48];
        strftime(stamp, sizeof(stamp), "Updated %H:%M", &tm_now);
        lv_label_set_text(s_status_label, stamp);
    }
}

lv_obj_t *ui_weather_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    s_scr = scr;
    /* Dark-theme colors, matching s_theme_dark's initial value above - see
     * apply_theme(), which takes over from here once the theme is resolved. */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0b1a1a), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_icon_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_icon_label, &font_weather_icons_64, 0);
    lv_obj_set_style_text_color(s_icon_label, lv_color_white(), 0);
    lv_obj_align(s_icon_label, LV_ALIGN_CENTER, -100, -60);
    lv_obj_add_flag(s_icon_label, LV_OBJ_FLAG_HIDDEN); /* shown once weather data exists */

    s_temp_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_temp_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_temp_label, lv_color_hex(0x35d0c3), 0);
    lv_obj_align(s_temp_label, LV_ALIGN_CENTER, 40, -60);

    s_desc_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_desc_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(s_desc_label, lv_color_white(), 0);
    lv_obj_set_width(s_desc_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_desc_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_desc_label, LV_ALIGN_CENTER, 0, -5);

    s_detail_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_detail_label, lv_color_hex(0xb7d6d2), 0);
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_detail_label, LV_ALIGN_CENTER, 0, 45);

    s_status_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xffb300), 0);
    lv_obj_set_width(s_status_label, LV_PCT(90));
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_status_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    lv_timer_create(update_cb, 5000, NULL);
    return scr;
}

void ui_weather_on_show(void)
{
    update_cb(NULL);
}

bool ui_weather_is_visible(void)
{
    return app_config_get()->weather_enabled;
}
