idf_component_register(
    SRCS "app_ota.c"
    INCLUDE_DIRS "include"
    REQUIRES
        app_update
        esp_app_format   # esp_app_desc.h (esp_app_desc_t, esp_app_get_description)
        esp_https_ota
        esp_http_client
        esp-tls
        mbedtls
)
