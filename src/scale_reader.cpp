#include "scale_reader.h"
#include "config.h"

// The whole feature compiles out when SCALE_MAC_ADDRESS isn't defined
// -- most boards won't have a scale, and this keeps flash usage (already
// tight -- see README.md) at zero cost for boards that don't need it.
#ifdef SCALE_MAC_ADDRESS
#include <NimBLEDevice.h>

namespace {

// Bluetooth SIG-standard Weight Scale Service (0x181D) / Weight
// Measurement characteristic (0x2A9D) defaults -- overridable in
// config.h for a scale with a proprietary profile instead (common on
// cheap consumer scales; verify against the real device before trusting
// this decode -- see README.md).
#ifndef SCALE_SERVICE_UUID
#define SCALE_SERVICE_UUID "0000181d-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_CHARACTERISTIC_UUID
#define SCALE_CHARACTERISTIC_UUID "00002a9d-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_READ_INTERVAL_MS
#define SCALE_READ_INTERVAL_MS 30000
#endif

float lastWeightKg = 0;
unsigned long lastReadMs = 0;
bool hasReading = false;
unsigned long lastAttemptMs = 0;

// Weight Measurement's own flags/scaling per the Bluetooth SIG spec --
// a flat scaled uint16, not IEEE-11073 SFLOAT (that's Health
// Thermometer's Temperature Measurement instead, a different
// characteristic already hand-verified once elsewhere in this project's
// history -- this one is the simpler of the two). Bit 0 of the flags
// byte selects the unit; only the weight field itself is decoded here,
// the optional timestamp/user-ID/BMI fields some scales also send are
// not read.
bool decodeWeightMeasurement(const uint8_t* data, size_t len, float* outWeightKg) {
  if (len < 3) return false;
  uint8_t flags = data[0];
  uint16_t raw = data[1] | (static_cast<uint16_t>(data[2]) << 8);
  bool isImperial = flags & 0x01;
  *outWeightKg = isImperial ? (raw * 0.01f * 0.45359237f) : (raw * 0.005f);
  return true;
}

// A real connect/read/disconnect cycle -- not held open between reads,
// so it can't starve ble_scanner.cpp's own continuous passive scan of
// radio time for longer than one read actually takes.
void performScaleRead() {
  NimBLEClient* client = NimBLEDevice::createClient();
  bool connected = client->connect(NimBLEAddress(SCALE_MAC_ADDRESS, BLE_ADDR_PUBLIC));
  if (connected) {
    NimBLERemoteService* service = client->getService(SCALE_SERVICE_UUID);
    NimBLERemoteCharacteristic* characteristic = service ? service->getCharacteristic(SCALE_CHARACTERISTIC_UUID) : nullptr;
    if (characteristic && characteristic->canRead()) {
      NimBLEAttValue value = characteristic->readValue();
      float weightKg;
      if (decodeWeightMeasurement(value.data(), value.length(), &weightKg)) {
        lastWeightKg = weightKg;
        lastReadMs = millis();
        hasReading = true;
        Serial.printf("Scale read: %.3f kg\n", weightKg);
      } else {
        Serial.println("Scale read: unexpected payload length, not decoded");
      }
    } else {
      Serial.println("Scale read: service/characteristic not found on this device");
    }
    client->disconnect();
  } else {
    Serial.println("Scale read: connect failed");
  }
  NimBLEDevice::deleteClient(client);
}

}  // namespace

void scaleReaderBegin() {
  lastAttemptMs = 0;  // read immediately on the first loop() pass
}

void scaleReaderLoop() {
  if (millis() - lastAttemptMs < SCALE_READ_INTERVAL_MS) return;
  lastAttemptMs = millis();
  performScaleRead();
}

String scaleReadingJson() {
  if (!hasReading) return "{}";
  String json = "{";
  json += "\"weightKg\":" + String(lastWeightKg, 3) + ",";
  json += "\"ageMs\":" + String(millis() - lastReadMs);
  json += "}";
  return json;
}

bool scaleHasReading() {
  return hasReading;
}

#else  // !defined(SCALE_MAC_ADDRESS)

void scaleReaderBegin() {}
void scaleReaderLoop() {}
String scaleReadingJson() { return "{}"; }
bool scaleHasReading() { return false; }

#endif
