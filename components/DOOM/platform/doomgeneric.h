#ifndef DOOM_GENERIC
#define DOOM_GENERIC

#include <stdlib.h>
#include <stdint.h>

// Our ST7789 panel is 240x280 (see LCD_H_RES/LCD_V_RES in main/main.c).
#define DOOMGENERIC_RESX 240
#define DOOMGENERIC_RESY 280

extern uint16_t* DG_ScreenBuffer;

void DG_Init();
void DG_DrawFrame();
void DG_SleepMs(uint32_t ms);
uint32_t DG_GetTicksMs();
int DG_GetKey(int* pressed, unsigned char* key);
void DG_SetWindowTitle(const char * title);

#endif //DOOM_GENERIC
