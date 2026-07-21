// meterkast-dns ESP32 proxy -- BLE + mDNS discovery, served as JSON, plus a
// small Tasmota-style status page. Board-agnostic: no GPIOs used yet, so
// this runs on whatever ESP32 board you point Arduino IDE at. Pulsetape's
// 433 MHz capture (board-specific pin wiring) slots in later as a third
// scanner alongside these two, once this basis is working.
//
// UNCOMPILED / UNVERIFIED: written without access to an ESP32 toolchain --
// a first real draft to build and iterate on in Arduino IDE, not something
// flash-tested against real hardware yet.
//
// Libraries needed (Arduino Library Manager):
//   - NimBLE-Arduino (h2zero)      -- lighter/faster than the stock BLEDevice
//   - ArduinoJson (bblanchon)      -- safe JSON building (name escaping, etc.)
//   - ESPmDNS, WebServer, WiFi     -- bundled with the arduino-esp32 core
//
// Setup: copy src/config.h.example to src/config.h and fill in your
// WiFi credentials (config.h is gitignored, same reasoning
// device-playlist.toml/.env are gitignored on the meterkast-dns side --
// personal, not committed).

#include "src/config.h"
#include "src/wifi_setup.h"
#include "src/ble_scanner.h"
#include "src/mdns_browser.h"
#include "src/web_server.h"

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\nmeterkast-dns ESP32 proxy starting...");

  wifiSetupBegin();
  bleScannerBegin();
  mdnsBrowserBegin();
  webServerBegin();

  Serial.println("Ready.");
}

void loop() {
  bleScannerLoop();
  mdnsBrowserLoop();
  webServerLoop();
}
