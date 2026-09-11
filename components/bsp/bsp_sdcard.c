/**
 * @file bsp_sdcard.c
 * @brief microSD card mounting (SPI mode, FAT filesystem).
 */
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"

#include "bsp/bsp_pins.h"
#include "bsp/bsp_board.h"

static const char *TAG = "bsp_sdcard";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;
static bool s_spi_bus_initialized = false;

esp_err_t bsp_sdcard_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_err_t ret;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BSP_SD_SPI_HOST;
    host.max_freq_khz = BSP_SD_MAX_FREQ_KHZ;

    if (!s_spi_bus_initialized) {
        const spi_bus_config_t bus_cfg = {
            .mosi_io_num = BSP_SD_PIN_MOSI,
            .miso_io_num = BSP_SD_PIN_MISO,
            .sclk_io_num = BSP_SD_PIN_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4000,
        };
        ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
            return ret;
        }
        s_spi_bus_initialized = true;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = BSP_SD_PIN_CS;
    slot_config.host_id = host.slot;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    ret = esp_vfs_fat_sdspi_mount(BSP_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem (is the card formatted as FAT32?)");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card: %s", esp_err_to_name(ret));
        }
        return ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

esp_err_t bsp_sdcard_unmount(void)
{
    if (!s_mounted) {
        return ESP_OK;
    }
    esp_err_t ret = esp_vfs_fat_sdcard_unmount(BSP_SD_MOUNT_POINT, s_card);
    if (ret == ESP_OK) {
        s_mounted = false;
        s_card = NULL;
    }
    return ret;
}

bool bsp_sdcard_is_mounted(void)
{
    return s_mounted;
}
