#include "zigbee_scanner.h"

// No-op implementation used by every env except esp32-c6-devkitc-1-zigbee
// -- see the comment at the top of zigbee_scanner.cpp for why this has to
// be a separate file (not an #ifdef branch within one file): PlatformIO's
// Library Dependency Finder pulls in the whole Zigbee library based on
// text-scanning for #include <Zigbee.h>, regardless of any surrounding
// preprocessor guard, so that #include can't appear anywhere in a file
// compiled for boards/envs without real Zigbee support.
void zigbeeScannerBegin() {}
void zigbeeScannerLoop() {}
String zigbeeNetworksJson() {
  return "[]";
}
size_t zigbeeNetworkCount() {
  return 0;
}
