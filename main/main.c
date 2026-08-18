#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_partition.h"
#include "esp_err.h"

/* -------------------------------------------------------------------------
 * CONFIGURATION
 * -----------------------------------------------------------------------*/
// Display Pins
#define LCD_HOST    SPI2_HOST
#define LCD_H_RES   240
#define LCD_V_RES   280
#define LCD_OFFSET_X 0
#define LCD_OFFSET_Y 20
#define PIN_NUM_CS  44
#define PIN_NUM_DC  43
#define PIN_NUM_RST 6
#define PIN_NUM_CLK 7
#define PIN_NUM_MOSI 9

// Button Pins (Active Low: Connect to GND when pressed)
#define BTN_UP      2
#define BTN_LEFT    3
#define BTN_DOWN    4
#define BTN_RIGHT   5
#define BTN_OK      8

/* Consumed directly by components/DOOM/platform/doomgeneric_esp32.c */
esp_lcd_panel_handle_t g_doom_panel_handle = NULL;
SemaphoreHandle_t g_doom_disp_sem = NULL;

/* Wad partition, mmap'd once at boot; read directly by
 * components/DOOM/platform/w_file.c */
const void* wad_partition_ptr = NULL;
uint32_t wad_partition_size = 0;

/* -------------------------------------------------------------------------
 * BUTTONS
 * -----------------------------------------------------------------------*/
static void init_buttons(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1; // Enable Pull-up
    io_conf.pin_bit_mask = ((1ULL<<BTN_UP) | (1ULL<<BTN_DOWN) | (1ULL<<BTN_LEFT) | (1ULL<<BTN_RIGHT) | (1ULL<<BTN_OK));
    gpio_config(&io_conf);
}

/* -------------------------------------------------------------------------
 * DISPLAY
 * -----------------------------------------------------------------------*/
static bool IRAM_ATTR notify_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t hp_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(g_doom_disp_sem, &hp_task_woken);
    return hp_task_woken == pdTRUE;
}

static void init_display(void)
{
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_CLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t) + 100,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_trans_done,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &g_doom_panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(g_doom_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(g_doom_panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(g_doom_panel_handle, true));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(g_doom_panel_handle, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(g_doom_panel_handle, true));
}

/* -------------------------------------------------------------------------
 * DOOM TASK
 * -----------------------------------------------------------------------*/
static void run_doom(void *pvParameters)
{
    extern int doom_main(int argc, char **argv);
    char *argv[] = {"doom", "-mb", "4", "-iwad", "doom1.wad", NULL};
    int argc = sizeof(argv) / sizeof(argv[0]) - 1;

    doom_main(argc, argv);

    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------
 * MAIN APP
 * -----------------------------------------------------------------------*/
void app_main(void)
{
    g_doom_disp_sem = xSemaphoreCreateBinary();

    init_buttons();
    init_display();

    // Type 0x40 / subtype 0x00 match the "wad" entry in partitions.csv
    const esp_partition_t *partition = esp_partition_find_first(0x40, 0x00, "wad");
    if (partition == NULL) {
        printf("FATAL: 'wad' partition not found - did you flash doom1-cut.wad to it?\n");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    esp_partition_mmap_handle_t map_handle;
    ESP_ERROR_CHECK(esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA,
                                        &wad_partition_ptr, &map_handle));
    wad_partition_size = partition->size;

    printf("Doom wad partition mapped: %u bytes at %p\n", (unsigned) wad_partition_size, wad_partition_ptr);

    xTaskCreatePinnedToCore(run_doom, "doom", 32768, NULL, configMAX_PRIORITIES - 1, NULL, 0);
}
