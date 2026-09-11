/**
 * @file bsp_display.c
 * @brief Display (ST7796, 8080/i80 parallel bus) + capacitive touch (FT6336U)
 *        bring-up, wired into LVGL through esp_lvgl_port.
 *
 * Targets ESP-IDF >= 5.2 (new `driver/i2c_master.h` I2C driver and the
 * `esp_lcd_new_i80_bus()` / `esp_lcd_new_panel_io_i80()` APIs). If the
 * physical image comes up mirrored, rotated or color-inverted, that is a
 * per-panel-batch quirk - adjust the esp_lcd_panel_* calls near the bottom
 * of bsp_display_start(), not the pin map.
 */
#include <string.h>
#include "esp_check.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"

#include "bsp/bsp_pins.h"
#include "bsp/bsp_board.h"

static const char *TAG = "bsp_display";

#define BSP_LCD_DRAW_BUF_LINES  60  /* partial LVGL draw buffer, in scanlines */
#define BSP_LCD_PIXEL_CLK_HZ    (10 * 1000 * 1000)

#define BSP_BL_LEDC_TIMER       LEDC_TIMER_0
#define BSP_BL_LEDC_MODE        LEDC_LOW_SPEED_MODE
#define BSP_BL_LEDC_CHANNEL     LEDC_CHANNEL_0
#define BSP_BL_LEDC_DUTY_RES    LEDC_TIMER_8_BIT
#define BSP_BL_LEDC_FREQ_HZ     5000

static lv_display_t *s_disp = NULL;

static void bsp_backlight_init(void)
{
    const ledc_timer_config_t timer_cfg = {
        .speed_mode = BSP_BL_LEDC_MODE,
        .timer_num = BSP_BL_LEDC_TIMER,
        .duty_resolution = BSP_BL_LEDC_DUTY_RES,
        .freq_hz = BSP_BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    const ledc_channel_config_t ch_cfg = {
        .gpio_num = BSP_LCD_PIN_BL,
        .speed_mode = BSP_BL_LEDC_MODE,
        .channel = BSP_BL_LEDC_CHANNEL,
        .timer_sel = BSP_BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

void bsp_display_set_backlight(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    uint32_t max_duty = (1 << BSP_BL_LEDC_DUTY_RES) - 1;
    uint32_t duty = (max_duty * percent) / 100;
    ledc_set_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL, duty);
    ledc_update_duty(BSP_BL_LEDC_MODE, BSP_BL_LEDC_CHANNEL);
}

static esp_lcd_panel_io_handle_t bsp_lcd_new_io(esp_lcd_i80_bus_handle_t bus)
{
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = -1, /* no CS line on this board */
        .pclk_hz = BSP_LCD_PIXEL_CLK_HZ,
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(bus, &io_config, &io_handle));
    return io_handle;
}

static esp_lcd_i80_bus_handle_t bsp_lcd_new_bus(void)
{
    esp_lcd_i80_bus_handle_t bus = NULL;
    const esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num = BSP_LCD_PIN_DC,
        .wr_gpio_num = BSP_LCD_PIN_WR,
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums = {
            BSP_LCD_PIN_D0, BSP_LCD_PIN_D1, BSP_LCD_PIN_D2, BSP_LCD_PIN_D3,
            BSP_LCD_PIN_D4, BSP_LCD_PIN_D5, BSP_LCD_PIN_D6, BSP_LCD_PIN_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = BSP_LCD_H_RES * BSP_LCD_DRAW_BUF_LINES * sizeof(uint16_t),
        .psram_trans_align = 64,
        .sram_trans_align = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &bus));
    return bus;
}

static i2c_master_bus_handle_t bsp_i2c_init(void)
{
    i2c_master_bus_handle_t bus_handle = NULL;
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BSP_TP_I2C_PORT,
        .sda_io_num = BSP_TP_PIN_SDA,
        .scl_io_num = BSP_TP_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    return bus_handle;
}

static esp_lcd_touch_handle_t bsp_touch_init(i2c_master_bus_handle_t i2c_bus)
{
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_FT5x06_CONFIG();
    tp_io_config.scl_speed_hz = BSP_TP_I2C_CLK_HZ;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = -1, /* already reset together with the LCD */
        .int_gpio_num = BSP_TP_PIN_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };

    esp_lcd_touch_handle_t tp_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle));
    return tp_handle;
}

lv_display_t *bsp_display_start(void)
{
    ESP_LOGI(TAG, "Initializing backlight");
    bsp_backlight_init();
    bsp_display_set_backlight(0);

    ESP_LOGI(TAG, "Initializing i80 bus + ST7796 panel");
    esp_lcd_i80_bus_handle_t i80_bus = bsp_lcd_new_bus();
    esp_lcd_panel_io_handle_t io_handle = bsp_lcd_new_io(i80_bus);

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
    };
    esp_lcd_panel_handle_t panel_handle = NULL;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Initializing touch controller (FT6336U)");
    i2c_master_bus_handle_t i2c_bus = bsp_i2c_init();
    esp_lcd_touch_handle_t tp_handle = bsp_touch_init(i2c_bus);

    ESP_LOGI(TAG, "Starting LVGL port");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = BSP_LCD_H_RES * BSP_LCD_DRAW_BUF_LINES,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .swap_bytes = true,
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = tp_handle,
    };
    lvgl_port_add_touch(&touch_cfg);

    bsp_display_set_backlight(80);
    ESP_LOGI(TAG, "Display ready (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return s_disp;
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
