idf_component_register(
    SRCS "app_wifi.c" "dns_server.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        nvs_flash
        lwip
        app_config
)
