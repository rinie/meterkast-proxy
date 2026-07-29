#include "ble_scanner.h"
#include <NimBLEDevice.h>
#include <ArduinoJson.h>  // assumes ArduinoJson v7 (JsonDocument, no fixed capacity) -- v6's DynamicJsonDocument(size) API differs
#include <map>
#include <algorithm>
#include <cctype>

// NimBLE-Arduino has real API differences between v1.x (NimBLEAdvertisedDeviceCallbacks,
// scan->setAdvertisedDeviceCallbacks(...)) and v2.x (NimBLEScanCallbacks,
// scan->setScanCallbacks(...), used below) -- written against v2.x's
// current API; check your installed library version if this doesn't
// compile as-is.

namespace {

struct SeenDevice {
  std::string name;
  int rssi;
  unsigned long lastSeenMs;
  // Raw BLE advertisement Service Data AD structures, keyed by the
  // service UUID's own string form -- generic capture, no assumption
  // about what any of these bytes mean (that's meterkast-dns's job now,
  // not firmware's -- see README). Populated whenever the advertisement
  // that updates this entry carries any; not cleared on a later
  // advertisement that happens not to repeat it, same "keep the last
  // real value seen" treatment `name` already gets below.
  std::map<std::string, std::string> serviceData;
};

// Raw bytes -> lowercase hex, for JSON transport (JSON has no byte-string
// type). Plain and dependency-free -- this is the only place in this
// file that needs it.
String toHex(const std::string& raw) {
  static const char* digits = "0123456789abcdef";
  String out;
  out.reserve(raw.size() * 2);
  for (unsigned char b : raw) {
    out += digits[b >> 4];
    out += digits[b & 0x0f];
  }
  return out;
}

// Capped so a long scan session near a busy area (offices, apartment
// blocks) can't grow this without bound -- oldest entry evicted once
// full, the same "bounded, real device state, not infinite history"
// shape meterkast-dns's own log.js ring buffer uses on the Node side.
constexpr size_t MAX_TRACKED_DEVICES = 100;

std::map<std::string, SeenDevice> seenDevices;

void evictOldestIfFull() {
  if (seenDevices.size() < MAX_TRACKED_DEVICES) return;
  auto oldest = seenDevices.begin();
  for (auto it = seenDevices.begin(); it != seenDevices.end(); ++it) {
    if (it->second.lastSeenMs < oldest->second.lastSeenMs) oldest = it;
  }
  seenDevices.erase(oldest);
}

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* device) override {
    std::string address = device->getAddress().toString();
    std::string name = device->haveName() ? device->getName() : "";
    int rssi = device->getRSSI();

    std::map<std::string, std::string> serviceData;
    if (device->haveServiceData()) {
      uint8_t count = device->getServiceDataCount();
      for (uint8_t i = 0; i < count; i++) {
        serviceData[device->getServiceDataUUID(i).toString()] = device->getServiceData(i);
      }
    }

    auto it = seenDevices.find(address);
    if (it == seenDevices.end()) {
      evictOldestIfFull();
      seenDevices[address] = {name, rssi, millis(), serviceData};
    } else {
      it->second.rssi = rssi;
      it->second.lastSeenMs = millis();
      // A device's name-carrying advertisement/scan-response doesn't fire
      // every time -- keep whatever name was last seen rather than
      // clearing it back to blank. Same treatment for service data: a
      // BTHome-style advertiser resends it on (close to) every
      // advertisement anyway, but there's no reason to require that.
      if (!name.empty()) it->second.name = name;
      for (const auto& [uuid, data] : serviceData) it->second.serviceData[uuid] = data;
    }
  }
};

ScanCallbacks scanCallbacks;

}  // namespace

void bleScannerBegin() {
  NimBLEDevice::init("");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&scanCallbacks);
  scan->setActiveScan(true);  // also request scan responses -- often the only place a device's name is carried
  scan->setInterval(100);
  scan->setWindow(99);
  scan->start(0, false);  // duration 0 = scan forever, in NimBLE's own background task
  Serial.println("BLE scanner started (continuous)");
}

void bleScannerLoop() {
  // Intentionally empty -- see the header comment.
}

size_t bleDeviceCount() {
  return seenDevices.size();
}

namespace {

// Shared by bleDevicesJson()/bleDevicesJsonByPrefix() -- an empty prefix
// matches everything. Case-insensitive since MAC addresses show up in
// either case depending on the caller.
String serializeSeenDevices(const String& prefix) {
  std::string prefixLower(prefix.c_str());
  std::transform(prefixLower.begin(), prefixLower.end(), prefixLower.begin(), ::tolower);

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  unsigned long now = millis();
  for (const auto& [address, device] : seenDevices) {
    if (!prefixLower.empty()) {
      std::string addressLower = address;
      std::transform(addressLower.begin(), addressLower.end(), addressLower.begin(), ::tolower);
      if (addressLower.compare(0, prefixLower.size(), prefixLower) != 0) continue;
    }
    JsonObject obj = arr.add<JsonObject>();
    obj["address"] = address;
    if (!device.name.empty()) obj["name"] = device.name;
    obj["rssi"] = device.rssi;
    obj["ageMs"] = now - device.lastSeenMs;
    if (!device.serviceData.empty()) {
      JsonObject serviceDataObj = obj["serviceData"].to<JsonObject>();
      for (const auto& [uuid, data] : device.serviceData) {
        serviceDataObj[uuid] = toHex(data);
      }
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

}  // namespace

String bleDevicesJson() {
  return serializeSeenDevices("");
}

// Discovery helper for devices whose MAC isn't known up front (e.g. the
// scale reader, config.h) -- reuses this already-running passive scan
// instead of a dedicated active scan for the purpose.
String bleDevicesJsonByPrefix(const String& prefix) {
  return serializeSeenDevices(prefix);
}
