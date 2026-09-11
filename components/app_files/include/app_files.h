/**
 * @file app_files.h
 * @brief SD-card file manager backing the web UI's "Files" tab: list,
 *        delete, rename and create folders. Upload/download are streamed
 *        directly in app_web.c's HTTP handlers (they don't need to buffer a
 *        whole file in RAM), using app_files_resolve() for path safety.
 *
 * Every path taken by this API is SD-card-relative ("/" is the SD card
 * root, e.g. "/photos/a.jpg") - never a full filesystem path.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_FILES_NAME_MAX 128
#define APP_FILES_PATH_MAX 512

typedef struct {
    char   name[APP_FILES_NAME_MAX];
    bool   is_dir;
    size_t size;
} app_files_entry_t;

/**
 * @brief Resolve an SD-card-relative path into a full filesystem path
 *        (prefixed with the SD card's mount point), rejecting anything
 *        malformed or trying to escape the mount point via "..".
 *
 * @return true if rel_path was valid and out_abs was filled in.
 */
bool app_files_resolve(const char *rel_path, char *out_abs, size_t out_abs_len);

/** List the immediate children of a directory. *out_count is always set,
 *  even on a truncated (max_entries-limited) listing. */
esp_err_t app_files_list(const char *rel_dir, app_files_entry_t *out, size_t max_entries, size_t *out_count);

/** Delete a file, or an empty directory. */
esp_err_t app_files_delete(const char *rel_path);

/** Rename/move a file or directory within the SD card. */
esp_err_t app_files_rename(const char *rel_from, const char *rel_to);

/** Create a new, empty directory. */
esp_err_t app_files_mkdir(const char *rel_path);

#ifdef __cplusplus
}
#endif
