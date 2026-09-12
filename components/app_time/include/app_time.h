/**
 * @file app_time.h
 * @brief Timezone + SNTP wall-clock time for the clock screen.
 *
 * There is no battery-backed RTC chip on the WT32-SC01 Plus, so the clock is
 * only accurate once SNTP has synced at least once after boot (a full power
 * loss forgets the time - see README "Known limitations").
 */
#pragma once

#include <time.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Apply the configured POSIX TZ string and start the SNTP client (non-blocking). */
esp_err_t app_time_start(void);

/** Change the active timezone at runtime (e.g. after the user edits it in the web UI). */
void app_time_set_timezone(const char *tz_posix);

/** Restart SNTP against a (possibly new) server, e.g. after a config change. */
void app_time_set_server(const char *ntp_server);

/**
 * @return true once at least one successful SNTP sync has completed.
 *
 * Stays true afterwards (including across the periodic background resyncs
 * SNTP keeps doing every ~1h) - this is a latch, not a live "currently
 * mid-sync" status, so it's safe to poll from UI code without it flapping
 * back to false between resyncs.
 */
bool app_time_is_synced(void);

/** Fill @p out with the current local (timezone-adjusted) time. */
void app_time_get_local(struct tm *out);

#ifdef __cplusplus
}
#endif
