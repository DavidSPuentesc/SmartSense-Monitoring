#include "Display_ST7789.h"

#include <Wire.h>

#define SPI_WRITE(_dat)      SPI.transfer(_dat)
#define SPI_WRITE_Word(_dat) SPI.transfer16(_dat)

namespace {

bool g_touchReady = false;
bool g_touchInitAttempted = false;
constexpr uint8_t kTouchAddress = 0x63;
constexpr uint8_t kTouchIdRegister = 0x08;
constexpr uint8_t kTouchDataRegister = 0x01;
constexpr uint8_t kTouchFrameSize = 14;

void SPI_Init()
{
  SPI.begin(EXAMPLE_PIN_NUM_SCLK, EXAMPLE_PIN_NUM_MISO, EXAMPLE_PIN_NUM_MOSI, EXAMPLE_PIN_NUM_LCD_CS);
}

void LCD_WriteCommand(uint8_t cmd)
{
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, LOW);
  SPI_WRITE(cmd);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteData(uint8_t data)
{
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  SPI_WRITE(data);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteDataBytes(const uint8_t* data, size_t length)
{
  if (data == nullptr || length == 0) {
    return;
  }

  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
  for (size_t i = 0; i < length; ++i) {
    SPI_WRITE(data[i]);
  }
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteCommandData(uint8_t cmd, const uint8_t* data, size_t length)
{
  LCD_WriteCommand(cmd);
  LCD_WriteDataBytes(data, length);
}

void LCD_Reset()
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH);
  delay(20);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, LOW);
  delay(20);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_RST, HIGH);
  delay(120);
}

void LCD_StartDataWrite()
{
  SPI.beginTransaction(SPISettings(SPIFreq, MSBFIRST, SPI_MODE0));
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, LOW);
  digitalWrite(EXAMPLE_PIN_NUM_LCD_DC, HIGH);
}

void LCD_EndDataWrite()
{
  digitalWrite(EXAMPLE_PIN_NUM_LCD_CS, HIGH);
  SPI.endTransaction();
}

void LCD_WriteRepeatedColor(uint16_t color, uint32_t pixel_count)
{
  LCD_StartDataWrite();
  for (uint32_t i = 0; i < pixel_count; ++i) {
    SPI_WRITE_Word(color);
  }
  LCD_EndDataWrite();
}

void Touch_ResetController()
{
  pinMode(EXAMPLE_PIN_NUM_TOUCH_RST, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_TOUCH_INT, INPUT_PULLUP);
  digitalWrite(EXAMPLE_PIN_NUM_TOUCH_RST, LOW);
  delay(200);
  digitalWrite(EXAMPLE_PIN_NUM_TOUCH_RST, HIGH);
  delay(300);
}

bool Touch_ReadRegister(uint8_t reg, uint8_t* data, size_t length)
{
  Wire.beginTransmission(kTouchAddress);
  Wire.write(reg);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delayMicroseconds(100);

  const int received = Wire.requestFrom(static_cast<int>(kTouchAddress), static_cast<int>(length));
  if (received != static_cast<int>(length)) {
    while (Wire.available() > 0) {
      Wire.read();
    }
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    if (Wire.available() <= 0) {
      return false;
    }
    data[i] = static_cast<uint8_t>(Wire.read());
  }
  return true;
}

bool Touch_Probe()
{
  uint8_t id[3] = {0};
  if (!Touch_ReadRegister(kTouchIdRegister, id, sizeof(id))) {
    return false;
  }

  Serial.printf("[LCD] Touch ID: %02X %02X %02X\n", id[0], id[1], id[2]);
  return true;
}

bool Touch_InitInternal()
{
  if (g_touchReady) {
    return true;
  }
  if (g_touchInitAttempted) {
    return false;
  }

  g_touchInitAttempted = true;
  Wire.begin(EXAMPLE_PIN_NUM_TOUCH_SDA, EXAMPLE_PIN_NUM_TOUCH_SCL);
  Wire.setClock(100000);
  Touch_ResetController();

  g_touchReady = Touch_Probe();
  if (g_touchReady) {
    Serial.println("[LCD] Touch inicializado");
  } else {
    Serial.println("[LCD] No se pudo inicializar el touch");
  }
  return g_touchReady;
}

bool Touch_ReadPolledCoordinate(uint16_t* x, uint16_t* y)
{
  uint8_t data[kTouchFrameSize] = {0};
  if (!g_touchReady) {
    return false;
  }

  if (!Touch_ReadRegister(kTouchDataRegister, data, sizeof(data))) {
    return false;
  }

  const uint8_t touchCount = data[1];
  if (touchCount == 0) {
    return false;
  }

  const uint16_t rawX = (static_cast<uint16_t>(data[2] & 0x0F) << 8) | data[3];
  const uint16_t rawY = (static_cast<uint16_t>(data[4] & 0x0F) << 8) | data[5];

  if (x != nullptr) {
    *x = (rawX < LCD_WIDTH) ? static_cast<uint16_t>(LCD_WIDTH - 1 - rawX) : 0;
  }
  if (y != nullptr) {
    *y = (rawY < LCD_HEIGHT) ? rawY : 0;
  }
  return true;
}

}  // namespace

void LCD_Init(void)
{
  pinMode(EXAMPLE_PIN_NUM_LCD_CS, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_DC, OUTPUT);
  pinMode(EXAMPLE_PIN_NUM_LCD_RST, OUTPUT);

  Backlight_Init();
  SPI_Init();
  LCD_Reset();

  LCD_WriteCommand(0x11);
  delay(120);

  LCD_WriteCommand(0xDF);
  LCD_WriteData(0x98);
  LCD_WriteData(0x53);

  LCD_WriteCommand(0xB2);
  LCD_WriteData(0x23);

  {
    const uint8_t data[] = {0x00, 0x47, 0x00, 0x6F};
    LCD_WriteCommandData(0xB7, data, sizeof(data));
  }

  {
    const uint8_t data[] = {0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0};
    LCD_WriteCommandData(0xBB, data, sizeof(data));
  }

  LCD_WriteCommand(0xC0);
  LCD_WriteData(0x44);
  LCD_WriteData(0xA4);

  LCD_WriteCommand(0xC1);
  LCD_WriteData(0x16);

  {
    const uint8_t data[] = {0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77};
    LCD_WriteCommandData(0xC3, data, sizeof(data));
  }

  {
    const uint8_t data[] = {0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82};
    LCD_WriteCommandData(0xC4, data, sizeof(data));
  }

  {
    const uint8_t data[] = {
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00
    };
    LCD_WriteCommandData(0xC8, data, sizeof(data));
  }

  {
    const uint8_t data[] = {0x04, 0x06, 0x6B, 0x0F, 0x00};
    LCD_WriteCommandData(0xD0, data, sizeof(data));
  }

  LCD_WriteCommand(0xD7);
  LCD_WriteData(0x00);
  LCD_WriteData(0x30);

  LCD_WriteCommand(0xE6);
  LCD_WriteData(0x14);

  LCD_WriteCommand(0xDE);
  LCD_WriteData(0x01);

  {
    const uint8_t data[] = {0x03, 0x13, 0xEF, 0x35, 0x35};
    LCD_WriteCommandData(0xB7, data, sizeof(data));
  }

  {
    const uint8_t data[] = {0x14, 0x15, 0xC0};
    LCD_WriteCommandData(0xC1, data, sizeof(data));
  }

  LCD_WriteCommand(0xC2);
  LCD_WriteData(0x06);
  LCD_WriteData(0x3A);

  LCD_WriteCommand(0xC4);
  LCD_WriteData(0x72);
  LCD_WriteData(0x12);

  LCD_WriteCommand(0xBE);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0xDE);
  LCD_WriteData(0x02);

  {
    const uint8_t data[] = {0x00, 0x02, 0x00};
    LCD_WriteCommandData(0xE5, data, sizeof(data));
  }
  {
    const uint8_t data[] = {0x01, 0x02, 0x00};
    LCD_WriteCommandData(0xE5, data, sizeof(data));
  }

  LCD_WriteCommand(0xDE);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0x35);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0x3A);
  LCD_WriteData(0x05);

  {
    const uint8_t data[] = {0x00, 0x22, 0x00, 0xCD};
    LCD_WriteCommandData(0x2A, data, sizeof(data));
  }

  {
    const uint8_t data[] = {0x00, 0x00, 0x01, 0x3F};
    LCD_WriteCommandData(0x2B, data, sizeof(data));
  }

  LCD_WriteCommand(0xDE);
  LCD_WriteData(0x02);

  {
    const uint8_t data[] = {0x00, 0x02, 0x00};
    LCD_WriteCommandData(0xE5, data, sizeof(data));
  }

  LCD_WriteCommand(0xDE);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0x36);
  LCD_WriteData(0x00);

  LCD_WriteCommand(0x21);
  delay(10);

  LCD_WriteCommand(0x29);
  Touch_InitInternal();
}

void LCD_SetCursor(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend)
{
  if (HORIZONTAL) {
    LCD_WriteCommand(0x2A);
    LCD_WriteData(Xstart >> 8);
    LCD_WriteData(static_cast<uint8_t>(Xstart + Offset_X));
    LCD_WriteData(Xend >> 8);
    LCD_WriteData(static_cast<uint8_t>(Xend + Offset_X));

    LCD_WriteCommand(0x2B);
    LCD_WriteData(Ystart >> 8);
    LCD_WriteData(static_cast<uint8_t>(Ystart + Offset_Y));
    LCD_WriteData(Yend >> 8);
    LCD_WriteData(static_cast<uint8_t>(Yend + Offset_Y));
  } else {
    LCD_WriteCommand(0x2A);
    LCD_WriteData(Ystart >> 8);
    LCD_WriteData(static_cast<uint8_t>(Ystart + Offset_Y));
    LCD_WriteData(Yend >> 8);
    LCD_WriteData(static_cast<uint8_t>(Yend + Offset_Y));

    LCD_WriteCommand(0x2B);
    LCD_WriteData(Xstart >> 8);
    LCD_WriteData(static_cast<uint8_t>(Xstart + Offset_X));
    LCD_WriteData(Xend >> 8);
    LCD_WriteData(static_cast<uint8_t>(Xend + Offset_X));
  }

  LCD_WriteCommand(0x2C);
}

uint16_t LCD_Color565(uint8_t red, uint8_t green, uint8_t blue)
{
  return static_cast<uint16_t>(((red & 0xF8) << 8) | ((green & 0xFC) << 3) | (blue >> 3));
}

void LCD_WritePixels(const uint16_t* colors, uint32_t count)
{
  if (colors == nullptr || count == 0) {
    return;
  }

  LCD_StartDataWrite();
  for (uint32_t i = 0; i < count; ++i) {
    SPI_WRITE_Word(colors[i]);
  }
  LCD_EndDataWrite();
}

void LCD_addWindow(uint16_t Xstart, uint16_t Ystart, uint16_t Xend, uint16_t Yend, const uint16_t* color)
{
  if (color == nullptr || Xstart > Xend || Ystart > Yend) {
    return;
  }

  const uint16_t width = Xend - Xstart + 1;
  const uint16_t height = Yend - Ystart + 1;

  LCD_SetCursor(Xstart, Ystart, Xend, Yend);
  LCD_WritePixels(color, static_cast<uint32_t>(width) * height);
}

void LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (x >= LCD_WIDTH || y >= LCD_HEIGHT) {
    return;
  }

  LCD_SetCursor(x, y, x, y);
  LCD_WriteRepeatedColor(color, 1);
}

void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
  if (width == 0 || height == 0 || x >= LCD_WIDTH || y >= LCD_HEIGHT) {
    return;
  }

  if (x + width > LCD_WIDTH) {
    width = LCD_WIDTH - x;
  }

  if (y + height > LCD_HEIGHT) {
    height = LCD_HEIGHT - y;
  }

  LCD_SetCursor(x, y, x + width - 1, y + height - 1);
  LCD_WriteRepeatedColor(color, static_cast<uint32_t>(width) * height);
}

void LCD_FillScreen(uint16_t color)
{
  LCD_FillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

bool LCD_TouchAvailable(void)
{
  return g_touchReady;
}

bool LCD_TouchRead(uint16_t* x, uint16_t* y)
{
  if (!g_touchReady) {
    return false;
  }

  return Touch_ReadPolledCoordinate(x, y);
}

void Backlight_Init(void)
{
  ledcAttach(EXAMPLE_PIN_NUM_BK_LIGHT, Frequency, Resolution);
  Set_Backlight(100);
}

void Set_Backlight(uint8_t Light)
{
  if (Light > 100) {
    Light = 100;
  }

  const uint32_t max_duty = (1U << Resolution) - 1U;
  const uint32_t duty = (static_cast<uint32_t>(Light) * max_duty) / 100U;
  ledcWrite(EXAMPLE_PIN_NUM_BK_LIGHT, duty);
}
