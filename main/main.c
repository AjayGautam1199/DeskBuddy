#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "lvgl.h"
#include "ui/ui.h" 

// --- NimBLE Includes ---
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

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

SemaphoreHandle_t gui_mutex;

/* -------------------------------------------------------------------------
 * UI LOGGING (Thread Safe)
 * -----------------------------------------------------------------------*/
void ui_print(const char * format, ...) {
    if (ui_TextArea1 == NULL) return;

    char buffer[128];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (xSemaphoreTake(gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        lv_textarea_add_text(ui_TextArea1, buffer);
        lv_textarea_add_text(ui_TextArea1, "\n");
        xSemaphoreGive(gui_mutex); 
    }
}

/* -------------------------------------------------------------------------
 * INPUT DEVICE (BUTTONS)
 * -----------------------------------------------------------------------*/
void init_buttons() {
    // Configure all button pins as Input with Pull-up
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 1; // Enable Pull-up
    // Bitmask of all pins
    io_conf.pin_bit_mask = ((1ULL<<BTN_UP) | (1ULL<<BTN_DOWN) | (1ULL<<BTN_LEFT) | (1ULL<<BTN_RIGHT) | (1ULL<<BTN_OK));
    gpio_config(&io_conf);
}

// LVGL calls this function periodically to read the buttons
static void keypad_read(lv_indev_drv_t * drv, lv_indev_data_t * data)
{
    uint32_t act_key = 0;

    // Check GPIOs (Logic 0 means pressed because of Pull-up)
    if(gpio_get_level(BTN_OK) == 0) {
        act_key = LV_KEY_ENTER; // Adds a new line (as you requested)
    } 
    else if(gpio_get_level(BTN_UP) == 0) {
        act_key = LV_KEY_UP;    // CHANGED: Moves cursor UP (Scrolls Up)
    } 
    else if(gpio_get_level(BTN_DOWN) == 0) {
        act_key = LV_KEY_DOWN;  // CHANGED: Moves cursor DOWN (Scrolls Down)
    } 
    else if(gpio_get_level(BTN_LEFT) == 0) {
        act_key = LV_KEY_LEFT;  // Moves cursor Left
    } 
    else if(gpio_get_level(BTN_RIGHT) == 0) {
        act_key = LV_KEY_RIGHT; // Moves cursor Right
    }

    if(act_key != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->key = act_key;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* -------------------------------------------------------------------------
 * NimBLE BLUETOOTH CALLBACKS
 * -----------------------------------------------------------------------*/
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    struct ble_hs_adv_fields fields;
    int rc;

    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (rc != 0) return 0;

            if (fields.name_len > 0) {
                char name_buf[32];
                int len = fields.name_len > 31 ? 31 : fields.name_len;
                snprintf(name_buf, len + 1, "%.*s", len, fields.name);
                ui_print("Found: %s (%d)", name_buf, event->disc.rssi);
            }
            break;
    }
    return 0;
}

static void ble_app_on_sync(void) {
    struct ble_gap_disc_params disc_params;
    disc_params.filter_duplicates = 1;
    disc_params.passive = 0;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;

    ble_gap_disc(0, BLE_HS_FOREVER, &disc_params, ble_gap_event, NULL);
    ui_print("BLE Scan Started...");
}

static void host_task(void *param) {
    nimble_port_run(); 
    nimble_port_freertos_deinit();
}

void init_bluetooth() {
    nvs_flash_init();
    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(host_task);
}

/* -------------------------------------------------------------------------
 * DISPLAY DRIVERS (LVGL v8)
 * -----------------------------------------------------------------------*/
static void my_disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) disp_drv->user_data;
    int x1 = area->x1;
    int x2 = area->x2;
    int y1 = area->y1;
    int y2 = area->y2;
    esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, (uint16_t *)color_p);
}

static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_drv = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_drv);
    return false;
}

/* -------------------------------------------------------------------------
 * MAIN APP
 * -----------------------------------------------------------------------*/
void app_main(void)
{
    gui_mutex = xSemaphoreCreateMutex();
    
    // 0. Initialize Buttons
    init_buttons();

    // 1. Hardware Init
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
    static lv_disp_drv_t disp_drv; 

    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_DC,
        .cs_gpio_num = PIN_NUM_CS,
        .pclk_hz = 20 * 1000 * 1000, 
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0, 
        .trans_queue_depth = 10,
        .on_color_trans_done = notify_lvgl_flush_ready,
        .user_ctx = &disp_drv,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(LCD_HOST, &io_config, &io_handle));

    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true)); 
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, LCD_OFFSET_X, LCD_OFFSET_Y));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    // 2. LVGL Init
    lv_init();

    #define BUF_SIZE (LCD_H_RES * LCD_V_RES / 10) 
    static lv_color_t *buf1 = NULL;
    static lv_color_t *buf2 = NULL;
    buf1 = heap_caps_malloc(BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = heap_caps_malloc(BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_H_RES;
    disp_drv.ver_res = LCD_V_RES;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_drv_register(&disp_drv);

    // ----------------------------------------------------
    // 3. Register Keypad Input Driver
    // ----------------------------------------------------
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keypad_read;
    lv_indev_t * my_indev = lv_indev_drv_register(&indev_drv);

    // ----------------------------------------------------
    // 4. Create Navigation Group
    // ----------------------------------------------------
    // A group links the hardware buttons to the software objects
    lv_group_t * g = lv_group_create();
    lv_group_set_default(g); // Important: New objects will be added here automatically
    lv_indev_set_group(my_indev, g);

    // 5. UI Init
    xSemaphoreTake(gui_mutex, portMAX_DELAY);
    printf("Loading UI...\n");
    ui_init();
    
    // Manual Group Assignment (Just in case ui_init didn't catch them)
    // If Screen 3 is active, let's ensure the Text Area is in the group so we can scroll it
    if(ui_TextArea1 != NULL) {
        lv_group_add_obj(g, ui_TextArea1);
        lv_group_focus_obj(ui_TextArea1); // Force focus on the text area
        lv_obj_add_state(ui_TextArea1, LV_STATE_FOCUSED);
    }
    
    xSemaphoreGive(gui_mutex);

    // 6. Start NimBLE
    ui_print("Initializing NimBLE...");
    init_bluetooth();

    printf("Looping...\n");
    while (1) {
        if (xSemaphoreTake(gui_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            lv_tick_inc(10); 
            lv_timer_handler(); 
            xSemaphoreGive(gui_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}