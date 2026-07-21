#pragma once

// Connects WiFi and starts the mDNS *responder* (this device announcing
// itself as DEVICE_HOSTNAME.local) -- separate concern from
// mdns_browser.h, which *queries* for other devices' services. Blocks
// until connected; an ESP32 sitting in a closet doing discovery work has
// nothing useful to do before WiFi is up anyway.
void wifiSetupBegin();
