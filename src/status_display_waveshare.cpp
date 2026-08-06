// Waveshare ESP32-C6-Touch-LCD-1.47's display -- a JD9853 panel that
// enumerates/responds to the standard ST7789 command set (Arduino_GFX has
// no dedicated JD9853 class; this board doesn't need one), driven over its
// dedicated 4-wire SPI bus. Pins and the panel-specific register-unlock
// sequence below are not derived/guessed -- copied verbatim from a real,
// benchmark-confirmed-working report against this exact board:
// https://github.com/moononournation/Arduino_GFX/discussions/693
// (the plain ST7789 init alone leaves this panel blank; JD9853 needs the
// 0xDF 0x98 0x53 unlock write plus the vendor gamma/voltage table that
// follows before anything appears).
#include "status_display.h"
#include <Arduino_GFX_Library.h>

namespace {

constexpr int PIN_DC = 15;
constexpr int PIN_CS = 14;
constexpr int PIN_SCK = 1;
constexpr int PIN_MOSI = 2;
constexpr int PIN_MISO = 3;
constexpr int PIN_RST = 22;
constexpr int PIN_BACKLIGHT = 23;
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
      WRITE_C8_D16, 0xDF, 0x98, 0x53,
      WRITE_C8_D8, 0xB2, 0x23,

      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 4,
      0x00, 0x47, 0x00, 0x6F,

      WRITE_COMMAND_8, 0xBB,
      WRITE_BYTES, 6,
      0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

      WRITE_C8_D16, 0xC0, 0x44, 0xA4,
      WRITE_C8_D8, 0xC1, 0x16,

      WRITE_COMMAND_8, 0xC3,
      WRITE_BYTES, 8,
      0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

      WRITE_COMMAND_8, 0xC4,
      WRITE_BYTES, 12,
      0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

      WRITE_COMMAND_8, 0xC8,
      WRITE_BYTES, 32,
      0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17,
      0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
      0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

      WRITE_COMMAND_8, 0xD0,
      WRITE_BYTES, 5,
      0x04, 0x06, 0x6B, 0x0F, 0x00,

      WRITE_C8_D16, 0xD7, 0x00, 0x30,
      WRITE_C8_D8, 0xE6, 0x14,
      WRITE_C8_D8, 0xDE, 0x01,

      WRITE_COMMAND_8, 0xB7,
      WRITE_BYTES, 5,
      0x03, 0x13, 0xEF, 0x35, 0x35,

      WRITE_COMMAND_8, 0xC1,
      WRITE_BYTES, 3,
      0x14, 0x15, 0xC0,

      WRITE_C8_D16, 0xC2, 0x06, 0x3A,
      WRITE_C8_D16, 0xC4, 0x72, 0x12,
      WRITE_C8_D8, 0xBE, 0x00,
      WRITE_C8_D8, 0xDE, 0x02,

      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3,
      0x00, 0x02, 0x00,

      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3,
      0x01, 0x02, 0x00,

      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x35, 0x00,
      WRITE_C8_D8, 0x3A, 0x05,

      WRITE_COMMAND_8, 0x2A,
      WRITE_BYTES, 4,
      0x00, 0x22, 0x00, 0xCD,

      WRITE_COMMAND_8, 0x2B,
      WRITE_BYTES, 4,
      0x00, 0x00, 0x01, 0x3F,

      WRITE_C8_D8, 0xDE, 0x02,

      WRITE_COMMAND_8, 0xE5,
      WRITE_BYTES, 3,
      0x00, 0x02, 0x00,

      WRITE_C8_D8, 0xDE, 0x00,
      WRITE_C8_D8, 0x36, 0x00,
      WRITE_COMMAND_8, 0x21,
      END_WRITE,

      DELAY, 10,

      BEGIN_WRITE,
      WRITE_COMMAND_8, 0x29,
      END_WRITE};
  bus->batchOperation(initOperations, sizeof(initOperations));
}

}  // namespace

void statusDisplayBegin() {
  pinMode(PIN_SD_CS, OUTPUT);
  digitalWrite(PIN_SD_CS, HIGH);
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);

  gfx->begin();
  lcdRegInit();
  gfx->setRotation(2);  // USB connector at top, readable
  gfx->invertDisplay(true);

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
