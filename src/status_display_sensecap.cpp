// SenseCAP Indicator D1's display -- an ST7701S 480x480 RGB/DPI panel
// driven directly by the ESP32-S3 (its own dedicated LCD_CAM peripheral,
// not routed through the board's separate RP2040). Confirmed real: the
// RP2040 was a red herring from Seeed's own high-level hardware overview --
// their own Arduino guide drives this display straight from the S3, same
// approach openHASP's board profile uses:
//   https://wiki.seeedstudio.com/SenseCAP_Indicator_ESP32_Arduino/
//   https://github.com/HASwitchPlate/openHASP/blob/main/user_setups/esp32s3/sensecap-indicator.ini
//
// The panel's own 3-wire init-command bus has its CS line wired through
// this board's PCA9535 I2C GPIO expander (port 0, pin 4), not a direct
// GPIO. Seeed's own published example passes `PCA95x5::Port::P04`
// straight as Arduino_SWSPI's CS argument -- confirmed live, on this
// specific physical unit, that this does *not* work: Arduino_SWSPI treats
// CS as a plain integer GPIO number (see its own source -- a bare
// `pinMode()`/`digitalWrite()` call, no expander awareness at all), so
// that constant just toggles an unrelated real GPIO instead of ever
// reaching the panel's actual CS pin. The panel's init sequence never
// lands; screen stays backlit but blank. Confirmed via
// https://github.com/moononournation/Arduino_GFX/discussions/334, where
// the original poster hit this exact symptom with this exact board and
// the exact same GFX_NOT_DEFINED/PCA95x5-as-CS approach, and the real fix
// was a hand-written custom Arduino_DataBus mixing plain GPIO bit-banged
// SCK/MOSI with expander-routed CS -- never published, so
// `SensecapExpanderCsBus` below is that class, written from scratch
// against the PCA9535's register map (same standard layout used by
// Arduino_GFX's own bundled `Arduino_XL9535SWSPI` class, a register-
// compatible expander family, which is what confirms this map is right).
#include "status_display.h"
#include <Arduino_GFX_Library.h>
#include <Wire.h>

namespace {

constexpr int PIN_TOUCH_SDA = 39;
constexpr int PIN_TOUCH_SCL = 40;
constexpr int PIN_BACKLIGHT = 45;
constexpr int PIN_SCK = 41;
constexpr int PIN_MOSI = 48;

constexpr uint8_t PCA9535_I2C_ADDRESS = 0x20;
constexpr uint8_t PCA9535_OUTPUT_PORT_0_REG = 0x02;
constexpr uint8_t PCA9535_CONFIG_PORT_0_REG = 0x06;
constexpr uint8_t PCA9535_CS_BIT = 1 << 4;  // P04

// A 3-wire (D/C-bit-in-stream) software-SPI Arduino_DataBus where CS is
// an I2C-expander pin instead of a real GPIO -- see this file's header
// comment for why the stock Arduino_SWSPI/PCA95x5-as-CS combination
// doesn't work on this board.
class SensecapExpanderCsBus : public Arduino_DataBus {
 public:
  bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t dataMode = GFX_NOT_DEFINED) override {
    (void)speed;
    (void)dataMode;
    pinMode(PIN_SCK, OUTPUT);
    pinMode(PIN_MOSI, OUTPUT);
    digitalWrite(PIN_SCK, LOW);

    uint8_t config = readRegister(PCA9535_CONFIG_PORT_0_REG);
    writeRegister(PCA9535_CONFIG_PORT_0_REG, config & ~PCA9535_CS_BIT);  // P04 as output
    setCs(true);
    return true;
  }

  void beginWrite() override { setCs(false); }
  void endWrite() override { setCs(true); }

  void writeCommand(uint8_t c) override { shiftOut9(c, /*isData=*/false); }
  void writeCommand16(uint16_t c) override {
    shiftOut9(c >> 8, false);
    shiftOut9(c & 0xFF, false);
  }
  void writeCommandBytes(uint8_t *data, uint32_t len) override {
    for (uint32_t i = 0; i < len; i++) shiftOut9(data[i], false);
  }
  void write(uint8_t d) override { shiftOut9(d, /*isData=*/true); }
  void write16(uint16_t d) override {
    shiftOut9(d >> 8, true);
    shiftOut9(d & 0xFF, true);
  }
  void writeRepeat(uint16_t p, uint32_t len) override {
    for (uint32_t i = 0; i < len; i++) write16(p);
  }
  void writeBytes(uint8_t *data, uint32_t len) override {
    for (uint32_t i = 0; i < len; i++) write(data[i]);
  }
  void writePixels(uint16_t *data, uint32_t len) override {
    for (uint32_t i = 0; i < len; i++) write16(data[i]);
  }

 private:
  void setCs(bool high) {
    uint8_t out = readRegister(PCA9535_OUTPUT_PORT_0_REG);
    writeRegister(PCA9535_OUTPUT_PORT_0_REG, high ? (out | PCA9535_CS_BIT) : (out & ~PCA9535_CS_BIT));
  }

  void shiftOut9(uint8_t byte, bool isData) {
    digitalWrite(PIN_MOSI, isData ? HIGH : LOW);
    digitalWrite(PIN_SCK, HIGH);
    digitalWrite(PIN_SCK, LOW);
    for (uint8_t bit = 0x80; bit; bit >>= 1) {
      digitalWrite(PIN_MOSI, (byte & bit) ? HIGH : LOW);
      digitalWrite(PIN_SCK, HIGH);
      digitalWrite(PIN_SCK, LOW);
    }
  }

  uint8_t readRegister(uint8_t reg) {
    Wire.beginTransmission(PCA9535_I2C_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((int)PCA9535_I2C_ADDRESS, 1);
    return Wire.available() ? Wire.read() : 0xFF;
  }

  void writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(PCA9535_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
  }
};

SensecapExpanderCsBus *bus = new SensecapExpanderCsBus();

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
    18 /* DE */, 17 /* VSYNC */, 16 /* HSYNC */, 21 /* PCLK */,
    4 /* R0 */, 3 /* R1 */, 2 /* R2 */, 1 /* R3 */, 0 /* R4 */,
    10 /* G0 */, 9 /* G1 */, 8 /* G2 */, 7 /* G3 */, 6 /* G4 */, 5 /* G5 */,
    15 /* B0 */, 14 /* B1 */, 13 /* B2 */, 12 /* B3 */, 11 /* B4 */,
    1 /* hsync_polarity */, 10 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 50 /* hsync_back_porch */,
    1 /* vsync_polarity */, 10 /* vsync_front_porch */, 8 /* vsync_pulse_width */, 20 /* vsync_back_porch */,
    0 /* pclk_active_neg */, GFX_NOT_DEFINED /* prefer_speed */, false /* useBigEndian */,
    0 /* de_idle_high */, 0 /* pclk_idle_high */,
    // A small SRAM bounce buffer, shielding the LCD DMA from PSRAM bus
    // contention with WiFi -- without it, confirmed live: the screen
    // flickers/flashes roughly once a second while WiFi is active.
    480 * 10 /* bounce_buffer_size_px */);

Arduino_RGB_Display *gfx =
    new Arduino_RGB_Display(480 /* width */, 480 /* height */, rgbpanel, 2 /* rotation */,
                             true /* auto_flush */, bus, GFX_NOT_DEFINED /* RST */,
                             st7701_type1_init_operations, sizeof(st7701_type1_init_operations));

}  // namespace

void statusDisplayBegin() {
  Wire.begin(PIN_TOUCH_SDA, PIN_TOUCH_SCL, 400000);

  bool ok = gfx->begin();
  Serial.printf("Display: gfx->begin() = %s\n", ok ? "true" : "false");
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
