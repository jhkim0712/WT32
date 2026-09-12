/**
 * @file ui_album.c
 * @brief Photo album screen: slideshow of static images plus, separately,
 *        animated GIFs.
 *
 * Static formats (.bmp/.jpg/.png) go through app_photo_decode_to_canvas(),
 * which decodes and nearest-neighbor-stretches them to fill the panel
 * exactly, then get displayed as one fixed lv_image (s_img). GIFs are a
 * completely different, path-based pipeline: LVGL's own lv_gif widget
 * (s_gif) opens and plays them directly off the SD card, decoding and
 * redrawing frames on its own timer for as long as it's shown. Because
 * that widget owns its own draw buffer sized to the GIF's native
 * resolution, GIFs show at native size/centered rather than stretched to
 * fill the screen like the other formats (a uniform LVGL zoom can't
 * reproduce app_photo_resize.c's independent x/y stretch anyway).
 */
#include <stdio.h>
#include <stdlib.h>
#include "ui_internal.h"
#include "app_photo.h"
#include "app_config.h"
#include "bsp/bsp_pins.h"

static lv_obj_t *s_scr;
static lv_obj_t *s_img;
static lv_obj_t *s_gif;
static lv_obj_t *s_empty_label;
static lv_image_dsc_t s_img_dsc;
static uint16_t *s_pixel_buf = NULL;
static size_t s_index = 0;
static lv_timer_t *s_timer;
static bool s_theme_dark = true; /* sentinel matching the initial style below; forces the first apply_theme() to run */

/* Only really visible on the "no photos" placeholder - a real photo covers
 * the whole screen either way - but flipping it keeps that placeholder
 * readable in light mode too. */
static void apply_theme(void)
{
    bool dark = ui_theme_is_dark();
    if (dark == s_theme_dark) {
        return;
    }
    s_theme_dark = dark;

    lv_obj_set_style_bg_color(s_scr, ui_theme_color(0x000000, 0xf5f5f5), 0);
    lv_obj_set_style_text_color(s_empty_label, ui_theme_color(0xffffff, 0x1c1f26), 0);
}

static void free_current_buf(void)
{
    if (s_pixel_buf) {
        free(s_pixel_buf);
        s_pixel_buf = NULL;
    }
}

/* Registered on LV_EVENT_SCREEN_UNLOADED (see ui_album_create()): fires
 * whenever the user swipes/cycles away to a different screen. lv_gif has
 * no idea it's sitting on a screen that isn't the one being displayed any
 * more, so left alone it would keep decoding and redrawing frames
 * indefinitely in the background - pause it explicitly instead. Resumed
 * (or reloaded fresh) from ui_album_on_show() when the user comes back. */
static void screen_unloaded_cb(lv_event_t *e)
{
    (void)e;
    lv_gif_pause(s_gif);
}

/* Actually decodes/loads slide idx and displays it - always for real, no
 * "is this screen even showing right now" check (on_show/tap_cb only ever
 * call this while it genuinely is). advance_cb() below is the one exception
 * that needs such a check, since its timer runs regardless of which screen
 * is on the panel - it does that check itself before calling this. */
static void show_index(size_t idx)
{
    size_t count = app_photo_count();
    if (count == 0) {
        /* s_img_dsc.data may point at s_pixel_buf - clear the source before
         * freeing it so nothing can end up reading freed memory through it. */
        lv_image_set_src(s_img, NULL);
        free_current_buf();
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        lv_gif_pause(s_gif);
        lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    idx = idx % count;
    s_index = idx;
    const char *path = app_photo_get_path(idx);

    if (app_photo_is_gif(path)) {
        /* Same freed-before-hidden ordering as the count==0 case above,
         * and for the same reason - s_img is about to sit hidden and
         * pointed at nothing for a while, not immediately reassigned. */
        lv_image_set_src(s_img, NULL);
        free_current_buf();
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);

        /* lv_gif reads through LVGL's own fs layer (see sdkconfig.defaults'
         * LV_USE_FS_POSIX block), which needs the "S:" drive prefix -
         * app_photo already hands out plain POSIX paths (e.g.
         * "/sdcard/photos/x.gif"), so just prepend it. Static: LVGL keeps
         * this pointer around for as long as the gif stays loaded, not
         * just for the duration of this call. */
        static char gif_src[APP_PHOTO_PATH_MAX + 2];
        snprintf(gif_src, sizeof(gif_src), "S:%s", path);
        lv_gif_set_src(s_gif, gif_src);
        lv_obj_center(s_gif);
        lv_obj_clear_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_gif_pause(s_gif);
    lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);

    uint16_t *buf = NULL;
    if (app_photo_decode_to_canvas(idx, BSP_LCD_H_RES, BSP_LCD_V_RES, &buf) != ESP_OK) {
        return;
    }

    free_current_buf();
    s_pixel_buf = buf;

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
    /* This timer runs continuously no matter which screen is actually on
     * the panel. That's harmless for a static photo (just a wasted
     * decode), but a GIF's own animation timer would otherwise keep
     * running indefinitely for a slide nobody can see - so while this
     * screen isn't the active one, just advance which slide is "current"
     * and stop there; ui_album_on_show() loads it for real once the user
     * swipes back here. */
    if (lv_screen_active() != s_scr) {
        size_t count = app_photo_count();
        if (count > 0) {
            s_index = (s_index + 1) % count;
        }
        return;
    }

    apply_theme();
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
    s_scr = scr;
    /* Dark-theme colors, matching s_theme_dark's initial value above - see
     * apply_theme(), which takes over from here once the theme is resolved. */
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_border_width(scr, 0, 0);
    lv_obj_add_event_cb(scr, screen_unloaded_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    s_img = lv_image_create(scr);
    lv_obj_center(s_img);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    /* s_img covers virtually the whole screen, so it's what actually gets
     * hit-tested at touch-down - without this, LVGL delivers LV_EVENT_GESTURE
     * to s_img itself (which has no gesture handler) instead of bubbling it
     * up to the screen's LV_EVENT_GESTURE callback (see ui.c), so swiping to
     * switch screens silently did nothing while a finger started on the
     * photo; tapping to advance still worked since that's LV_EVENT_CLICKED,
     * a separate mechanism. */
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_img, tap_cb, LV_EVENT_CLICKED, NULL);

    /* Same tap-to-advance/swipe-to-switch-screens setup as s_img above -
     * whichever of the two is actually showing needs both to work. Left at
     * its default (content) size instead of BSP_LCD_H_RES/V_RES, per the
     * file comment, so it auto-sizes to each GIF's native resolution. */
    s_gif = lv_gif_create(scr);
    lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_gif, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_gif, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_gif, tap_cb, LV_EVENT_CLICKED, NULL);

    s_empty_label = lv_label_create(scr);
    lv_label_set_text(s_empty_label, "No photos found.\nCopy .bmp/.jpg/.png/.gif files into /sdcard/photos");
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
    apply_theme();
    show_index(s_index);
}
