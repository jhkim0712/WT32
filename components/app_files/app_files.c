#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_log.h"

#include "bsp/bsp_pins.h"
#include "app_files.h"

static const char *TAG = "app_files";

bool app_files_resolve(const char *rel_path, char *out_abs, size_t out_abs_len)
{
    if (!rel_path || rel_path[0] != '/') {
        return false;
    }
    if (strstr(rel_path, "..") != NULL) {
        return false; /* no escaping the SD card mount point */
    }
    /* Truncation (rel_path too long for out_abs_len) is checked via the
     * returned length right below, not relied upon to never happen. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    int n = snprintf(out_abs, out_abs_len, "%s%s", BSP_SD_MOUNT_POINT, rel_path);
#pragma GCC diagnostic pop
    return (n > 0 && (size_t)n < out_abs_len);
}

esp_err_t app_files_list(const char *rel_dir, app_files_entry_t *out, size_t max_entries, size_t *out_count)
{
    char abs_dir[APP_FILES_PATH_MAX];
    if (!app_files_resolve(rel_dir, abs_dir, sizeof(abs_dir))) {
        return ESP_ERR_INVALID_ARG;
    }

    DIR *dir = opendir(abs_dir);
    if (!dir) {
        ESP_LOGW(TAG, "opendir(%s) failed", abs_dir);
        return ESP_ERR_NOT_FOUND;
    }

    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < max_entries) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        /* GCC can't prove abs_dir + d_name always fits APP_FILES_PATH_MAX (it
         * can't see the caller only ever passes short SD-card-relative
         * paths) - safe truncation is fine here, so silence the warning
         * instead of restructuring around a limit that isn't really at risk. */
        char child_abs[APP_FILES_PATH_MAX];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
        snprintf(child_abs, sizeof(child_abs), "%s/%s", abs_dir, entry->d_name);
#pragma GCC diagnostic pop

        bool is_dir = (entry->d_type == DT_DIR);
        size_t size = 0;
        struct stat st;
        if (stat(child_abs, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size = (size_t)st.st_size;
        }

        strncpy(out[count].name, entry->d_name, sizeof(out[count].name) - 1);
        out[count].name[sizeof(out[count].name) - 1] = '\0';
        out[count].is_dir = is_dir;
        out[count].size = size;
        count++;
    }
    closedir(dir);

    *out_count = count;
    return ESP_OK;
}

esp_err_t app_files_delete(const char *rel_path)
{
    char abs_path[APP_FILES_PATH_MAX];
    if (!app_files_resolve(rel_path, abs_path, sizeof(abs_path))) {
        return ESP_ERR_INVALID_ARG;
    }

    struct stat st;
    if (stat(abs_path, &st) != 0) {
        return ESP_ERR_NOT_FOUND;
    }

    int ret = S_ISDIR(st.st_mode) ? rmdir(abs_path) : remove(abs_path);
    if (ret != 0) {
        ESP_LOGW(TAG, "delete %s failed", abs_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_files_rename(const char *rel_from, const char *rel_to)
{
    char abs_from[APP_FILES_PATH_MAX];
    char abs_to[APP_FILES_PATH_MAX];
    if (!app_files_resolve(rel_from, abs_from, sizeof(abs_from)) ||
        !app_files_resolve(rel_to, abs_to, sizeof(abs_to))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (rename(abs_from, abs_to) != 0) {
        ESP_LOGW(TAG, "rename %s -> %s failed", abs_from, abs_to);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t app_files_mkdir(const char *rel_path)
{
    char abs_path[APP_FILES_PATH_MAX];
    if (!app_files_resolve(rel_path, abs_path, sizeof(abs_path))) {
        return ESP_ERR_INVALID_ARG;
    }

    if (mkdir(abs_path, 0775) != 0) {
        ESP_LOGW(TAG, "mkdir %s failed", abs_path);
        return ESP_FAIL;
    }
    return ESP_OK;
}
