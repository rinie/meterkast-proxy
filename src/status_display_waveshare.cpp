// Waveshare ESP32-C6-Touch-LCD-1.47's display -- a JD9853 panel driven
// over its dedicated 4-wire SPI bus. Pins, init sequence, and the PWM
// backlight control below are not derived/guessed -- ported from Volos
// Projects' own published, working example for this exact board:
// https://github.com/VolosR/WaveShareC6lvglexample (NewYearExample/
// Display_ST7789.cpp/.h). An earlier version of this file used a
// different pin mapping and register-unlock sequence sourced from
// https://github.com/moononournation/Arduino_GFX/discussions/693 (which
// names this same board) -- confirmed live, on this specific physical
// unit, that mapping left the screen backlit but with zero visible
// content, even a plain full-screen fill; Volos's SCLK/MOSI/MISO/RST
// pins and gamma/voltage init table are a real, different, and
// (confirmed via their own repo) actually-working reference for this
// same product.
#include "status_display.h"
#include <Arduino_GFX_Library.h>

namespace {

constexpr int PIN_DC = 15;
constexpr int PIN_CS = 14;
constexpr int PIN_SCK = 7;
constexpr int PIN_MOSI = 6;
constexpr int PIN_MISO = 5;
constexpr int PIN_RST = 21;
constexpr int PIN_BACKLIGHT = 22;
constexpr int BACKLIGHT_PWM_FREQ_HZ = 1000;
constexpr int BACKLIGHT_PWM_RESOLUTION_BITS = 10;
// This board's SD card slot shares the same SPI bus; must be deselected or
// its floating CS can corrupt display writes.
constexpr int PIN_SD_CS = 4;

Arduino_DataBus *bus = new Arduino_HWSPI(PIN_DC, PIN_CS, PIN_SCK, PIN_MOSI, PIN_MISO);
Arduino_GFX *gfx = new Arduino_ST7789(bus, PIN_RST, /*rotation=*/0, /*ips=*/false,
                                       /*width=*/172, /*height=*/320,
                                       /*col offset1=*/34, /*row offset1=*/0,
                                       /*col offset2=*/34, /*row offset2=*/0);

void lcdRegInit() {
  static const uint8_t initOperations[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x11,
      END_WRITE,
      DELAY, 120,

      BEGIN_WRITE,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,
      WRITE_C8_D16, 0xB0, 0x00, 0xE8,

      WRITE_COMMAND_8, 0xB2,
      WRITE_BYTES, 5,
      0x0C, 0x0C, 0x00, 0x33, 0x33,

      WRITE_C8_D8, 0xB7, 0x35,
      WRITE_C8_D8, 0xBB, 0x35,
      WRITE_C8_D8, 0xC0, 0x2C,
      WRITE_C8_D8, 0xC2, 0x01,
      WRITE_C8_D8, 0xC3, 0x13,
      WRITE_C8_D8, 0xC4, 0x20,
      WRITE_C8_D8, 0xC6, 0x0F,
      WRITE_C8_D16, 0xD0, 0xA4, 0xA1,
      WRITE_C8_D8, 0xD6, 0xA1,

      WRITE_COMMAND_8, 0xE0,
      WRITE_BYTES, 14,
      0xF0, 0x00, 0x04, 0x04, 0x04, 0x05, 0x29, 0x33, 0x3E, 0x38, 0x12, 0x12, 0x28, 0x30,

      WRITE_COMMAND_8, 0xE1,
      WRITE_BYTES, 14,
      0xF0, 0x07, 0x0A, 0x0D, 0x0B, 0x07, 0x28, 0x33, 0x3E, 0x36, 0x14, 0x14, 0x29, 0x32,

      WRITE_COMMAND_8, 0x21,
      END_WRITE,

      DELAY, 10,

      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x11,
      END_WRITE,
      DELAY, 120,

      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x29,
      END_WRITE};
  bus->batchOperation(initOperations, sizeof(initOperations));
}

}  // namespace

void statusDisplayBegin() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);

  ledcAttach(PIN_BACKLIGHT, BACKLIGHT_PWM_FREQ_HZ, BACKLIGHT_PWM_RESOLUTION_BITS);
  ledcWrite(PIN_BACKLIGHT, (1 << BACKLIGHT_PWM_RESOLUTION_BITS) - 1);

  bool ok = gfx->begin();
  Serial.printf("Display: gfx->begin() = %s\n", ok ? "true" : "false");
  lcdRegInit();
  // Landscape (320x172, not the panel's native 172x320 portrait) --
  // confirmed live, the wider 320px line lets "meterkast-proxy" actually
  // fit at this text size, which the narrower portrait width didn't.
  // Same col/row offset pair as before works unchanged: Arduino_TFT's
  // rotation handling already picks the right offset per rotation.
  gfx->setRotation(1);

  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_LIME);
  gfx->setTextSize(2);
  gfx->setCursor(0, 0);
  gfx->println("meterkast-proxy");
  gfx->println("connecting...");
}

void statusDisplayShowIP(const String &ip) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setCursor(0, 0);
  gfx->println("meterkast-proxy");
  gfx->println();
  gfx->println("IP:");
  gfx->println(ip);
}
