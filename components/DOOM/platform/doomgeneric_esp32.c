#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_ops.h"

#include "doomgeneric.h"
#include "doomkeys.h"

/* Hardware handles owned and initialized by main.c */
extern esp_lcd_panel_handle_t g_doom_panel_handle;
extern SemaphoreHandle_t g_doom_disp_sem;

/* Button GPIOs, matching main.c's pin assignment (active low, pull-up). */
#define BTN_UP    2
#define BTN_LEFT  3
#define BTN_DOWN  4
#define BTN_RIGHT 5
#define BTN_OK    8

uint16_t* DG_ScreenBuffer = NULL;

void DG_Init()
{
    DG_ScreenBuffer = heap_caps_malloc(DOOMGENERIC_RESX * DOOMGENERIC_RESY * sizeof(uint16_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}

void DG_DrawFrame()
{
    esp_lcd_panel_draw_bitmap(g_doom_panel_handle, 0, 0, DOOMGENERIC_RESX, DOOMGENERIC_RESY, DG_ScreenBuffer);
    /* Block until the SPI transfer completes so we never scribble over
     * DG_ScreenBuffer while the DMA engine is still reading it. */
    xSemaphoreTake(g_doom_disp_sem, portMAX_DELAY);
}

void DG_SleepMs(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

uint32_t DG_GetTicksMs()
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void DG_SetWindowTitle(const char * title)
{
    (void) title;
}

/* -------------------------------------------------------------------------
 * INPUT
 *
 * Only 5 physical buttons exist (UP/DOWN/LEFT/RIGHT/OK), active-low with
 * pull-ups. Doom wants more distinct actions than that (move, turn, fire,
 * use, menu-confirm), so:
 *
 *   UP / DOWN            -> move forward / backward
 *   LEFT / RIGHT         -> turn left / right
 *   LEFT + RIGHT (both)  -> "use" (open doors, flip switches)
 *   UP + DOWN (both)     -> escape (open/close the menu, so you can save,
 *                           change options, or quit)
 *   OK                   -> fires KEY_ENTER *and* KEY_FIRE together, so the
 *                           same button both confirms menus/skips the demo
 *                           and shoots during gameplay.
 *
 * DG_GetKey is polled continuously by i_input.c, which drains it in a loop
 * until it returns 0. We keep a small FIFO of synthesized key events so a
 * single physical edge (e.g. OK press) can expand into multiple logical
 * key events without losing any of them.
 * ---------------------------------------------------------------------*/

typedef struct {
    unsigned char key;
    int pressed;
} key_event_t;

#define EVQ_SIZE 16
static key_event_t s_evq[EVQ_SIZE];
static uint8_t s_evq_head = 0;
static uint8_t s_evq_tail = 0;

static void evq_push(unsigned char key, int pressed)
{
    uint8_t next = (s_evq_tail + 1) % EVQ_SIZE;
    if (next == s_evq_head) {
        return; /* queue full, drop the event */
    }
    s_evq[s_evq_tail].key = key;
    s_evq[s_evq_tail].pressed = pressed;
    s_evq_tail = next;
}

static int evq_pop(unsigned char *key, int *pressed)
{
    if (s_evq_head == s_evq_tail) {
        return 0;
    }
    *key = s_evq[s_evq_head].key;
    *pressed = s_evq[s_evq_head].pressed;
    s_evq_head = (s_evq_head + 1) % EVQ_SIZE;
    return 1;
}

enum {
    BIT_UP, BIT_DOWN, BIT_LEFT, BIT_RIGHT, BIT_OK, BIT_USE, BIT_ESCAPE, BIT_COUNT
};

static uint8_t s_prev_logical = 0;
static uint32_t s_last_sample_ms = 0;
static uint8_t s_cached_raw = 0;

static void sample_and_queue_events(void)
{
    uint32_t now = DG_GetTicksMs();

    /* Rate-limit raw GPIO sampling as a simple debounce (~100Hz). */
    if (s_last_sample_ms == 0 || (now - s_last_sample_ms) >= 10) {
        uint8_t raw = 0;
        if (gpio_get_level(BTN_UP)    == 0) raw |= (1 << BIT_UP);
        if (gpio_get_level(BTN_DOWN)  == 0) raw |= (1 << BIT_DOWN);
        if (gpio_get_level(BTN_LEFT)  == 0) raw |= (1 << BIT_LEFT);
        if (gpio_get_level(BTN_RIGHT) == 0) raw |= (1 << BIT_RIGHT);
        if (gpio_get_level(BTN_OK)    == 0) raw |= (1 << BIT_OK);
        s_cached_raw = raw;
        s_last_sample_ms = now;
    }

    uint8_t raw = s_cached_raw;
    bool use_chord    = (raw & (1 << BIT_LEFT)) && (raw & (1 << BIT_RIGHT));
    bool escape_chord = (raw & (1 << BIT_UP))   && (raw & (1 << BIT_DOWN));

    uint8_t logical = 0;
    logical |= (raw & (1 << BIT_OK));
    if (use_chord) {
        logical |= (1 << BIT_USE);
    } else {
        logical |= (raw & (1 << BIT_LEFT));
        logical |= (raw & (1 << BIT_RIGHT));
    }
    if (escape_chord) {
        logical |= (1 << BIT_ESCAPE);
    } else {
        logical |= (raw & (1 << BIT_UP));
        logical |= (raw & (1 << BIT_DOWN));
    }

    uint8_t diff = logical ^ s_prev_logical;
    if (diff == 0) {
        return;
    }

    for (int i = 0; i < BIT_COUNT; i++) {
        if (!(diff & (1 << i))) {
            continue;
        }
        int pressed = (logical & (1 << i)) ? 1 : 0;

        switch (i) {
            case BIT_UP:    evq_push(KEY_UPARROW, pressed); break;
            case BIT_DOWN:  evq_push(KEY_DOWNARROW, pressed); break;
            case BIT_LEFT:  evq_push(KEY_LEFTARROW, pressed); break;
            case BIT_RIGHT: evq_push(KEY_RIGHTARROW, pressed); break;
            case BIT_USE:    evq_push(KEY_USE, pressed); break;
            case BIT_ESCAPE: evq_push(KEY_ESCAPE, pressed); break;
            case BIT_OK:
                evq_push(KEY_ENTER, pressed);
                evq_push(KEY_FIRE, pressed);
                break;
        }
    }

    s_prev_logical = logical;
}

int DG_GetKey(int* pressed, unsigned char* key)
{
    unsigned char popped_key;
    int popped_pressed;

    if (evq_pop(&popped_key, &popped_pressed)) {
        *key = popped_key;
        *pressed = popped_pressed;
        return 1;
    }

    sample_and_queue_events();

    if (evq_pop(&popped_key, &popped_pressed)) {
        *key = popped_key;
        *pressed = popped_pressed;
        return 1;
    }

    return 0;
}
