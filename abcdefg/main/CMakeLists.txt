idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES lvgl esp_driver_spi esp_lcd esp_timer driver
)