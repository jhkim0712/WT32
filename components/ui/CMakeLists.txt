idf_component_register(
    SRCS
        "ui.c"
        "ui_clock.c"
        "ui_album.c"
        "ui_info.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        lvgl
        bsp
        app_config
        app_time
        app_photo
        app_wifi
        app_ota
        esp_timer
        esp_system
)
