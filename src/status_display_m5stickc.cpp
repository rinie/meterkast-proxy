// M5Stick-C's built-in 0.96" ST7735S display, driven via M5Unified --
// plain text only, per the actual ask (a boot-log-style IP readout, not a
// UI). See status_display.h for why this file only builds into the
// m5stick-c env.
#include "status_display.h"
#include <M5Unified.h>

void statusDisplayBegin() {
  auto cfg = M5.config();
  // Serial.begin() already happened in the .ino before wifiSetupBegin()
  // (which calls into here) -- don't let M5.begin() re-init it.
  cfg.serial_baudrate = 0;
  M5.begin(cfg);

  M5.Display.setRotation(1);
  M5.Display.setTextSize(2);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("meterkast-proxy");
  M5.Display.println("connecting...");
}

void statusDisplayShowIP(const String &ip) {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(0, 0);
  M5.Display.println("meterkast-proxy");
  M5.Display.println();
  M5.Display.println("IP:");
  M5.Display.println(ip);
}
