idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bsp
        app_config
        app_wifi
        app_time
        app_web
        app_photo
        app_weather
        app_ota
        ui
)
