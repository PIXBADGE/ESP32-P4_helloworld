idf_component_register(
    SRCS "st7801n.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_lcd esp_driver_gpio esp_hw_support
)
