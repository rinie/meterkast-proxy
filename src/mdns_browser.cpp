#include "mdns_browser.h"
#include "config.h"
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <vector>

namespace {

struct MdnsEntry {
  String serviceType;
  String hostname;
  IPAddress ip;
  uint16_t port;
};

std::vector<MdnsEntry> lastResults;
unsigned long lastQueryMs = 0;
const char* serviceTypes[] = MDNS_SERVICE_TYPES;
constexpr size_t SERVICE_TYPE_COUNT = sizeof(serviceTypes) / sizeof(serviceTypes[0]);

// MDNS.queryService() blocks for up to its own internal timeout per call
// -- querying every configured service type back to back means a real,
// if brief (single-digit seconds), pause each round. This only blocks
// the Arduino main loop() task (delaying the webserver's own
// handleClient() and this timer's own re-arming), not BLE scanning,
// which runs in NimBLE's own separate background task and is unaffected.
void queryAllServiceTypes() {
  std::vector<MdnsEntry> results;
  for (size_t i = 0; i < SERVICE_TYPE_COUNT; i++) {
    int count = MDNS.queryService(serviceTypes[i], MDNS_SERVICE_PROTO);
    for (int j = 0; j < count; j++) {
      results.push_back({
        String(serviceTypes[i]) + "." + MDNS_SERVICE_PROTO,
        MDNS.hostname(j),
        MDNS.IP(j),
        MDNS.port(j),
      });
    }
  }
  lastResults = results;
}

}  // namespace

void mdnsBrowserBegin() {
  queryAllServiceTypes();
  lastQueryMs = millis();
}

void mdnsBrowserLoop() {
  if (millis() - lastQueryMs < MDNS_QUERY_INTERVAL_MS) return;
  queryAllServiceTypes();
  lastQueryMs = millis();
}

size_t mdnsDeviceCount() {
  return lastResults.size();
}

String mdnsDevicesJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& entry : lastResults) {
    JsonObject obj = arr.add<JsonObject>();
    obj["serviceType"] = entry.serviceType;
    obj["hostname"] = entry.hostname;
    obj["ip"] = entry.ip.toString();
    obj["port"] = entry.port;
  }
  String out;
  serializeJson(doc, out);
  return out;
}
