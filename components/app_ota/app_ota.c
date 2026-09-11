/**
 * @file app_ota.c
 * @brief See app_ota.h. The URL-install path mirrors ESP-IDF's official
 *        `examples/system/ota/advanced_https_ota` example - check that
 *        example first if `esp_https_ota_*` calls ever need updating for a
 *        newer IDF release.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h" /* esp_app_desc_t / ESP_APP_DESC_MAGIC_WORD / esp_app_get_description() */
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

#include "app_ota.h"

static const char *TAG = "app_ota";

/* Must match the `project()` name in the top-level CMakeLists.txt - that
 * string is what ESP-IDF bakes into every image's esp_app_desc_t.project_name. */
#define FIRMWARE_PROJECT_NAME "wt32_firmware"

#define VALIDATE_HEADER_LEN 512
#define GITHUB_JSON_MAX_LEN  8192

static SemaphoreHandle_t s_lock;
static app_ota_status_t s_status;
static bool s_ota_busy;

static esp_ota_handle_t s_upload_handle;
static const esp_partition_t *s_upload_partition;
static size_t s_upload_written;
static bool s_upload_validated;

static bool validate_app_desc(const esp_app_desc_t *desc)
{
    if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD) {
        return false;
    }
    return strncmp(desc->project_name, FIRMWARE_PROJECT_NAME, sizeof(desc->project_name)) == 0;
}

static void set_status(app_ota_state_t state, const char *msg, int pct)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    strncpy(s_status.message, msg, sizeof(s_status.message) - 1);
    s_status.message[sizeof(s_status.message) - 1] = '\0';
    s_status.progress_pct = pct;
    xSemaphoreGive(s_lock);
}

static void clear_busy(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ota_busy = false;
    xSemaphoreGive(s_lock);
}

esp_err_t app_ota_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
    s_status.state = APP_OTA_STATE_IDLE;
    s_status.message[0] = '\0';
    s_status.progress_pct = -1;
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_ota_confirm_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
        if (state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "Boot looks healthy - confirming this image (cancels auto-rollback)");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }
}

void app_ota_get_current_version(char *buf, size_t len)
{
    const esp_app_desc_t *desc = esp_app_get_description();
    strncpy(buf, desc->version, len - 1);
    buf[len - 1] = '\0';
}

bool app_ota_version_is_newer(const char *latest, const char *current)
{
    int lv[3] = {0, 0, 0};
    int cv[3] = {0, 0, 0};
    sscanf(latest, "%d.%d.%d", &lv[0], &lv[1], &lv[2]);
    sscanf(current, "%d.%d.%d", &cv[0], &cv[1], &cv[2]);
    for (int i = 0; i < 3; i++) {
        if (lv[i] != cv[i]) {
            return lv[i] > cv[i];
        }
    }
    return false;
}

void app_ota_get_status(app_ota_status_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

/* ---------------------------------------------------------------------- */
/* GitHub "latest release" lookup                                         */
/* ---------------------------------------------------------------------- */

esp_err_t app_ota_check_github(const char *owner_repo,
                                char *out_version, size_t ver_len,
                                char *out_asset_url, size_t url_len,
                                char *out_notes, size_t notes_len)
{
    char url[192];
    /* owner_repo is always a short "owner/name" string (app_config caps it at
     * APP_CFG_STR_MAX_LEN=64) - GCC can't see that bound from here, so
     * silence the truncation warning instead of restructuring around it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(url, sizeof(url), "https://api.github.com/repos/%s/releases/latest", owner_repo);
#pragma GCC diagnostic pop

    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }
    esp_http_client_set_header(client, "User-Agent", "wt32-ota");
    esp_http_client_set_header(client, "Accept", "application/vnd.github+json");

    esp_err_t ret = esp_http_client_open(client, 0);
    if (ret != ESP_OK) {
        esp_http_client_cleanup(client);
        return ret;
    }
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (status != 200) {
        ESP_LOGW(TAG, "GitHub API returned HTTP %d (repo=\"%s\")", status, owner_repo);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    char *body = malloc(GITHUB_JSON_MAX_LEN);
    if (!body) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    int total = 0;
    while (total < GITHUB_JSON_MAX_LEN - 1) {
        int r = esp_http_client_read(client, body + total, GITHUB_JSON_MAX_LEN - 1 - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }
    body[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        ESP_LOGW(TAG, "GitHub API response was not valid JSON");
        return ESP_FAIL;
    }

    cJSON *tag = cJSON_GetObjectItem(root, "tag_name");
    if (!cJSON_IsString(tag)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }
    const char *tag_str = tag->valuestring;
    const char *version_str = (tag_str[0] == 'v' || tag_str[0] == 'V') ? tag_str + 1 : tag_str;
    strncpy(out_version, version_str, ver_len - 1);
    out_version[ver_len - 1] = '\0';

    if (out_notes && notes_len > 0) {
        out_notes[0] = '\0';
        cJSON *body_field = cJSON_GetObjectItem(root, "body");
        if (cJSON_IsString(body_field)) {
            strncpy(out_notes, body_field->valuestring, notes_len - 1);
            out_notes[notes_len - 1] = '\0';
        }
    }

    out_asset_url[0] = '\0';
    const char *fallback_url = NULL;
    cJSON *assets = cJSON_GetObjectItem(root, "assets");
    if (cJSON_IsArray(assets)) {
        cJSON *asset;
        cJSON_ArrayForEach(asset, assets) {
            cJSON *name = cJSON_GetObjectItem(asset, "name");
            cJSON *dl = cJSON_GetObjectItem(asset, "browser_download_url");
            if (!cJSON_IsString(name) || !cJSON_IsString(dl)) {
                continue;
            }
            if (strcmp(name->valuestring, "firmware.bin") == 0) {
                strncpy(out_asset_url, dl->valuestring, url_len - 1);
                out_asset_url[url_len - 1] = '\0';
                break;
            }
            size_t nlen = strlen(name->valuestring);
            if (!fallback_url && nlen > 4 && strcmp(name->valuestring + nlen - 4, ".bin") == 0) {
                fallback_url = dl->valuestring;
            }
        }
        if (out_asset_url[0] == '\0' && fallback_url) {
            strncpy(out_asset_url, fallback_url, url_len - 1);
            out_asset_url[url_len - 1] = '\0';
        }
    }

    esp_err_t result = out_asset_url[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
    cJSON_Delete(root);
    return result;
}

/* ---------------------------------------------------------------------- */
/* Install from a direct .bin URL (background task)                       */
/* ---------------------------------------------------------------------- */

static void ota_url_task(void *arg)
{
    char *url = (char *)arg;

    set_status(APP_OTA_STATE_DOWNLOADING, "Connecting...", -1);

    esp_http_client_config_t http_config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        set_status(APP_OTA_STATE_ERROR, "Could not reach the download URL", -1);
        goto done;
    }

    esp_app_desc_t desc;
    err = esp_https_ota_get_img_desc(handle, &desc);
    if (err != ESP_OK || !validate_app_desc(&desc)) {
        ESP_LOGW(TAG, "Downloaded image failed the firmware identity check");
        set_status(APP_OTA_STATE_ERROR, "This file is not a WT32 firmware image", -1);
        esp_https_ota_abort(handle);
        goto done;
    }

    set_status(APP_OTA_STATE_WRITING, "Installing update...", 0);
    /* -1 (e.g. chunked-encoded response, no Content-Length) just means we
     * report unknown progress below - esp_https_ota_perform() itself does
     * not need the total size to keep working. */
    int total_len = esp_https_ota_get_image_size(handle);

    while (1) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        int read_len = esp_https_ota_get_image_len_read(handle);
        int pct = (total_len > 0) ? (int)(((int64_t)read_len * 100) / total_len) : -1;
        set_status(APP_OTA_STATE_WRITING, "Installing update...", pct);
    }

    if (err != ESP_OK || !esp_https_ota_is_complete_data_received(handle)) {
        ESP_LOGW(TAG, "OTA download incomplete: %s", esp_err_to_name(err));
        set_status(APP_OTA_STATE_ERROR, "Download incomplete - please try again", -1);
        esp_https_ota_abort(handle);
        goto done;
    }

    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_https_ota_finish failed: %s", esp_err_to_name(err));
        set_status(APP_OTA_STATE_ERROR, "Failed to finalize the update", -1);
        goto done;
    }

    set_status(APP_OTA_STATE_SUCCESS, "Update installed - restarting...", 100);
    vTaskDelay(pdMS_TO_TICKS(1500));
    esp_restart();

done:
    clear_busy();
    free(url);
    vTaskDelete(NULL);
}

esp_err_t app_ota_start_from_url(const char *url)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_ota_busy) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_ota_busy = true;
    xSemaphoreGive(s_lock);

    char *url_copy = strdup(url);
    if (!url_copy) {
        clear_busy();
        return ESP_ERR_NO_MEM;
    }

    set_status(APP_OTA_STATE_DOWNLOADING, "Starting...", -1);
    if (xTaskCreate(ota_url_task, "ota_url", 8192, url_copy, tskIDLE_PRIORITY + 3, NULL) != pdPASS) {
        free(url_copy);
        clear_busy();
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* Manual upload (streamed by the caller from an HTTP request body)       */
/* ---------------------------------------------------------------------- */

esp_err_t app_ota_upload_begin(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_ota_busy) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_ota_busy = true;
    xSemaphoreGive(s_lock);

    s_upload_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_upload_partition) {
        ESP_LOGE(TAG, "No OTA update partition available");
        clear_busy();
        return ESP_FAIL;
    }

    s_upload_written = 0;
    s_upload_validated = false;

    esp_err_t ret = esp_ota_begin(s_upload_partition, OTA_SIZE_UNKNOWN, &s_upload_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(ret));
        clear_busy();
        return ret;
    }

    set_status(APP_OTA_STATE_WRITING, "Uploading...", 0);
    return ESP_OK;
}

esp_err_t app_ota_upload_write(const uint8_t *data, size_t len)
{
    esp_err_t ret = esp_ota_write(s_upload_handle, data, len);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "esp_ota_write failed: %s (is this a valid ESP32 image?)", esp_err_to_name(ret));
        return ret;
    }
    s_upload_written += len;

    if (!s_upload_validated && s_upload_written >= VALIDATE_HEADER_LEN) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(s_upload_partition, &desc) != ESP_OK || !validate_app_desc(&desc)) {
            ESP_LOGW(TAG, "Uploaded image failed the firmware identity check");
            return ESP_ERR_INVALID_ARG;
        }
        s_upload_validated = true;
        ESP_LOGI(TAG, "Uploaded image identity OK (version %s)", desc.version);
    }
    return ESP_OK;
}

esp_err_t app_ota_upload_finish(void)
{
    if (!s_upload_validated) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(s_upload_partition, &desc) != ESP_OK || !validate_app_desc(&desc)) {
            ESP_LOGW(TAG, "Uploaded image failed the firmware identity check");
            esp_ota_abort(s_upload_handle);
            clear_busy();
            return ESP_ERR_INVALID_ARG;
        }
    }

    esp_err_t ret = esp_ota_end(s_upload_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(ret));
        clear_busy();
        return ret;
    }

    ret = esp_ota_set_boot_partition(s_upload_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(ret));
    } else {
        set_status(APP_OTA_STATE_SUCCESS, "Update installed - restarting...", 100);
    }
    clear_busy();
    return ret;
}

void app_ota_upload_abort(void)
{
    if (s_upload_handle) {
        esp_ota_abort(s_upload_handle);
        s_upload_handle = 0;
    }
    clear_busy();
}
