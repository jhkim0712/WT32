idf_component_register(
    SRCS
        "bsp_display.c"
        "bsp_sdcard.c"
        "bsp_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        # ESP-IDF >= 6.0 split most of the old "driver" component apart -
        # each peripheral header now comes from its own esp_driver_xxx
        # component instead of being pulled in transitively. "driver"
        # itself is kept too (harmless) as a safety net for anything still
        # routed through it (e.g. chip-specific soc/ macros).
        driver
        esp_driver_gpio    # driver/gpio.h
        esp_driver_ledc    # driver/ledc.h (backlight PWM)
        esp_driver_i2c     # driver/i2c_master.h (touch controller)
        esp_driver_i2s     # driver/i2s_std.h (audio amplifier)
        esp_driver_spi     # driver/spi_common.h
        esp_driver_sdspi   # driver/sdspi_host.h (microSD over SPI)
        sdmmc              # sdmmc_cmd.h
        fatfs              # esp_vfs_fat.h
        esp_lcd            # esp_lcd_panel_io.h / esp_lcd_panel_ops.h / i80 bus
        esp_timer
        lvgl
        esp_lvgl_port
        esp_lcd_touch_ft5x06
        esp_lcd_st7796
)
