#include "matter_bridge.h"

// No-op implementation used by every env except esp32-c6-devkitc-1-matter
// -- see the comment at the top of matter_bridge.h for why this has to be
// a separate file (not an #ifdef branch within one file): PlatformIO's
// Library Dependency Finder pulls in the whole Matter library based on
// text-scanning for #include <Matter.h>, regardless of any surrounding
// preprocessor guard, so that #include can't appear anywhere in a file
// compiled for boards/envs without real Matter support.
void matterBridgeBegin() {}
void matterBridgeLoop() {}
bool matterBridgeStartCommissioning() {
  return false;
}
bool matterBridgeIsStarted() {
  return false;
}
bool matterBridgeIsCommissioned() {
  return false;
}
String matterBridgeManualPairingCode() {
  return "";
}
String matterBridgeOnboardingQrCodeUrl() {
  return "";
}
