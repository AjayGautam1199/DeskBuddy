file(GLOB_RECURSE UI_SOURCES "ui/*.c")

idf_component_register(
    SRCS "main.c" ${UI_SOURCES}
    INCLUDE_DIRS "." "ui"
    REQUIRES lvgl esp_driver_spi esp_lcd esp_timer driver nvs_flash bt esp_phy
)

# FORCE LINKING for Bluetooth functions that might be stripped out
target_link_libraries(${COMPONENT_LIB} INTERFACE idf::bt idf::esp_phy)