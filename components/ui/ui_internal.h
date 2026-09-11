/**
 * @file ui_internal.h
 * @brief Private declarations shared between ui.c and the individual page
 *        modules. Not installed under include/ - only visible inside this
 *        component.
 */
#pragma once

#include "lvgl.h"

lv_obj_t *ui_clock_create(void);
void ui_clock_on_show(void);

lv_obj_t *ui_album_create(void);
void ui_album_on_show(void);

lv_obj_t *ui_info_create(void);
void ui_info_on_show(void);
