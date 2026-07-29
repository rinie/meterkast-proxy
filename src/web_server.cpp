#include "web_server.h"
#include "config.h"
#include "ble_scanner.h"
#include "mdns_browser.h"
#include "scale_reader.h"
#include "zigbee_scanner.h"
#include "mija_thermometer.h"
#include "matter_bridge.h"
#include "wifi_setup.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace {

WebServer server(80);

void handleRoot() {
  String html =
    "<!doctype html><html><head><title>" DEVICE_HOSTNAME "</title>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:system-ui,sans-serif;max-width:640px;margin:2rem auto;padding:0 1rem}"
    "a{color:#06c}</style></head><body>";
  html += "<h1>" DEVICE_HOSTNAME "</h1>";
  html += "<p>meterkast-proxy " FIRMWARE_VERSION "</p>";
  html += "<p>Uptime: " + String(millis() / 1000) + "s &middot; ";
  html += "Free heap: " + String(ESP.getFreeHeap()) + " bytes &middot; ";
  html += "WiFi RSSI: " + String(WiFi.RSSI()) + " dBm</p>";

  html += "<h2>Bluetooth</h2>";
  html += "<p>" + String(bleDeviceCount()) + " device(s) seen. <a href=\"/scan/ble\">raw JSON</a></p>";

  html += "<h2>mDNS</h2>";
  html += "<p>" + String(mdnsDeviceCount()) + " entr" + String(mdnsDeviceCount() == 1 ? "y" : "ies") +
          " seen. <a href=\"/scan/mdns\">raw JSON</a></p>";

  if (scaleHasReading()) {
    html += "<h2>Scale</h2>";
    html += "<p>Last reading: <a href=\"/scale/read\">raw JSON</a></p>";
  }

  html += "<p><a href=\"/status\">/status</a> (compact JSON, for a health check)</p>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleBleJson() {
  server.send(200, "application/json", bleDevicesJson());
}

void handleMdnsJson() {
  server.send(200, "application/json", mdnsDevicesJson());
}

// Nearby Zigbee *networks* (coordinators/PANs), not their member devices
// -- see zigbee_scanner.h. [] on any build without real Zigbee support
// (most boards/builds -- see platformio.ini), not an error.
void handleZigbeeJson() {
  server.send(200, "application/json", zigbeeNetworksJson());
}

// Always instant -- the buffered cache from scale_reader.cpp's own
// background read cycle, never a live BLE round trip on the request
// path. {} (empty object, not an error) if no scale is configured or
// no successful read has happened yet.
void handleScaleJson() {
  server.send(200, "application/json", scaleReadingJson());
}

// Discovery helper: the scale's MAC isn't known up front, so this filters
// the already-running passive BLE scan (ble_scanner.cpp) by address
// prefix instead of requiring a hardcoded guess. Defaults to the
// Medisana BS440-family range; override with ?prefix= for a different
// scale/device entirely.
void handleScaleDiscoverJson() {
  String prefix = server.hasArg("prefix") ? server.arg("prefix") : "E4:54:EB";
  server.send(200, "application/json", bleDevicesJsonByPrefix(prefix));
}

void handleScaleConfigGet() {
  String mac = scaleGetMac();
  String json = mac.isEmpty() ? "{\"mac\":null}" : "{\"mac\":\"" + mac + "\"}";
  server.send(200, "application/json", json);
}

// Runtime scale MAC configuration -- POST {"mac":"E4:54:EB:.."}, persisted
// via Preferences (scale_reader.cpp), no reflash required.
void handleScaleConfigPost() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc["mac"].is<const char*>()) {
    server.send(400, "application/json", "{\"error\":\"expected JSON body {\\\"mac\\\":\\\"XX:XX:XX:XX:XX:XX\\\"}\"}");
    return;
  }
  String mac = doc["mac"].as<const char*>();
  if (!scaleSetMac(mac)) {
    server.send(400, "application/json", "{\"error\":\"malformed MAC, expected XX:XX:XX:XX:XX:XX\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// Discovery helper for the Xiaomi Mijia (ATC firmware) thermometers
// matter_bridge.cpp bridges as Matter endpoints -- same reused
// bleDevicesJsonByPrefix() as /scale/discover, just defaulting to the
// Xiaomi/ATC MAC range instead of the Medisana one.
void handleMijaDiscoverJson() {
  String prefix = server.hasArg("prefix") ? server.arg("prefix") : "A4:C1:38";
  server.send(200, "application/json", bleDevicesJsonByPrefix(prefix));
}

// mija_thermometer.h has a fixed number of slots (Matter endpoints can't
// be added after Matter.begin() starts) -- ?slot= selects which one,
// defaulting to 0.
size_t mijaSlotArg() {
  return server.hasArg("slot") ? static_cast<size_t>(server.arg("slot").toInt()) : 0;
}

// Decoded reading, independent of whether Matter commissioning is
// working -- lets the ATC-firmware byte parsing (mija_thermometer.cpp)
// be verified against real hardware on its own. {} if unconfigured or no
// reading decoded yet, same "buffered, no live round trip" spirit as
// /scale/read.
void handleMijaReadJson() {
  size_t slot = mijaSlotArg();
  if (slot >= MIJA_SLOT_COUNT || !mijaSlotHasReading(slot)) {
    server.send(200, "application/json", "{}");
    return;
  }
  JsonDocument doc;
  doc["temperatureC"] = mijaSlotTemperatureC(slot);
  doc["humidityPercent"] = mijaSlotHumidityPercent(slot);
  doc["ageMs"] = mijaSlotAgeMs(slot);
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

void handleMijaConfigGet() {
  size_t slot = mijaSlotArg();
  if (slot >= MIJA_SLOT_COUNT) {
    server.send(400, "application/json", "{\"error\":\"slot out of range\"}");
    return;
  }
  String mac = mijaGetSlotMac(slot);
  String json = mac.isEmpty() ? "{\"mac\":null}" : "{\"mac\":\"" + mac + "\"}";
  server.send(200, "application/json", json);
}

// Runtime thermometer slot MAC configuration -- POST {"mac":"A4:C1:38:.."}
// (optionally ?slot=1), persisted via Preferences (mija_thermometer.cpp),
// no reflash required. Same validation/response shape as /scale/config.
void handleMijaConfigPost() {
  size_t slot = mijaSlotArg();
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc["mac"].is<const char*>()) {
    server.send(400, "application/json", "{\"error\":\"expected JSON body {\\\"mac\\\":\\\"XX:XX:XX:XX:XX:XX\\\"}\"}");
    return;
  }
  String mac = doc["mac"].as<const char*>();
  if (!mijaSetSlotMac(slot, mac)) {
    server.send(400, "application/json", "{\"error\":\"invalid slot or malformed MAC\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// Matter's own BLE commissioning conflicts with ble_scanner.cpp's
// continuous scan if both run at once (confirmed live: Matter.begin()
// hangs indefinitely otherwise) -- so unlike every other feature here,
// starting Matter is an on-demand action, not something that happens
// automatically at boot. matterBridgeStartCommissioning() (matter_bridge.cpp)
// pauses the BLE scan, starts the Matter stack, and arranges for the
// scan to resume once Matter's own BLE usage ends. false (400) if
// already started -- this is meant to be called once.
void handleMatterCommissionPost() {
  if (!matterBridgeStartCommissioning()) {
    server.send(400, "application/json", "{\"error\":\"Matter already started\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
}

// The manual pairing code/QR URL, so commissioning doesn't require
// serial access -- real values (or "" on any build without Matter
// support -- see platformio.ini).
void handleMatterStatusJson() {
  JsonDocument doc;
  doc["started"] = matterBridgeIsStarted();
  doc["commissioned"] = matterBridgeIsCommissioned();
  doc["manualPairingCode"] = matterBridgeManualPairingCode();
  doc["qrCodeUrl"] = matterBridgeOnboardingQrCodeUrl();
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// Clears saved WiFi credentials and reboots -- the re-entry path into the
// WiFiProvisioner captive portal (wifi_setup.cpp) for a device that's
// already connected, without needing a physical button (this project is
// deliberately board/GPIO-agnostic -- see README).
void handleWifiReset() {
  Preferences prefs;
  prefs.begin(WIFI_PREFS_NAMESPACE, false);
  prefs.clear();
  prefs.end();
  server.send(200, "application/json", "{\"ok\":true,\"rebooting\":true}");
  delay(500);  // let the response actually flush before the reboot drops the connection
  ESP.restart();
}

// Plain string-built JSON here, not ArduinoJson -- every value is numeric
// (nothing to escape), so the extra dependency/overhead isn't worth it
// for this one small, fixed-shape endpoint.
void handleStatusJson() {
  String json = "{";
  json += "\"app\":\"meterkast-proxy\",";
  json += "\"version\":\"" FIRMWARE_VERSION "\",";
  json += "\"uptimeMs\":" + String(millis()) + ",";
  json += "\"freeHeap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"wifiRssi\":" + String(WiFi.RSSI()) + ",";
  json += "\"bleDeviceCount\":" + String(bleDeviceCount()) + ",";
  json += "\"mdnsDeviceCount\":" + String(mdnsDeviceCount()) + ",";
  json += "\"zigbeeNetworkCount\":" + String(zigbeeNetworkCount());
  json += "}";
  server.send(200, "application/json", json);
}

}  // namespace

void webServerBegin() {
  server.on("/", handleRoot);
  server.on("/scan/ble", handleBleJson);
  server.on("/scan/mdns", handleMdnsJson);
  server.on("/scan/zigbee", handleZigbeeJson);
  server.on("/scale/read", handleScaleJson);
  server.on("/scale/discover", handleScaleDiscoverJson);
  server.on("/scale/config", HTTP_GET, handleScaleConfigGet);
  server.on("/scale/config", HTTP_POST, handleScaleConfigPost);
  server.on("/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/mija/discover", handleMijaDiscoverJson);
  server.on("/mija/read", handleMijaReadJson);
  server.on("/mija/config", HTTP_GET, handleMijaConfigGet);
  server.on("/mija/config", HTTP_POST, handleMijaConfigPost);
  server.on("/matter/commission", HTTP_POST, handleMatterCommissionPost);
  server.on("/matter/status", handleMatterStatusJson);
  server.on("/status", handleStatusJson);
  server.begin();
  Serial.println("Web server started on port 80");
}

void webServerLoop() {
  server.handleClient();
}
