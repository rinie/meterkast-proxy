#include "web_server.h"
#include "config.h"
#include "ble_scanner.h"
#include "mdns_browser.h"
#include "gatt_session.h"
#include "zigbee_scanner.h"
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

// Generic BLE GATT read session -- connect to `address`, read whatever
// characteristics under `serviceUuid` are listed in `read`, disconnect.
// No device-specific knowledge here or in gatt_session.cpp at all: every
// UUID comes from the request body, which meterkast-dns builds from its
// own playlist. Read-only for now -- see gatt_session.h.
void handleGattSession() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err || !doc["address"].is<const char*>() || !doc["serviceUuid"].is<const char*>() || !doc["read"].is<JsonArray>()) {
    server.send(
        400, "application/json",
        "{\"ok\":false,\"error\":\"expected JSON body {\\\"address\\\":\\\"..\\\",\\\"serviceUuid\\\":\\\"..\\\",\\\"read\\\":[\\\"..\\\"]}\"}");
    return;
  }

  std::vector<String> characteristicUuids;
  for (JsonVariant v : doc["read"].as<JsonArray>()) characteristicUuids.push_back(v.as<const char*>());

  GattSessionResult result =
      gattSessionExecute(doc["address"].as<const char*>(), doc["serviceUuid"].as<const char*>(), characteristicUuids);

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
  server.on("/status", handleStatusJson);
  server.begin();
  Serial.println("Web server started on port 80");
}

void webServerLoop() {
  server.handleClient();
}
