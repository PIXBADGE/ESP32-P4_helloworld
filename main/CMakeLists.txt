idf_component_register(
    SRCS "hello_world_main.c" "lcd_gfx.c"
    INCLUDE_DIRS "."
    REQUIRES st7801n touch esp_lcd spi_flash
)
