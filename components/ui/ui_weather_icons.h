/**
 * @file ui_weather_icons.h
 * @brief Weather condition icon glyphs, from Erik Flowers' "Weather Icons"
 *        font (https://github.com/erikflowers/weather-icons, SIL OFL 1.1).
 *
 * Earlier this used the pack's SVG artwork directly, embedded as raw bytes
 * and rendered through LVGL's (experimental) SVG decoder - that needed
 * ThorVG, a ~2.3MB bundled C++ vector-graphics library, which turned out to
 * be a repeated source of build breakage (upstream ThorVG/lv_svg_decoder.c
 * warnings this toolchain's -Werror treats as fatal - see git history).
 *
 * This instead uses a small subset of the font's glyphs (just the icons the
 * weather screen needs) converted into an LVGL bitmap font with lv_font_conv
 * (see font_weather_icons_64.c) - the same mechanism this codebase already
 * uses for LV_SYMBOL_WARNING etc., just with our own font instead of the
 * Montserrat-bundled symbol set. No SVG decoder, no ThorVG, no runtime
 * vector rendering - just a label with a custom font.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The baked-in font these glyphs live in - see font_weather_icons_64.c. */
LV_FONT_DECLARE(font_weather_icons_64);

typedef enum {
    UI_WEATHER_ICON_CLEAR_SKY,
    UI_WEATHER_ICON_FEW_CLOUDS,
    UI_WEATHER_ICON_SCATTERED_CLOUDS,
    UI_WEATHER_ICON_BROKEN_CLOUDS,
    UI_WEATHER_ICON_SHOWER_RAIN,
    UI_WEATHER_ICON_RAIN,
    UI_WEATHER_ICON_THUNDERSTORM,
    UI_WEATHER_ICON_SNOW,
    UI_WEATHER_ICON_MIST,
} ui_weather_icon_t;

/**
 * @return the UTF-8 glyph (a string literal, safe to hand straight to
 *         lv_label_set_text()) for one condition, in font_weather_icons_64.
 * @param is_day picks the day/night variant of icons that have both (all of
 *               them, here) - see the day/night pairs in ui_weather_icons.c.
 */
const char *ui_weather_icon_glyph(ui_weather_icon_t icon, bool is_day);

/**
 * Map an OpenWeatherMap "weather[0].id" condition code
 * (see https://openweathermap.org/weather-conditions) to the closest icon
 * this set has. Doesn't distinguish OWM's finer categories (e.g. drizzle
 * vs. rain, or exactly what "few/scattered/broken" cloud cover means vs.
 * 801/802/803/804), so this is an approximation, not a 1:1 map.
 * Unrecognized codes fall back to UI_WEATHER_ICON_CLEAR_SKY.
 */
ui_weather_icon_t ui_weather_icon_from_owm_id(int id);

/* A few extra glyphs from the same font/size, for detail-row icons (sunrise,
 * sunset, wind, humidity) if the weather screen grows to use them - not
 * currently wired into ui_weather.c. */
#define UI_WI_SUNRISE     "\xEF\x81\x91" /* U+F051 wi-sunrise */
#define UI_WI_SUNSET      "\xEF\x81\x92" /* U+F052 wi-sunset */
#define UI_WI_STRONG_WIND "\xEF\x81\x90" /* U+F050 wi-strong-wind */
#define UI_WI_HUMIDITY    "\xEF\x81\xBA" /* U+F07A wi-humidity */

#ifdef __cplusplus
}
#endif
