#include "ui.h"
#include "ui_internal.h"
#include "bsp/bsp_board.h"
#include "app_config.h"

#define UI_PAGE_COUNT 5

static lv_obj_t *s_screens[UI_PAGE_COUNT];
static void (*s_on_show[UI_PAGE_COUNT])(void);
/* Optional per-page visibility check - NULL means "always visible". Pages
 * that can be turned off (Weather, from the web UI; Clock/Album/Weather all
 * three, while Wi-Fi isn't set up yet) are skipped over when cycling/swiping
 * instead of being removed from the array, so no other index needs to shift
 * around at runtime. */
static bool (*s_page_visible[UI_PAGE_COUNT])(void);
static int s_current = 0;
static lv_timer_t *s_cycle_timer = NULL;

static int wrap_index(int index)
{
    return ((index % UI_PAGE_COUNT) + UI_PAGE_COUNT) % UI_PAGE_COUNT;
}

static bool page_is_visible(int index)
{
    return s_page_visible[index] == NULL || s_page_visible[index]();
}

/* Wi-Fi not configured yet: the normal screens (clock/album/weather) stay
 * out of rotation and only the setup QR code + ui_info are reachable, so a
 * device fresh out of the box doesn't show a clock with no idea what time
 * it is instead of guiding the user through setup. */
static bool page_visible_when_configured(void)
{
    return !ui_setup_is_visible();
}

static bool weather_page_visible(void)
{
    return page_visible_when_configured() && ui_weather_is_visible();
}

static void show_screen(int index, int step, lv_screen_load_anim_t anim)
{
    int next = wrap_index(index);
    /* Skip disabled pages in the direction of travel. Bounded by
     * UI_PAGE_COUNT so an "everything disabled" edge case can't spin
     * forever - it just falls back to whatever index it started at. */
    for (int guard = 0; guard < UI_PAGE_COUNT && !page_is_visible(next); guard++) {
        next = wrap_index(next + step);
    }
    s_current = next;
    lv_screen_load_anim(s_screens[s_current], anim, 200, 0, false);
    if (s_on_show[s_current]) {
        s_on_show[s_current]();
    }
}

void ui_next_screen(void)
{
    show_screen(s_current + 1, 1, LV_SCR_LOAD_ANIM_MOVE_LEFT);
}

void ui_prev_screen(void)
{
    show_screen(s_current - 1, -1, LV_SCR_LOAD_ANIM_MOVE_RIGHT);
}

static void gesture_event_cb(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(indev);
    if (dir == LV_DIR_LEFT) {
        ui_next_screen();
    } else if (dir == LV_DIR_RIGHT) {
        ui_prev_screen();
    }
}

static void cycle_timer_cb(lv_timer_t *t)
{
    app_config_t *cfg = app_config_get();
    if (cfg->auto_cycle_enabled) {
        ui_next_screen();
    }
}

void ui_init(void)
{
    bsp_display_lock(0);

    s_screens[0] = ui_setup_create();
    s_on_show[0] = ui_setup_on_show;
    s_page_visible[0] = ui_setup_is_visible;
    s_screens[1] = ui_clock_create();
    s_on_show[1] = ui_clock_on_show;
    s_page_visible[1] = page_visible_when_configured;
    s_screens[2] = ui_album_create();
    s_on_show[2] = ui_album_on_show;
    s_page_visible[2] = page_visible_when_configured;
    s_screens[3] = ui_weather_create();
    s_on_show[3] = ui_weather_on_show;
    s_page_visible[3] = weather_page_visible;
    s_screens[4] = ui_info_create();
    s_on_show[4] = ui_info_on_show;

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_screens[i], gesture_event_cb, LV_EVENT_GESTURE, NULL);
    }

    /* Start on the first visible page rather than hardcoding index 0 - if
     * Wi-Fi isn't set up yet that's the setup screen, otherwise it's the
     * clock, same as before this screen existed. */
    s_current = 0;
    for (int guard = 0; guard < UI_PAGE_COUNT && !page_is_visible(s_current); guard++) {
        s_current = wrap_index(s_current + 1);
    }
    lv_screen_load(s_screens[s_current]);
    if (s_on_show[s_current]) {
        s_on_show[s_current]();
    }

    app_config_t *cfg = app_config_get();
    uint32_t period_ms = cfg->cycle_seconds > 0 ? (uint32_t)cfg->cycle_seconds * 1000 : 10000;
    s_cycle_timer = lv_timer_create(cycle_timer_cb, period_ms, NULL);

    bsp_display_unlock();
}
