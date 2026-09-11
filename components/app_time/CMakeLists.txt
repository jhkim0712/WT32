idf_component_register(
    SRCS "app_time.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif lwip app_config
)
