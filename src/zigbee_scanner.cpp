#include "zigbee_scanner.h"

// PlatformIO's Library Dependency Finder scans source text for
// #include <Zigbee.h> and pulls in (compiles+links) the whole Zigbee
// Arduino library the moment it sees that line -- it doesn't run a real
// preprocessor, so an #ifdef guard around the #include can't stop this.
// Once pulled in, the library's own .cpp files unconditionally reference
// esp_zb_* symbols that only resolve when built with the ZIGBEE_MODE_ZCZR/
// _ED flag (confirmed live: linking this file into a build without that
// flag set fails with "undefined reference to esp_zb_ep_list_create").
// So this file is only ever compiled at all for the dedicated
// esp32-c6-devkitc-1-zigbee env (see platformio.ini's build_src_filter on
// every other env) -- zigbee_scanner_stub.cpp provides the same functions
// as a no-op everywhere else. This #error is just a fail-fast safety net
// in case that filter or flag is ever misconfigured.
#if !(defined(ZIGBEE_MODE_ZCZR) || defined(ZIGBEE_MODE_ED))
#error "zigbee_scanner.cpp requires ZIGBEE_MODE_ZCZR or ZIGBEE_MODE_ED -- see esp32-c6-devkitc-1-zigbee in platformio.ini"
#endif

#include <Zigbee.h>
#include <ArduinoJson.h>
#include <vector>

namespace {

struct ZigbeeNetwork {
  uint16_t panId;
  String extendedPanId;
  uint8_t channel;
  bool permitJoining;
  bool routerCapacity;
  bool endDeviceCapacity;
};

std::vector<ZigbeeNetwork> lastResults;
bool scanInProgress = false;

// ZIGBEE_ROUTER, not ZIGBEE_COORDINATOR -- per the arduino-esp32
// Zigbee_Scan_Networks example this is modeled on, a coordinator can't
// scan for other networks (it forms/anchors one, doesn't look for
// others); only a router or end-device candidate can.
#ifdef ZIGBEE_MODE_ZCZR
constexpr zigbee_role_t SCAN_ROLE = ZIGBEE_ROUTER;
#else
constexpr zigbee_role_t SCAN_ROLE = ZIGBEE_END_DEVICE;
#endif

String formatExtendedPanId(const uint8_t* extendedPanId) {
  char buf[24];
  snprintf(
    buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", extendedPanId[7], extendedPanId[6], extendedPanId[5], extendedPanId[4], extendedPanId[3],
    extendedPanId[2], extendedPanId[1], extendedPanId[0]
  );
  return String(buf);
}

void startScan() {
  Zigbee.scanNetworks();
  scanInProgress = true;
}

// Zigbee.scanNetworks() is async (unlike scale_reader.cpp's blocking BLE
// connect) -- this just polls the non-blocking scanComplete() each
// loop() pass rather than busy-waiting.
void collectResultsIfDone() {
  int16_t status = Zigbee.scanComplete();
  if (status == ZB_SCAN_RUNNING) return;

  if (status == ZB_SCAN_FAILED) {
    Serial.println("Zigbee network scan failed, retrying");
  } else {
    std::vector<ZigbeeNetwork> results;
    zigbee_scan_result_t* scanResult = Zigbee.getScanResult();
    for (int16_t i = 0; i < status; i++) {
      results.push_back({
        scanResult[i].short_pan_id,
        formatExtendedPanId(scanResult[i].extended_pan_id),
        scanResult[i].logic_channel,
        static_cast<bool>(scanResult[i].permit_joining),
        static_cast<bool>(scanResult[i].router_capacity),
        static_cast<bool>(scanResult[i].end_device_capacity),
      });
    }
    lastResults = results;
    Zigbee.scanDelete();
  }

  scanInProgress = false;
  startScan();  // continuous re-scan, same spirit as ble_scanner.cpp's own background scan
}

}  // namespace

void zigbeeScannerBegin() {
  if (!Zigbee.begin(SCAN_ROLE)) {
    Serial.println("Zigbee failed to start -- network scanner disabled");
    return;
  }
  Serial.println("Zigbee network scanner started");
  startScan();
}

void zigbeeScannerLoop() {
  if (!scanInProgress) return;
  collectResultsIfDone();
}

String zigbeeNetworksJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& network : lastResults) {
    JsonObject obj = arr.add<JsonObject>();
    obj["panId"] = network.panId;
    obj["extendedPanId"] = network.extendedPanId;
    obj["channel"] = network.channel;
    obj["permitJoining"] = network.permitJoining;
    obj["routerCapacity"] = network.routerCapacity;
    obj["endDeviceCapacity"] = network.endDeviceCapacity;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

size_t zigbeeNetworkCount() {
  return lastResults.size();
}
