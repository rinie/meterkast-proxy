#pragma once

#include <Arduino.h>

// Shows this device's status (currently just its IP address) on an onboard
// display, plain text, boot-log style -- no fancy graphics. Only the three
// boards that actually have a display (m5stick-c, esp32-c6-waveshare-matter,
// esp32-s3-sensecap-indicator) link a real implementation; every other env
// links status_display_stub.cpp instead (see platformio.ini's
// build_src_filter per env -- same real-vs-stub split already used for
// zigbee_scanner.cpp/matter_bridge.cpp, for the same PlatformIO Library
// Dependency Finder reason documented there).
void statusDisplayBegin();
void statusDisplayShowIP(const String &ip);
