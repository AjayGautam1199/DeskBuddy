# Find all .c files in the "ui" folder and subfolders
file(GLOB_RECURSE UI_SOURCES "ui/*.c")

idf_component_register(
    SRCS "main.c" ${UI_SOURCES}
    INCLUDE_DIRS "." "ui"
    REQUIRES lvgl esp_driver_spi esp_lcd esp_timer driver
)