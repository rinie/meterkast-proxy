#include "ble_scanner.h"
#include "mija_thermometer.h"
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
};

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

    auto it = seenDevices.find(address);
    if (it == seenDevices.end()) {
      evictOldestIfFull();
      seenDevices[address] = {name, rssi, millis()};
    } else {
      it->second.rssi = rssi;
      it->second.lastSeenMs = millis();
      // A device's name-carrying advertisement/scan-response doesn't fire
      // every time -- keep whatever name was last seen rather than
      // clearing it back to blank.
      if (!name.empty()) it->second.name = name;
    }

    // Xiaomi Mijia thermometers broadcast their reading directly in
    // Service Data, under one of two possible UUIDs depending on
    // firmware/config -- see mija_thermometer.h. Forwarded here rather
    // than run as a second scan, since NimBLE only supports one
    // registered scan callback. No-op for any device not configured as a
    // mija_thermometer.cpp slot.
    for (uint8_t i = 0; i < device->getServiceDataCount(); i++) {
      NimBLEUUID uuid = device->getServiceDataUUID(i);
      if (uuid == NimBLEUUID(static_cast<uint16_t>(0x181A))) {
        mijaHandleAdvertisement(String(address.c_str()), 0x181A, device->getServiceData(i));
      } else if (uuid == NimBLEUUID(static_cast<uint16_t>(0xFE95))) {
        mijaHandleAdvertisement(String(address.c_str()), 0xFE95, device->getServiceData(i));
      }
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

void bleScannerPause() {
  NimBLEDevice::getScan()->stop();
}

void bleScannerResume() {
  NimBLEDevice::getScan()->start(0, false);
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
