idf_component_register(
    SRCS
        "app_photo.c"
        "app_photo_bmp.c"
        "app_photo_jpeg.c"
        "app_photo_resize.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES fatfs esp_jpeg esp_hw_support
)
