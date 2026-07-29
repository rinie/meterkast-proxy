#include "web_server.h"
#include "config.h"
#include "ble_scanner.h"
#include "mdns_browser.h"
#include "gatt_session.h"
#include "zigbee_scanner.h"
#include "mija_thermometer.h"
#include "matter_bridge.h"
#include "wifi_setup.h"
#include <WebServer.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>

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

// Discovery helper: a device's MAC often isn't known up front, so this
// filters the already-running passive BLE scan (ble_scanner.cpp) by
// address prefix instead of requiring a hardcoded guess. Not specific to
// any one device (was named /scale/discover once, before this project
// moved every device-specific decision off this firmware and into
// meterkast-dns's own playlist -- see README); defaults to the Medisana
// BS440-family range purely because that's the device this was first
// built for, override with ?prefix= for anything else.
void handleBleDiscoverJson() {
  String prefix = server.hasArg("prefix") ? server.arg("prefix") : "E4:54:EB";
  server.send(200, "application/json", bleDevicesJsonByPrefix(prefix));
}

// Generic BLE GATT session -- connect to `address`, optionally write a
// trigger value to a characteristic first (an optional `write` object:
// `{"characteristicUuid":"..","hex":"..","delayMs":..}`), then read
// whatever characteristics under `serviceUuid` are listed in `read`,
// disconnect. No device-specific knowledge here or in gatt_session.cpp
// at all: every UUID/byte/delay comes from the request body, which
// meterkast-dns builds from its own playlist. Still no
// subscribe/wait-for-indication -- see gatt_session.h.
void handleGattSession() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc["address"].is<const char*>() || !doc["serviceUuid"].is<const char*>() || !doc["read"].is<JsonArray>()) {
    server.send(
        400, "application/json",
        "{\"ok\":false,\"error\":\"expected JSON body {\\\"address\\\":\\\"..\\\",\\\"serviceUuid\\\":\\\"..\\\",\\\"read\\\":[\\\"..\\\"],"
        "\\\"write\\\":{\\\"characteristicUuid\\\":\\\"..\\\",\\\"hex\\\":\\\"..\\\",\\\"delayMs\\\":..} (optional)}\"}");
    return;
  }

  std::vector<String> characteristicUuids;
  for (JsonVariant v : doc["read"].as<JsonArray>()) characteristicUuids.push_back(v.as<const char*>());

  GattWriteStep writeStep;
  bool hasWrite = doc["write"].is<JsonObject>();
  if (hasWrite) {
    JsonObject w = doc["write"].as<JsonObject>();
    writeStep.characteristicUuid = w["characteristicUuid"] | "";
    writeStep.hex = w["hex"] | "";
    writeStep.delayMs = w["delayMs"] | 0;
  }

  GattSessionResult result = gattSessionExecute(
      doc["address"].as<const char*>(), doc["serviceUuid"].as<const char*>(), characteristicUuids,
      hasWrite ? &writeStep : nullptr);

  JsonDocument response;
  response["ok"] = result.ok;
  if (!result.ok) {
    response["error"] = result.error;
  } else {
    JsonObject readings = response["readings"].to<JsonObject>();
    for (const GattReading& reading : result.readings) readings[reading.characteristicUuid] = reading.hex;
  }
  String out;
  serializeJson(response, out);
  server.send(200, "application/json", out);
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
  server.on("/ble/discover", handleBleDiscoverJson);
  server.on("/gatt/session", HTTP_POST, handleGattSession);
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
