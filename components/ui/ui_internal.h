/**
 * @file ui_internal.h
 * @brief Private declarations shared between ui.c and the individual page
 *        modules. Not installed under include/ - only visible inside this
 *        component.
 */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

lv_obj_t *ui_clock_create(void);
void ui_clock_on_show(void);

lv_obj_t *ui_album_create(void);
void ui_album_on_show(void);

lv_obj_t *ui_weather_create(void);
void ui_weather_on_show(void);
/** @return true if the weather screen should currently appear in rotation. */
bool ui_weather_is_visible(void);

lv_obj_t *ui_info_create(void);
void ui_info_on_show(void);

lv_obj_t *ui_setup_create(void);
void ui_setup_on_show(void);
/** @return true if Wi-Fi hasn't been configured yet (first-time setup). */
bool ui_setup_is_visible(void);

/* --- Theme (dark/light/auto, see app_config's display_theme) --- */

/** @return true if the UI should currently render in dark mode - resolves
 *  app_config's display_theme against the time of day when it's "auto". */
bool ui_theme_is_dark(void);

/** Shorthand for lv_color_hex(ui_theme_is_dark() ? dark_hex : light_hex) -
 *  every screen styles itself with pairs of these instead of one fixed
 *  color, so lv_obj_set_style_*_color() calls read as "which color, in
 *  which theme" at the call site. */
lv_color_t ui_theme_color(uint32_t dark_hex, uint32_t light_hex);
