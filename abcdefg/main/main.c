#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "lvgl.h"

/* -------------------------------------------------------------------------
 * PIN DEFINITIONS (Xiao ESP32S3)
 * -----------------------------------------------------------------------*/
#define LCD_HOST    SPI2_HOST 
#define PIN_NUM_CS  44
#define PIN_NUM_DC  43
#define PIN_NUM_RST 6
#define PIN_NUM_CLK 7
#define PIN_NUM_MOSI 9
#define PIN_NUM_MISO -1 
#define PIN_NUM_BCKL -1 

/* -------------------------------------------------------------------------
 * DISPLAY CONFIGURATION
 * -----------------------------------------------------------------------*/
#define LCD_H_RES   240
#define LCD_V_RES   280
#define LCD_OFFSET_X 0
#define LCD_OFFSET_Y 20  

static lv_display_t *display = NULL;

/* -------------------------------------------------------------------------
 * CALLBACKS
 * -----------------------------------------------------------------------*/
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    if (display) {
        lv_display_flush_ready(display);
    }
    return false;
}

static void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)lv_display_get_user_data(disp);
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;

    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map);
}

/* -------------------------------------------------------------------------
 * MAIN APP
 * -----------------------------------------------------------------------*/
void app_main(void)
{
    // 1. Initialize SPI Bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_CLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = PIN_NUM_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES * sizeof(uint16_t) + 100,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2. Initialize Panel IO
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        // FIX 1: Lowered to 10MHz to stop blinking/noise
        .pclk_hz = 10 * 1000 * 1000, 
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0, 
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = NULL,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    // 3. Initialize Panel Driver
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    // 4. Configure Panel
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); 
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 5. Initialize LVGL
    lv_init();

    // 6. Create Display
    display = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_user_data(display, panel_handle);
    lv_display_set_flush_cb(display, my_disp_flush);
    
    // 7. Allocate Draw Buffers (Using PSRAM if available)
    #define BUF_SIZE (LCD_H_RES * LCD_V_RES / 10 * 2) 
    static uint8_t *buf1 = NULL;
    static uint8_t *buf2 = NULL;
    
    // Try to allocate in internal RAM first for speed, or PSRAM if too large
    buf1 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(BUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    
    lv_display_set_buffers(display, buf1, buf2, BUF_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 8. Create Test UI
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);

    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Ajay Gautam");
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t * btn = lv_button_create(screen);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_t * btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "More");

    printf("Looping...\n");
    while (1) {
        lv_timer_handler(); 
        
        // FIX 2: Increased delay to 20ms to prevent Watchdog Timeout (Starvation)
        vTaskDelay(pdMS_TO_TICKS(20)); 
    }
}