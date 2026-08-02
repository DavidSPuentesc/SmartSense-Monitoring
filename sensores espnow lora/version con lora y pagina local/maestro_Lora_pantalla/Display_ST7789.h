#pragma once

#include <Arduino.h>
#include <SPI.h>

#define LCD_WIDTH   172
#define LCD_HEIGHT  320

// ESP32-C6-Touch-LCD-1.47 pin map (JD9853 + AXS5106L)
#define SPIFreq                        80000000
#define EXAMPLE_PIN_NUM_MISO           3
#define EXAMPLE_PIN_NUM_MOSI           2
#define EXAMPLE_PIN_NUM_SCLK           1
#define EXAMPLE_PIN_NUM_LCD_CS         14
#define EXAMPLE_PIN_NUM_LCD_DC         15
#define EXAMPLE_PIN_NUM_LCD_RST        22
#define EXAMPLE_PIN_NUM_BK_LIGHT       23
#define EXAMPLE_PIN_NUM_TOUCH_SDA      18
#define EXAMPLE_PIN_NUM_TOUCH_SCL      19
#define EXAMPLE_PIN_NUM_TOUCH_RST      20
#define EXAMPLE_PIN_NUM_TOUCH_INT      21
#define Frequency                      5000
#define Resolution                     10

#define VERTICAL   0
#define HORIZONTAL 1

#define Offset_X 34
#define Offset_Y 0

void LCD_Init(void);
void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend);
void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, const uint16_t* color);
void LCD_WritePixels(const uint16_t* colors, uint32_t count);
void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void LCD_FillScreen(uint16_t color);
uint16_t LCD_Color565(uint8_t red, uint8_t green, uint8_t blue);
bool LCD_TouchAvailable(void);
bool LCD_TouchRead(uint16_t* x, uint16_t* y);

void Backlight_Init(void);
void Set_Backlight(uint8_t Light);
