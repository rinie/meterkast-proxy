#include "web_server.h"
#include "config.h"
#include "ble_scanner.h"
#include "mdns_browser.h"
#include "scale_reader.h"
#include <WebServer.h>
#include <WiFi.h>

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

// Always instant -- the buffered cache from scale_reader.cpp's own
// background read cycle, never a live BLE round trip on the request
// path. {} (empty object, not an error) if no scale is configured or
// no successful read has happened yet.
void handleScaleJson() {
  server.send(200, "application/json", scaleReadingJson());
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
  json += "\"mdnsDeviceCount\":" + String(mdnsDeviceCount());
  json += "}";
  server.send(200, "application/json", json);
}

}  // namespace

void webServerBegin() {
  server.on("/", handleRoot);
  server.on("/scan/ble", handleBleJson);
  server.on("/scan/mdns", handleMdnsJson);
  server.on("/scale/read", handleScaleJson);
  server.on("/status", handleStatusJson);
  server.begin();
  Serial.println("Web server started on port 80");
}

void webServerLoop() {
  server.handleClient();
}
