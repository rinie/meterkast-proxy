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

struct MdnsServiceQuery {
  const char* type;
  const char* proto;
};

std::vector<MdnsEntry> lastResults;
unsigned long lastQueryMs = 0;
MdnsServiceQuery serviceQueries[] = MDNS_SERVICE_TYPES;
constexpr size_t SERVICE_TYPE_COUNT = sizeof(serviceQueries) / sizeof(serviceQueries[0]);

// MDNS.queryService() blocks for up to its own internal timeout per call
// -- querying every configured service type back to back means a real,
// if brief (single-digit seconds), pause each round. This only blocks
// the Arduino main loop() task (delaying the webserver's own
// handleClient() and this timer's own re-arming), not BLE scanning,
// which runs in NimBLE's own separate background task and is unaffected.
void queryAllServiceTypes() {
  std::vector<MdnsEntry> results;
  for (size_t i = 0; i < SERVICE_TYPE_COUNT; i++) {
    const MdnsServiceQuery& query = serviceQueries[i];
    int count = MDNS.queryService(query.type, query.proto);
    for (int j = 0; j < count; j++) {
      results.push_back({
        String(query.type) + "." + query.proto,
        MDNS.hostname(j),
        // ESPmDNS's per-result IP accessor was renamed IP() -> address()
        // between arduino-esp32 core major versions 2.x and 3.x (real,
        // confirmed against both installed cores -- this project now
        // builds against both: m5stick-c on the 2.x-era official
        // platform, esp32-c6-devkitc-1 on the 3.x-era pioarduino fork --
        // see platformio.ini).
#if ESP_ARDUINO_VERSION_MAJOR >= 3
        MDNS.address(j),
#else
        MDNS.IP(j),
#endif
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
