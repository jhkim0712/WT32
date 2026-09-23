idf_component_register(
    SRCS "app_flickr.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp-tls
        mbedtls
        fatfs
        app_config
        app_wifi
        app_photo
        bsp
)
