/**
 * @file ui_theme.c
 * @brief Resolves app_config's display_theme (dark/light/auto) into a plain
 *        "is it dark right now" bool the individual screens style against.
 */
#include <stdio.h>
#include "ui_internal.h"
#include "app_config.h"
#include "app_time.h"

/** Parse a "HH:MM" string into minutes-since-midnight. @return false (and
 *  leaves *out_minutes untouched) if the string isn't a valid 00:00-23:59
 *  time. */
static bool parse_hhmm(const char *hhmm, int *out_minutes)
{
    int hour = 0, minute = 0;
    if (sscanf(hhmm, "%d:%d", &hour, &minute) != 2) {
        return false;
    }
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return false;
    }
    *out_minutes = hour * 60 + minute;
    return true;
}

bool ui_theme_is_dark(void)
{
    app_config_t *cfg = app_config_get();
    if (cfg->display_theme == DISPLAY_THEME_DARK) {
        return true;
    }
    if (cfg->display_theme == DISPLAY_THEME_LIGHT) {
        return false;
    }

    /* AUTO. Fall back to dark - the UI's original, always-on look - if NTP
     * hasn't synced yet or the stored times are malformed, rather than
     * guessing with an un-synced clock. */
    int day_start_min = 0, night_start_min = 0;
    if (!app_time_is_synced() || !parse_hhmm(cfg->theme_day_start, &day_start_min) ||
        !parse_hhmm(cfg->theme_night_start, &night_start_min) || day_start_min == night_start_min) {
        return true;
    }

    struct tm now;
    app_time_get_local(&now);
    int now_min = now.tm_hour * 60 + now.tm_min;

    bool is_light;
    if (day_start_min < night_start_min) {
        /* Normal case: e.g. light from 07:00 up to (not including) 20:00. */
        is_light = (now_min >= day_start_min && now_min < night_start_min);
    } else {
        /* Someone set the two the other way around - treat night as the
         * range that wraps past midnight instead of refusing to pick one. */
        is_light = !(now_min >= night_start_min || now_min < day_start_min);
    }
    return !is_light;
}

lv_color_t ui_theme_color(uint32_t dark_hex, uint32_t light_hex)
{
    return lv_color_hex(ui_theme_is_dark() ? dark_hex : light_hex);
}
