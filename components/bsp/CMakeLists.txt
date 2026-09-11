idf_component_register(
    SRCS
        "bsp_display.c"
        "bsp_sdcard.c"
        "bsp_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        driver
        esp_lcd
        fatfs
        sdmmc
        esp_timer
        lvgl
        esp_lvgl_port
        esp_lcd_touch_ft5x06
        esp_lcd_st7796
)
