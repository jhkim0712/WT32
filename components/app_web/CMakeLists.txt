idf_component_register(
    SRCS "app_web.c"
    INCLUDE_DIRS "include"
    EMBED_FILES "webapp/index.html" "webapp/style.css" "webapp/app.js"
                "webapp/prepare.html" "webapp/prepare.js"
    REQUIRES
        esp_http_server
        esp_wifi
        nvs_flash
        esp_system
        esp_timer
        app_config
        app_wifi
        app_time
        app_photo
        app_weather
        app_flickr
        app_ota
        app_files
        bsp
)
