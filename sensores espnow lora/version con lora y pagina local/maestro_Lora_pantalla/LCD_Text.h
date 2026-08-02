#pragma once

#include <Arduino.h>
#include "Display_ST7789.h"

void LCD_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t scale = 1);
void LCD_DrawText(uint16_t x, uint16_t y, const String& text, uint16_t color, uint8_t scale = 1);
uint16_t LCD_TextWidth(const String& text, uint8_t scale = 1);
uint16_t LCD_TextHeight(uint8_t scale = 1);
