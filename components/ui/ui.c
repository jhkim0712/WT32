#include "ui.h"
#include "ui_internal.h"
#include "bsp/bsp_board.h"
#include "app_config.h"

/* One entry per swipeable screen, in swipe order. Index into s_screens[] /
 * s_on_show[] / s_page_visible[] below - add new screens here instead of as
 * a bare array index, so the meaning of each slot stays self-documenting at
 * every call site. */
typedef enum {
    UI_PAGE_SETUP = 0,
    UI_PAGE_CLOCK,
    UI_PAGE_ALBUM,
    UI_PAGE_WEATHER,
    UI_PAGE_INFO,
    UI_PAGE_COUNT,
} ui_page_t;

static lv_obj_t *s_screens[UI_PAGE_COUNT];
static void (*s_on_show[UI_PAGE_COUNT])(void);
/* Optional per-page visibility check - NULL means "always visible". Pages
 * that can be turned off (Weather, from the web UI; Clock/Album/Weather all
 * three, while Wi-Fi isn't set up yet) are skipped over when cycling/swiping
 * instead of being removed from the array, so no other index needs to shift
 * around at runtime. */
static bool (*s_page_visible[UI_PAGE_COUNT])(void);
static ui_page_t s_current = UI_PAGE_SETUP;
static lv_timer_t *s_cycle_timer = NULL;

/* index is a plain int, not ui_page_t - callers pass s_current +/- 1, which
 * is transiently out of the enum's normal range until this wraps it back
 * into [0, UI_PAGE_COUNT). */
static ui_page_t wrap_index(int index)
{
    return (ui_page_t)(((index % UI_PAGE_COUNT) + UI_PAGE_COUNT) % UI_PAGE_COUNT);
}

static bool page_is_visible(ui_page_t page)
{
    return s_page_visible[page] == NULL || s_page_visible[page]();
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

/* The generic auto-cycle timer normally dwells on each screen for
 * cfg->cycle_seconds before moving on - fine for clock/weather/info, but for
 * the album that arbitrarily cuts the slideshow off after only a photo or
 * two whenever there are more photos than cycle_seconds/album_interval_s
 * implies. Stretch the dwell time to cover at least one full loop through
 * the album's own photos in that case, so auto-cycle never carries the
 * screen away before every photo has had a turn. */
static void restart_cycle_timer(void)
{
    if (!s_cycle_timer) {
        return;
    }
    app_config_t *cfg = app_config_get();
    uint32_t period_ms = cfg->cycle_seconds > 0 ? (uint32_t)cfg->cycle_seconds * 1000 : 10000;
    if (s_current == UI_PAGE_ALBUM) {
        uint32_t dwell_ms = ui_album_dwell_ms();
        if (dwell_ms > period_ms) {
            period_ms = dwell_ms;
        }
    }
    lv_timer_set_period(s_cycle_timer, period_ms);
    lv_timer_reset(s_cycle_timer);
}

static void show_screen(int index, int step, lv_screen_load_anim_t anim)
{
    ui_page_t next = wrap_index(index);
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
    restart_cycle_timer();
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

    s_screens[UI_PAGE_SETUP] = ui_setup_create();
    s_on_show[UI_PAGE_SETUP] = ui_setup_on_show;
    s_page_visible[UI_PAGE_SETUP] = ui_setup_is_visible;
    s_screens[UI_PAGE_CLOCK] = ui_clock_create();
    s_on_show[UI_PAGE_CLOCK] = ui_clock_on_show;
    s_page_visible[UI_PAGE_CLOCK] = page_visible_when_configured;
    s_screens[UI_PAGE_ALBUM] = ui_album_create();
    s_on_show[UI_PAGE_ALBUM] = ui_album_on_show;
    s_page_visible[UI_PAGE_ALBUM] = page_visible_when_configured;
    s_screens[UI_PAGE_WEATHER] = ui_weather_create();
    s_on_show[UI_PAGE_WEATHER] = ui_weather_on_show;
    s_page_visible[UI_PAGE_WEATHER] = weather_page_visible;
    s_screens[UI_PAGE_INFO] = ui_info_create();
    s_on_show[UI_PAGE_INFO] = ui_info_on_show;

    for (ui_page_t page = 0; page < UI_PAGE_COUNT; page++) {
        lv_obj_add_flag(s_screens[page], LV_OBJ_FLAG_CLICKABLE);
        /* lv_obj_create() screens are scrollable by default. None of our
         * screens actually need to scroll, but LVGL still tries scrolling
         * *before* gesture recognition on any drag - if it succeeds (even
         * on a sub-pixel content overflow we didn't intend), indev_gesture()
         * bails out early and no LV_EVENT_GESTURE ever fires, silently
         * eating swipe-to-switch-screens while the auto-cycle timer (which
         * calls ui_next_screen() directly, bypassing touch entirely) keeps
         * working fine. Removing SCROLLABLE guarantees every drag reaches
         * gesture recognition instead. */
        lv_obj_remove_flag(s_screens[page], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(s_screens[page], gesture_event_cb, LV_EVENT_GESTURE, NULL);
    }

    /* Start on the first visible page rather than hardcoding index 0 - if
     * Wi-Fi isn't set up yet that's the setup screen, otherwise it's the
     * clock, same as before this screen existed. */
    s_current = UI_PAGE_SETUP;
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
    restart_cycle_timer();

    bsp_display_unlock();
}
