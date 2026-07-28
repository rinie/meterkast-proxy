#pragma once

// Preferences namespace holding the captive-portal-saved SSID/password --
// shared with web_server.cpp's POST /wifi/reset, which clears it to force
// the portal to reappear on next boot.
#define WIFI_PREFS_NAMESPACE "wifi-provision"

// Connects WiFi (from Preferences-saved credentials, falling back to a
// WiFiProvisioner captive portal if none are saved or they no longer work)
// and starts the mDNS *responder* (this device announcing itself as
// DEVICE_HOSTNAME.local) -- separate concern from mdns_browser.h, which
// *queries* for other devices' services. Blocks until connected; an ESP32
// sitting in a closet doing discovery work has nothing useful to do before
// WiFi is up anyway. See web_server.h's POST /wifi/reset for how to force
// the portal to reappear on a device that's already connected.
void wifiSetupBegin();

// Convenience alternative to the captive portal for a tethered/debug
// setup: send a line `wifi:{"ssid":"...","password":"..."}` over USB
// Serial and the device saves it (same Preferences namespace the portal
// uses) and reboots to apply it. No-op until a complete line arrives --
// call every loop() pass.
void wifiSetupLoop();
