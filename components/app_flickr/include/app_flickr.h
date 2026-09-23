/**
 * @file app_flickr.h
 * @brief Mirrors the images of Flickr photo feeds onto the SD card so the
 *        photo album shows them.
 *
 * For every feed URL in app_config's flickr_feeds (RSS 2.0 or Atom - e.g.
 * https://www.flickr.com/services/feeds/photos_public.gne?id=<user-id>&format=rss2),
 * a background task periodically downloads the feed, then downloads each
 * item's image into its own folder, /photos/flickr/<feed-hash>/, where the
 * album's recursive scan (app_photo_scan()) picks it up like any other
 * photo. The folder is a mirror of the feed: images that drop out of the
 * feed are deleted, as is a whole folder once its feed is removed from the
 * config - so nothing else should be stored under /photos/flickr.
 *
 * Images are fetched at Flickr's 640px ("_z") size where the URL allows it,
 * rather than the feed's default 1024px, to keep downloads small and
 * decodes cheap on a 480x320 panel.
 */
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FLICKR_ERROR_MAX 64

typedef struct {
    bool   syncing;         /* a sync is running right now */
    bool   have_error;      /* the most recent sync had at least one failure */
    char   error[APP_FLICKR_ERROR_MAX]; /* short reason, valid when have_error */
    time_t last_sync;       /* when the most recent sync finished (0 = never) */
    int    image_count;     /* Flickr images on the SD card after that sync */
} app_flickr_status_t;

/** Start the background sync task. Call once, after app_config_load(),
 *  app_wifi_start() and the SD card mount/initial photo scan. */
esp_err_t app_flickr_start(void);

/** Sync right away instead of waiting for the next interval - call after
 *  the feed list changes so new feeds show up (and removed ones disappear)
 *  without a long wait. */
void app_flickr_request_sync(void);

/** Copy out the current sync status. */
void app_flickr_get_status(app_flickr_status_t *out);

#ifdef __cplusplus
}
#endif
