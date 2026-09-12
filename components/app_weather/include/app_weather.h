/**
 * @file app_weather.h
 * @brief OpenWeatherMap current-conditions fetcher for the weather screen.
 *
 * Polls https://api.openweathermap.org on a background task at a fixed
 * interval, whenever weather is enabled and both an API key and a city ID
 * are configured (see app_config.h). The last successfully fetched reading
 * is kept around (and shown as "stale") if a later poll fails, so a
 * transient network hiccup doesn't blank the screen.
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_WEATHER_DESC_MAX 48
#define APP_WEATHER_CITY_NAME_MAX 48
#define APP_WEATHER_ERROR_MAX 48

typedef struct {
    bool    valid;        /* true once at least one fetch has ever succeeded */
    bool    have_error;   /* true if the most recent fetch attempt failed */
    char    error[APP_WEATHER_ERROR_MAX]; /* short reason, valid when have_error */

    char    description[APP_WEATHER_DESC_MAX]; /* e.g. "light rain" */
    char    city_name[APP_WEATHER_CITY_NAME_MAX];
    int     condition_id; /* OpenWeatherMap "weather[0].id" condition code, e.g. 800 = clear sky -
                            * see https://openweathermap.org/weather-conditions */
    bool    is_day;       /* from "weather[0].icon"'s day/night suffix ('d'/'n') */
    float   temp_c;
    float   feels_like_c;
    float   temp_min_c;
    float   temp_max_c;
    int     humidity_pct;
    float   wind_speed_ms;
    time_t  updated_at;   /* local time of the last successful fetch */
} app_weather_data_t;

/** Start the background polling task. Call once, after app_config_load()
 *  and app_wifi_start() (fetches are skipped while Wi-Fi is disconnected). */
esp_err_t app_weather_start(void);

/** Copy out the last known reading (zeroed/valid=false if never fetched). */
void app_weather_get(app_weather_data_t *out);

/** Ask the background task to fetch immediately instead of waiting for the
 *  next interval - call after the API key / city ID / enabled flag change
 *  in the web UI so the change is reflected right away. */
void app_weather_request_refresh(void);

#ifdef __cplusplus
}
#endif
