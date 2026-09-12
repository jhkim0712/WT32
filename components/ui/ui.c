#include "ui.h"
#include "ui_internal.h"
#include "bsp/bsp_board.h"
#include "app_config.h"

#define UI_PAGE_COUNT 4

static lv_obj_t *s_screens[UI_PAGE_COUNT];
static void (*s_on_show[UI_PAGE_COUNT])(void);
/* Optional per-page visibility check - NULL means "always visible". Pages
 * that can be turned off from the web UI (currently just Weather) are
 * skipped over when cycling/swiping instead of being removed from the
 * array, so no other index needs to shift around at runtime. */
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

    s_screens[0] = ui_clock_create();
    s_on_show[0] = ui_clock_on_show;
    s_screens[1] = ui_album_create();
    s_on_show[1] = ui_album_on_show;
    s_screens[2] = ui_weather_create();
    s_on_show[2] = ui_weather_on_show;
    s_page_visible[2] = ui_weather_is_visible;
    s_screens[3] = ui_info_create();
    s_on_show[3] = ui_info_on_show;

    for (int i = 0; i < UI_PAGE_COUNT; i++) {
        lv_obj_add_flag(s_screens[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s_screens[i], gesture_event_cb, LV_EVENT_GESTURE, NULL);
    }

    lv_screen_load(s_screens[0]);
    s_current = 0;
    if (s_on_show[0]) {
        s_on_show[0]();
    }

    app_config_t *cfg = app_config_get();
    uint32_t period_ms = cfg->cycle_seconds > 0 ? (uint32_t)cfg->cycle_seconds * 1000 : 10000;
    s_cycle_timer = lv_timer_create(cycle_timer_cb, period_ms, NULL);

    bsp_display_unlock();
}
