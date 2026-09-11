/**
 * @file ui.h
 * @brief LVGL screen manager: Clock / Photo Album / Device Info, navigated
 *        by swipe gesture or automatic cycling (per app_config).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Build all screens and load the first one. Call after bsp_display_start(). */
void ui_init(void);

/** Programmatically advance to the next / previous screen (wraps around). */
void ui_next_screen(void);
void ui_prev_screen(void);

#ifdef __cplusplus
}
#endif
