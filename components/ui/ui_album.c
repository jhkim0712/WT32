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
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "ui_internal.h"
#include "app_photo.h"
#include "app_config.h"
#include "bsp/bsp_pins.h"

static const char *TAG = "ui_album";

static lv_obj_t *s_scr;
static lv_obj_t *s_img;
static lv_obj_t *s_gif;
static lv_obj_t *s_empty_label;
static lv_image_dsc_t s_img_dsc;
static uint16_t *s_canvas = NULL; /* allocated once, never freed - see canvas_get() */
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

/* The one 480x320 RGB565 canvas (~300KB) every static photo is decoded
 * into, kept for the life of the firmware. It used to be malloc'd per photo
 * and freed before the next, but with LVGL, TLS, the Flickr sync and the
 * decoders' own scratch buffers all churning through PSRAM in between, it
 * fragmented quickly: after a few minutes there'd be 440KB free yet no
 * single 300KB block, and every photo failed with ESP_ERR_NO_MEM. Grabbed
 * from ui_album_create() while PSRAM is still unfragmented; the retry here
 * only matters if that very first attempt failed. */
static uint16_t *canvas_get(void)
{
    if (!s_canvas) {
        size_t size = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t);
        s_canvas = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!s_canvas) {
            ESP_LOGW(TAG, "out of PSRAM allocating the %ux%u photo canvas (%u bytes needed, largest free block %u)",
                     BSP_LCD_H_RES, BSP_LCD_V_RES, (unsigned)size,
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        }
    }
    return s_canvas;
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
static void show_index_impl(size_t idx)
{
    size_t count = app_photo_count();
    if (count == 0) {
        lv_image_set_src(s_img, NULL);
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
        lv_image_set_src(s_img, NULL);
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

        if (!lv_gif_is_loaded(s_gif)) {
            /* LVGL bug/rough edge (managed_components/lvgl__lvgl's
             * lv_gif.c): when switching away from an already-open gif,
             * lv_gif_set_src() unconditionally resumes the animation timer
             * for the *new* one before it's known whether that new one
             * will actually load - if it doesn't (bad file, or - the
             * likely case here given how tight PSRAM already is - its own
             * decode buffer failing to allocate), the timer is left
             * running against a NULL draw buffer, and the very next tick
             * crashes (LoadProhibited in gif_disposal_last_frame). Not
             * something we can fix at the source without patching a
             * managed component, but cheap to guard against here. */
            ESP_LOGW(TAG, "Failed to load GIF %s (bad file, or out of PSRAM for its frame buffer)", path);
            lv_gif_pause(s_gif);
            lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
            return;
        }

        lv_obj_center(s_gif);
        lv_obj_clear_flag(s_gif, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_gif_pause(s_gif);
    lv_obj_add_flag(s_gif, LV_OBJ_FLAG_HIDDEN);

    /* Detach s_img before overwriting the canvas it points at - if the
     * decode below fails, s_img is left hidden rather than showing a
     * half-overwritten frame. */
    lv_image_set_src(s_img, NULL);

    uint16_t *canvas = canvas_get();
    if (!canvas || app_photo_decode_to_canvas(idx, canvas, BSP_LCD_H_RES, BSP_LCD_V_RES) != ESP_OK) {
        lv_obj_add_flag(s_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    s_img_dsc.header.w = BSP_LCD_H_RES;
    s_img_dsc.header.h = BSP_LCD_V_RES;
    s_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_img_dsc.header.stride = BSP_LCD_H_RES * 2;
    s_img_dsc.data_size = (size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    s_img_dsc.data = (const uint8_t *)canvas;

    /* Reassign the source (not just mutate the struct in place) so LVGL
     * doesn't skip the redraw thinking the source pointer is unchanged. */
    lv_image_set_src(s_img, NULL);
    lv_image_set_src(s_img, &s_img_dsc);
    lv_obj_clear_flag(s_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_empty_label, LV_OBJ_FLAG_HIDDEN);
}

/* Thin wrapper around show_index_impl() that re-syncs s_timer's own schedule
 * to "now" right after a slide actually finishes going up on screen.
 *
 * Without this, a slide's visible time gets shortchanged by however long its
 * own decode/load took: LVGL's lv_timer_exec() stamps the timer's last_run
 * *before* invoking advance_cb() (see managed_components/lvgl__lvgl's
 * lv_timer.c), so that work counts against the interval before the slide is
 * even on screen. Static photos decode fast enough for this to be in the
 * noise, but a GIF's own load (open the file off the SD card, parse its
 * header, allocate a native-resolution draw buffer, decode the first frame -
 * see lv_gif.c's gif_initialize()) is slow enough that it was visibly eating
 * into the slide's own display time, making the transition right after a GIF
 * (and, to a lesser extent, into one) happen noticeably earlier than the
 * configured interval. Resetting here instead makes every slide - GIF or
 * not - get the full interval measured from when it actually appeared. */
static void show_index(size_t idx)
{
    show_index_impl(idx);
    lv_timer_reset(s_timer);
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
    canvas_get();
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
    /* Must be set before the first lv_gif_set_src() (per its own doc
     * comment) or it just triggers an extra reallocation later. Halves the
     * GIF's own internal draw buffer vs. the ARGB8888 default (4 bytes/px);
     * PSRAM is tight enough on this board that this matters, and we have
     * no use for per-frame alpha anyway - the album always fills the
     * screen behind it either way. */
    lv_gif_set_color_format(s_gif, LV_COLOR_FORMAT_RGB565);
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

uint32_t ui_album_dwell_ms(void)
{
    size_t count = app_photo_count();
    if (count == 0) {
        return 0;
    }
    app_config_t *cfg = app_config_get();
    uint32_t interval_ms = cfg->album_interval_s > 0 ? (uint32_t)cfg->album_interval_s * 1000 : 8000;
    return (uint32_t)count * interval_ms;
}
