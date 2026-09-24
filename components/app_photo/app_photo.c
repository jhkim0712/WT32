#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_log.h"
#include "esp_random.h"

#include "app_photo.h"
#include "app_photo_internal.h"

static const char *TAG = "app_photo";

#define APP_PHOTO_MAX_ENTRIES 400
/* Guards scan_dir()'s recursion against a pathological (or symlink-cycle-like,
 * were FAT to ever have such a thing) directory structure - deep enough for
 * any real photo folder layout, shallow enough that a runaway nest can't
 * overflow the stack. */
#define APP_PHOTO_MAX_SCAN_DEPTH 8

static char (*s_paths)[APP_PHOTO_PATH_MAX] = NULL;
static size_t s_capacity = 0;
static size_t s_count = 0;
static size_t *s_order = NULL; /* playback order, indexes into s_paths */

static bool has_extension(const char *name, const char *ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    if (name_len < ext_len) {
        return false;
    }
    return strcasecmp(name + (name_len - ext_len), ext) == 0;
}

static bool is_supported_image(const char *name)
{
    return has_extension(name, ".bmp") || has_extension(name, ".jpg") || has_extension(name, ".jpeg") ||
           has_extension(name, ".png") || has_extension(name, ".gif");
}

bool app_photo_is_gif(const char *path)
{
    return has_extension(path, ".gif");
}

/* Recurses into dir_path, appending every supported image file found - directly
 * in it or in any subdirectory, to any depth up to APP_PHOTO_MAX_SCAN_DEPTH -
 * to s_paths/s_order. Silently stops adding once s_count hits s_capacity, same
 * as the old flat scan did; a missing/unopenable dir_path (which for a
 * subdirectory just means "not a real problem", unlike the top-level call in
 * app_photo_scan()) is likewise silently skipped rather than logged. */
static void scan_dir(const char *dir_path, int depth)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && s_count < s_capacity) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* GCC can't prove dir_path + "/" + d_name always fits
         * APP_PHOTO_PATH_MAX - safe truncation is fine here (a truncated path
         * just fails to open later), so silence the warning instead of
         * restructuring around a limit that isn't really at risk for any
         * realistic folder nesting. */
        char path[APP_PHOTO_PATH_MAX];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);
#pragma GCC diagnostic pop

        if (entry->d_type == DT_DIR) {
            if (depth + 1 < APP_PHOTO_MAX_SCAN_DEPTH) {
                scan_dir(path, depth + 1);
            }
            continue;
        }

        if (!is_supported_image(entry->d_name)) {
            continue;
        }

        strncpy(s_paths[s_count], path, APP_PHOTO_PATH_MAX - 1);
        s_paths[s_count][APP_PHOTO_PATH_MAX - 1] = '\0';
        s_order[s_count] = s_count;
        s_count++;
    }
    closedir(dir);
}

esp_err_t app_photo_scan(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGW(TAG, "Could not open %s (SD card not mounted, or folder missing)", dir_path);
        s_count = 0;
        return ESP_ERR_NOT_FOUND;
    }
    closedir(dir);

    if (!s_paths) {
        s_capacity = APP_PHOTO_MAX_ENTRIES;
        s_paths = malloc(s_capacity * APP_PHOTO_PATH_MAX);
        s_order = malloc(s_capacity * sizeof(size_t));
        if (!s_paths || !s_order) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_count = 0;
    scan_dir(dir_path, 0);

    ESP_LOGI(TAG, "Found %u image(s) in %s (including subfolders)", (unsigned)s_count, dir_path);
    return ESP_OK;
}

size_t app_photo_count(void)
{
    return s_count;
}

const char *app_photo_get_path(size_t index)
{
    if (index >= s_count) {
        return NULL;
    }
    return s_paths[s_order[index]];
}

void app_photo_shuffle(void)
{
    if (s_count < 2) {
        return;
    }
    for (size_t shuffle_i = s_count - 1; shuffle_i > 0; shuffle_i--) {
        size_t swap_j = esp_random() % (shuffle_i + 1);
        size_t tmp = s_order[shuffle_i];
        s_order[shuffle_i] = s_order[swap_j];
        s_order[swap_j] = tmp;
    }
}

void app_photo_unshuffle(void)
{
    for (size_t idx = 0; idx < s_count; idx++) {
        s_order[idx] = idx;
    }
}

esp_err_t app_photo_decode_to_canvas(size_t index, uint16_t *canvas, uint16_t canvas_w, uint16_t canvas_h)
{
    const char *path = app_photo_get_path(index);
    if (!path) {
        return ESP_ERR_INVALID_ARG;
    }
    /* Debug, not info: this fires every slideshow tick (every few seconds),
     * so it would spam the default log level - but printed before any
     * decode work starts, so with debug logging enabled a crash/hang/OOM
     * right after this line still identifies which file caused it. */
    ESP_LOGD(TAG, "Decoding %s (%u/%u)", path, (unsigned)(index + 1), (unsigned)s_count);

    esp_err_t ret;
    if (has_extension(path, ".bmp") || has_extension(path, ".png")) {
        uint16_t *native = NULL;
        uint16_t native_w = 0, native_h = 0;
        if (has_extension(path, ".bmp")) {
            ret = app_photo_bmp_decode_native(path, &native, &native_w, &native_h);
        } else {
            ret = app_photo_png_decode_native(path, &native, &native_w, &native_h);
        }
        if (ret == ESP_OK) {
            app_photo_resize_rgb565(native, native_w, native_h, canvas, canvas_w, canvas_h);
            free(native);
        }
    } else {
        ret = app_photo_jpeg_decode_to_canvas(path, canvas, canvas_w, canvas_h);
    }

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to decode %s: %s", path, esp_err_to_name(ret));
    }
    return ret;
}
