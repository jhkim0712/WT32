idf_component_register(
    SRCS "app_weather.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp-tls
        mbedtls
        app_config
        app_wifi
)
