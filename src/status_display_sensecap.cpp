// SenseCAP Indicator D1's display -- an ST7701S 480x480 RGB/DPI panel
// driven directly by the ESP32-S3 (its own dedicated LCD_CAM peripheral,
// not routed through the board's separate RP2040). Confirmed real: the
// RP2040 was a red herring from Seeed's own high-level hardware overview --
// their own Arduino guide drives this display straight from the S3, same
// approach openHASP's board profile uses:
//   https://wiki.seeedstudio.com/SenseCAP_Indicator_ESP32_Arduino/
//   https://github.com/HASwitchPlate/openHASP/blob/main/user_setups/esp32s3/sensecap-indicator.ini
// The panel's own init-command bus is 3-wire SPI whose CS line is wired
// through this board's PCA9535 I2C GPIO expander (pin P04), not a direct
// GPIO -- Arduino_GFX's Arduino_SWSPI accepts that PCA95x5 port constant
// directly as its CS argument (built-in expander-pin support), which is
// what Seeed's own example does; Wire.begin() below on the same SDA/SCL
// pins the board's touch controller uses is required first so that
// expander traffic actually reaches it.
#include "status_display.h"
#include <Arduino_GFX_Library.h>
#include <PCA95x5.h>
#include <Wire.h>

namespace {

constexpr int PIN_TOUCH_SDA = 39;
constexpr int PIN_TOUCH_SCL = 40;
constexpr int PIN_BACKLIGHT = 45;

Arduino_DataBus *bus = new Arduino_SWSPI(GFX_NOT_DEFINED /* DC */, PCA95x5::Port::P04 /* CS */,
                                          41 /* SCK */, 48 /* MOSI */, GFX_NOT_DEFINED /* MISO */);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    4 /* R0 */, 3 /* R1 */, 2 /* R2 */, 1 /* R3 */, 0 /* R4 */,
    10 /* G0 */, 9 /* G1 */, 8 /* G2 */, 7 /* G3 */, 6 /* G4 */, 5 /* G5 */,
    15 /* B0 */, 14 /* B1 */, 13 /* B2 */, 12 /* B3 */, 11 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */);

Arduino_RGB_Display *gfx =
    new Arduino_RGB_Display(480 /* width */, 480 /* height */, rgbpanel, 2 /* rotation */,
                             true /* auto_flush */, bus, GFX_NOT_DEFINED /* RST */,
                             st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

}  // namespace

void statusDisplayBegin() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  if (!gfx->begin()) {
    Serial.println("Display: gfx->begin() failed");
    return;
  }
  pinMode(PIN_BACKLIGHT, OUTPUT);
  digitalWrite(PIN_BACKLIGHT, HIGH);

  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_LIME);
  gfx->setTextSize(4);
  gfx->setCursor(10, 10);
  gfx->println("meterkast-proxy");
  gfx->println("connecting...");
}

void statusDisplayShowIP(const String &ip) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setCursor(10, 10);
  gfx->println("meterkast-proxy");
  gfx->println();
  gfx->println("IP:");
  gfx->println(ip);
}
