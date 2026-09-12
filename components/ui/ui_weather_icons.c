/**
 * @file ui_weather_icons.c
 * @brief See ui_weather_icons.h.
 *
 * Each glyph string below is a hand-picked codepoint from Weather Icons'
 * font, cross-checked against its own css/weather-icons.css (e.g. `.wi-day-
 * rain:before { content: "\f008"; }` -> U+F008), then UTF-8 encoded (all of
 * them fall in the 3-byte range, U+F000-U+F0FF). font_weather_icons_64.c was
 * generated with lv_font_conv against just this set of codepoints:
 *
 *   lv_font_conv --font weathericons-regular-webfont.ttf --size 64 --bpp 4 \
 *     --format lvgl --lv-font-name font_weather_icons_64 \
 *     -o font_weather_icons_64.c \
 *     -r 0xf00d,0xf00c,0xf086,0xf002,0xf031,0xf07d,0xf07e,0xf008,0xf036, \
 *        0xf009,0xf037,0xf010,0xf03b,0xf00a,0xf038,0xf003,0xf04a,0xf051, \
 *        0xf052,0xf050,0xf07a,0xf02e
 *
 * Regenerate it the same way (pick whatever --size/--bpp fits) if this set
 * of glyphs ever needs to change - see ui_weather_icons.h's UI_WI_* defines
 * for the codepoints already reserved for detail-row icons.
 */
#include <stddef.h>
#include "ui_weather_icons.h"

typedef struct {
    const char *day;
    const char *night;
} icon_glyphs_t;

static const icon_glyphs_t s_glyphs[] = {
    [UI_WEATHER_ICON_CLEAR_SKY]        = {"\xEF\x80\x8D" /* U+F00D wi-day-sunny */,
                                           "\xEF\x80\xAE" /* U+F02E wi-night-clear */},
    [UI_WEATHER_ICON_FEW_CLOUDS]       = {"\xEF\x80\x8C" /* U+F00C wi-day-sunny-overcast */,
                                           "\xEF\x82\x86" /* U+F086 wi-night-alt-cloudy */},
    [UI_WEATHER_ICON_SCATTERED_CLOUDS] = {"\xEF\x80\x82" /* U+F002 wi-day-cloudy */,
                                           "\xEF\x80\xB1" /* U+F031 wi-night-cloudy */},
    [UI_WEATHER_ICON_BROKEN_CLOUDS]    = {"\xEF\x81\xBD" /* U+F07D wi-day-cloudy-high */,
                                           "\xEF\x81\xBE" /* U+F07E wi-night-alt-cloudy-high */},
    [UI_WEATHER_ICON_RAIN]             = {"\xEF\x80\x88" /* U+F008 wi-day-rain */,
                                           "\xEF\x80\xB6" /* U+F036 wi-night-rain */},
    [UI_WEATHER_ICON_SHOWER_RAIN]      = {"\xEF\x80\x89" /* U+F009 wi-day-showers */,
                                           "\xEF\x80\xB7" /* U+F037 wi-night-showers */},
    [UI_WEATHER_ICON_THUNDERSTORM]     = {"\xEF\x80\x90" /* U+F010 wi-day-thunderstorm */,
                                           "\xEF\x80\xBB" /* U+F03B wi-night-thunderstorm */},
    [UI_WEATHER_ICON_SNOW]             = {"\xEF\x80\x8A" /* U+F00A wi-day-snow */,
                                           "\xEF\x80\xB8" /* U+F038 wi-night-snow */},
    [UI_WEATHER_ICON_MIST]             = {"\xEF\x80\x83" /* U+F003 wi-day-fog */,
                                           "\xEF\x81\x8A" /* U+F04A wi-night-fog */},
};

const char *ui_weather_icon_glyph(ui_weather_icon_t icon, bool is_day)
{
    if ((size_t)icon >= sizeof(s_glyphs) / sizeof(s_glyphs[0])) {
        icon = UI_WEATHER_ICON_CLEAR_SKY;
    }
    return is_day ? s_glyphs[icon].day : s_glyphs[icon].night;
}

ui_weather_icon_t ui_weather_icon_from_owm_id(int id)
{
    if (id >= 200 && id <= 232) {
        return UI_WEATHER_ICON_THUNDERSTORM;
    }
    if (id >= 300 && id <= 321) {
        return UI_WEATHER_ICON_RAIN; /* drizzle - no dedicated icon in this set */
    }
    if (id == 520 || id == 521 || id == 522 || id == 531) {
        return UI_WEATHER_ICON_SHOWER_RAIN;
    }
    if (id >= 500 && id <= 531) {
        return UI_WEATHER_ICON_RAIN;
    }
    if (id >= 600 && id <= 622) {
        return UI_WEATHER_ICON_SNOW;
    }
    if (id >= 701 && id <= 781) {
        return UI_WEATHER_ICON_MIST; /* mist/fog/haze/dust/smoke/sand/ash/squall/tornado */
    }
    if (id == 800) {
        return UI_WEATHER_ICON_CLEAR_SKY;
    }
    if (id == 801) {
        return UI_WEATHER_ICON_FEW_CLOUDS;
    }
    if (id == 802) {
        return UI_WEATHER_ICON_SCATTERED_CLOUDS;
    }
    if (id == 803 || id == 804) {
        return UI_WEATHER_ICON_BROKEN_CLOUDS;
    }
    return UI_WEATHER_ICON_CLEAR_SKY; /* unrecognized code - show something rather than nothing */
}
