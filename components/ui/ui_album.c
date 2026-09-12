#include <stdlib.h>
#include "ui_internal.h"
#include "app_photo.h"
#include "app_config.h"
#include "bsp/bsp_pins.h"

static lv_obj_t *s_img;
static lv_obj_t *s_empty_label;
static lv_image_dsc_t s_img_dsc;
static uint16_t *s_pixel_buf = NULL;
static size_t s_index = 0;
static lv_timer_t *s_timer;

static void free_current_buf(void)
{
    if (s_pixel_buf) {
        free(s_pixel_buf);
        s_pixel_buf = NULL;
    }
}

static void show_index(size_t idx)
{
    size_t count = app_photo_count();
    if (count == 0) {
        /* s_img_dsc.data may point at s_pixel_buf - clear the source before
         * freeing it so nothing can end up reading freed memory through it. */
        lv_image_set_src(s_img, NULL);
        free_current_buf();
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    idx = idx % count;
    uint16_t *buf = NULL;
    if (app_photo_decode_to_canvas(idx, BSP_LCD_H_RES, BSP_LCD_V_RES, &buf) != ESP_OK) {
        return;
    }

    free_current_buf();
    s_pixel_buf = buf;
    s_index = idx;

    s_img_dsc.header.w = BSP_LCD_H_RES;
    s_img_dsc.header.h = BSP_LCD_V_RES;
    s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.stride = BSP_LCD_H_RES * 2;
    s_img_dsc.data_size = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    s_img_dsc.data = (const uint8_t *)s_pixel_buf;

    /* Reassign the source (not just mutate the struct in place) so LVGL
     * doesn't skip the redraw thinking the source pointer is unchanged. */
    lv_image_set_src(s_img, NULL);
    lv_image_set_src(s_img, &s_img_dsc);
    lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
}

static void advance_cb(lv_timer_t *t)
{
    (void)t;
    /* Not guarded on app_photo_count() > 0: show_index() already handles the
     * zero case (hides the image, shows the "no photos" label) - it needs
     * to run even when the count just dropped to 0 (e.g. an SD card format
     * from the web UI while this screen was already showing a photo),
     * otherwise the last-decoded photo would stay on screen forever. */
    show_index(s_index + 1);
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    show_index(s_index + 1);
}

lv_obj_t *ui_album_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);

    s_img = lv_image_create(scr);
    lv_obj_center(s_img);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_img, tap_cb, LV_EVENT_CLICKED, NULL);

    s_empty_label = lv_label_create(scr);
    lv_label_set_text(s_empty_label, "No photos found.\nCopy .bmp/.jpg files into /sdcard/photos");
    lv_obj_set_style_text_align(s_empty_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_empty_label, lv_color_white(), 0);
    lv_obj_center(s_empty_label);

    app_config_t *cfg = app_config_get();
    uint32_t period_ms = cfg->album_interval_s > 0 ? (uint32_t)cfg->album_interval_s * 1000 : 8000;
    s_timer = lv_timer_create(advance_cb, period_ms, NULL);

    return scr;
}

void ui_album_on_show(void)
{
    if (app_photo_count() > 0 && s_pixel_buf == NULL) {
        show_index(0);
    }
}
