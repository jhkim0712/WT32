/**
 * @file bsp_display.c
 * @brief Display (ST7796, 8080/i80 parallel bus) + capacitive touch (FT6336U)
 *        bring-up, wired into LVGL through esp_lvgl_port.
 *
 * Targets ESP-IDF >= 6.0: the new `driver/i2c_master.h` I2C driver, the
 * `esp_lcd_new_i80_bus()` / `esp_lcd_new_panel_io_i80()` APIs, and the
 * post-v6.0 struct shapes (`esp_lcd_i80_bus_config_t::dma_burst_size`,
 * `esp_lcd_panel_dev_config_t::rgb_ele_order`). If the physical image comes
 * up mirrored, rotated or color-inverted, that is a per-panel-batch quirk -
 * adjust the esp_lcd_panel_* calls near the bottom of bsp_display_start(),
 * not the pin map.
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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

/* Display bring-up log (see README "Troubleshooting the display" for the
 * plain-language version):
 *   - PCLK_ACTIVE_NEG=1: blank/nothing regardless of other settings ->
 *     reverted to 0 (confirmed correct: wrong edge polarity breaks
 *     communication outright, rather than just misrendering).
 *   - Clock speed (10 -> 6 -> 2 MHz) and swap_color_bytes vs. software
 *     swap_bytes: no effect either way on the remaining symptom (regular
 *     repeating stripes, content faintly legible) -> not a signal-timing or
 *     byte-order issue, ruled out.
 *   - swap_xy=true: content became vertically-oriented text (wrong reading
 *     direction) while the striping was unchanged either way -> reverted to
 *     false; orientation and the striping are two separate problems.
 *   - Current suspect: the 60-scanline *partial* LVGL draw buffer / its
 *     double-buffer flush timing with the i80 DMA path - repeating "every
 *     N scan lines" corruption that doesn't depend on clock speed or pixel
 *     format is the signature of a buffer-reuse/flush-sync issue, not a bus
 *     problem. Testing a full-frame buffer (below) to confirm. */
#define BSP_LCD_DRAW_BUF_LINES    BSP_LCD_V_RES /* full frame - see log above; if this fixes it,
                                                  * a partial buffer can be reintroduced later to
                                                  * save PSRAM once the flush-sync issue is found */
#define BSP_LCD_PIXEL_CLK_HZ      (10 * 1000 * 1000) /* clock speed is confirmed not the cause - back to normal */
#define BSP_LCD_PCLK_ACTIVE_NEG   0

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
        .flags = {
            /* Reverted: ESP-IDF's own i80 driver applies this swizzle to
             * *every* transaction it sends - including the ST7796 init
             * command/parameter bytes, not just color data despite what its
             * own source comment claims - so turning it on can corrupt the
             * init sequence itself (panel stays blank/asleep). Byte-swapping
             * is done in software instead, only on the color buffer, via
             * lvgl_port_display_cfg_t.flags.swap_bytes below. */
            .swap_color_bytes = 0,
            .pclk_active_neg = BSP_LCD_PCLK_ACTIVE_NEG,
        },
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
        .dma_burst_size = 64, /* ESP-IDF >= v6.0: replaces the old psram/sram_trans_align pair */
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

/**
 * @brief TEMPORARY bring-up diagnostic: draw 5 solid color bars directly
 *        through esp_lcd (no LVGL, no esp_lvgl_port involved at all) as a
 *        single full-frame transfer, and hold them on screen for a while.
 *
 * v1 of this test issued one esp_lcd_panel_draw_bitmap() call *per scan
 * line* (320 separate CASET/RASET/RAMWR command sequences) and showed a
 * static band covering a consistent fraction of the screen, with clean,
 * correctly-shaped color bars everywhere else. Uniform content (every row
 * was identical) ruling out a content/addressing bug, plus that fraction
 * being suspiciously consistent, points at tearing: the panel's own
 * internal GRAM-to-glass refresh reading out a frame we hadn't finished
 * writing yet - unsurprising given 320 separate command round-trips is far
 * slower than one continuous burst, and LCD_TE (frame sync) is left
 * unused/floating (see bsp_pins.h) instead of being used to time writes to
 * the panel's vertical blanking interval.
 *
 * This version sends the whole frame in ONE call instead, matching how the
 * real LVGL flush path already works (full-frame double buffer, see
 * BSP_LCD_DRAW_BUF_LINES above) - if a single fast burst still shows the
 * same static band, tearing/write-speed is confirmed as the cause even for
 * the real UI, and LCD_TE needs to be wired into the flush path properly.
 * If this version comes up clean, the earlier per-row test was simply too
 * slow to be representative, and remaining LVGL-side corruption has some
 * other, still-unknown cause.
 *
 * Delete this function and its call site in bsp_display_start() once the
 * display is working - it has no purpose beyond this bring-up.
 */
static void bsp_display_test_pattern(esp_lcd_panel_handle_t panel)
{
    const uint16_t bar_colors[] = {
        0xF800, /* red   */
        0x07E0, /* green */
        0x001F, /* blue  */
        0xFFFF, /* white */
        0x0000, /* black */
    };
    const int num_bars = sizeof(bar_colors) / sizeof(bar_colors[0]);
    const int bar_w = BSP_LCD_H_RES / num_bars;

    uint16_t *frame = malloc((size_t)BSP_LCD_H_RES * BSP_LCD_V_RES * sizeof(uint16_t));
    if (!frame) {
        ESP_LOGE(TAG, "test pattern: out of memory");
        return;
    }
    for (int y = 0; y < BSP_LCD_V_RES; y++) {
        uint16_t *row = frame + (size_t)y * BSP_LCD_H_RES;
        for (int x = 0; x < BSP_LCD_H_RES; x++) {
            row[x] = bar_colors[(x / bar_w) % num_bars];
        }
    }

    ESP_LOGW(TAG, "Showing bring-up test pattern for 6s (see bsp_display_test_pattern)");
    esp_lcd_panel_draw_bitmap(panel, 0, 0, BSP_LCD_H_RES, BSP_LCD_V_RES, frame);
    vTaskDelay(pdMS_TO_TICKS(6000));
    free(frame);
}

lv_display_t *bsp_display_start(void)
{
    /* LCD_TE isn't used by the i80 driver, but leave it as a defined,
     * unpulled input rather than in its power-on-reset floating state -
     * cheap insurance against noise coupling into the neighboring data
     * lines while the panel drives it. */
    gpio_set_direction(BSP_LCD_PIN_TE, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BSP_LCD_PIN_TE, GPIO_FLOATING);

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
    /* Found it (probably): ST7796 is natively a 320-column x 480-row
     * controller. With swap_xy=false we were sending CASET (column) values
     * up to 479 - 160 columns *past* the controller's real 320-column
     * limit - to a controller whose behavior for out-of-range addresses is
     * undefined. That's a fixed, address-range bug, not a timing one,
     * which fits every experiment so far: identical regardless of PCLK
     * speed/polarity or single-call vs per-row writes, and a consistently
     * sized/positioned bad region.
     *
     * swap_xy=true makes the driver send OUR x (0-479) as RASET (native
     * row, max 480 - fits) and OUR y (0-319) as CASET (native column, max
     * 320 - fits), with no caller-side changes needed (the controller's
     * MADCTL MV bit handles reinterpreting the same pixel stream order).
     * Confirmed: this alone fixed the static/noise completely. With
     * mirror_x=true the image was clean and right-side up, just mirrored
     * left-right, so mirror_x=false below removes exactly that. */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    bsp_display_set_backlight(80); /* needs to be on to see the test pattern below */
    bsp_display_test_pattern(panel_handle); /* TEMPORARY - see its own doc comment */

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
        /* IMPORTANT: esp_lvgl_port calls esp_lcd_panel_swap_xy()/mirror()
         * itself, using *this* config, once lvgl_port_add_disp() runs below
         * - which overwrites/undoes the esp_lcd_panel_swap_xy()/mirror()
         * calls made earlier in this function (those only matter for the
         * bring-up test pattern that runs before this point). This must
         * match the values passed to esp_lcd_panel_swap_xy()/mirror() above
         * or LVGL's actual rendering silently reverts to the un-rotated,
         * out-of-CASET-range addressing that caused the static/noise. */
        .rotation = {
            .swap_xy = true,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,
            .buff_spiram = true,
            .swap_bytes = true, /* software swap of the color buffer only - see the long comment on
                                  * .swap_color_bytes in bsp_lcd_new_io() for why that (hardware)
                                  * alternative was reverted. */
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
