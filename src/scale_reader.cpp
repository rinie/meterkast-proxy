#include "scale_reader.h"
#include "config.h"

// The whole feature compiles out when SCALE_MAC_ADDRESS isn't defined
// -- most boards won't have a scale, and this keeps flash usage (already
// tight -- see README.md) at zero cost for boards that don't need it.
#ifdef SCALE_MAC_ADDRESS
#include <NimBLEDevice.h>

namespace {

// Defaults target a Medisana BS440-family scale (BS410/BS430/BS440/
// BS444, all share the same protocol) -- this device has NO standard
// Bluetooth SIG weight-scale profile at all, unlike the first draft of
// this file assumed. Every UUID/command below comes from the
// openScale/BS440/ha-medisana-scale community reverse-engineering
// projects (real, cross-referenced across independent sources), not an
// official spec -- treat this whole decode as real but
// community-sourced, and override every value in config.h for a
// different scale entirely.
#ifndef SCALE_SERVICE_UUID
#define SCALE_SERVICE_UUID "000078b2-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_WEIGHT_CHARACTERISTIC_UUID
#define SCALE_WEIGHT_CHARACTERISTIC_UUID "00008a21-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_FEATURE_CHARACTERISTIC_UUID
#define SCALE_FEATURE_CHARACTERISTIC_UUID "00008a22-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_CUSTOM5_CHARACTERISTIC_UUID
#define SCALE_CUSTOM5_CHARACTERISTIC_UUID "00008a82-0000-1000-8000-00805f9b34fb"
#endif
#ifndef SCALE_CMD_CHARACTERISTIC_UUID
#define SCALE_CMD_CHARACTERISTIC_UUID "00008a81-0000-1000-8000-00805f9b34fb"
#endif
// The real device apparently only responds once all three of the above
// characteristics have live indication subscriptions -- documented as
// the connection sequence across every one of the reverse-engineering
// projects, so this subscribes to all three even though only the
// weight packet is actually decoded below.
#ifndef SCALE_TRIGGER_COMMAND
#define SCALE_TRIGGER_COMMAND { 0x02, 0x7b, 0x7b, 0xf6, 0x0d }
#endif
#ifndef SCALE_READ_INTERVAL_MS
#define SCALE_READ_INTERVAL_MS 30000
#endif
// How long to wait for the weight indication to arrive after writing
// the trigger command before giving up on this cycle -- a real,
// bounded wait (this device's indications are asynchronous, not a
// direct read reply), not an arbitrary guess: generous enough to cover
// a real BLE handshake plus the scale's own measurement settle time.
#ifndef SCALE_INDICATION_TIMEOUT_MS
#define SCALE_INDICATION_TIMEOUT_MS 8000
#endif

float lastWeightKg = 0;
unsigned long lastReadMs = 0;
bool hasReading = false;
unsigned long lastAttemptMs = 0;

// Set from the BLE host's own callback task, read from the main loop()
// task's busy-wait below -- a plain bool/float pair, not mutex-guarded,
// the same simple-shared-state approach ble_scanner.cpp's own scan
// callback already uses elsewhere in this firmware.
volatile bool weightIndicationReceived = false;
float pendingWeightKg = 0;

// Weight Measurement packet (characteristic 0x8a21) -- byte layout per
// the openScale wiki's Medisana-BS444 page (cross-referenced against
// the keptenkurk/BS440 and ha-medisana-scale projects): byte 0 flags
// (bits 5-6 select the unit: 00=kg, 01=lb, 10=st:lb), bytes 1-2 the
// weight itself as a little-endian uint16 divided by 100. Bytes 5-8
// (a Unix timestamp offset from 2010) and byte 13 (user ID, 1-8 or
// 0xFF) exist in the real packet too but aren't read here -- only
// weight was asked for.
void onWeightIndication(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (length < 3) return;
  uint8_t flags = data[0];
  uint8_t unit = (flags >> 5) & 0x03;
  uint16_t raw = data[1] | (static_cast<uint16_t>(data[2]) << 8);
  float weightKg = raw / 100.0f;
  if (unit == 1) weightKg *= 0.45359237f;  // lb -> kg
  // unit == 2 (stone:pounds) isn't decoded -- no real device available
  // to confirm the exact split encoding against yet.
  pendingWeightKg = weightKg;
  weightIndicationReceived = true;
}

// Feature (body composition: fat/water/muscle/bone %) and custom5
// packets -- subscribed to because the real device expects it (see
// above), but not decoded. Real, parked follow-up work, not attempted
// here since only weight was asked for and the mask/scale details for
// those fields weren't confidently sourced.
void onOtherIndication(NimBLERemoteCharacteristic*, uint8_t*, size_t, bool) {}

// A real connect/subscribe/trigger/wait/disconnect cycle -- meaningfully
// longer than a plain read (this device's measurement arrives
// asynchronously as an indication after writing a trigger command, not
// as a direct reply), so this can take up to SCALE_INDICATION_TIMEOUT_MS
// per attempt, not a quick round trip the way the original
// SIG-standard-profile draft of this file assumed.
void performScaleRead() {
  NimBLEClient* client = NimBLEDevice::createClient();
  bool connected = client->connect(NimBLEAddress(SCALE_MAC_ADDRESS, BLE_ADDR_PUBLIC));
  if (connected) {
    NimBLERemoteService* service = client->getService(SCALE_SERVICE_UUID);
    NimBLERemoteCharacteristic* weightChar = service ? service->getCharacteristic(SCALE_WEIGHT_CHARACTERISTIC_UUID) : nullptr;
    NimBLERemoteCharacteristic* featureChar = service ? service->getCharacteristic(SCALE_FEATURE_CHARACTERISTIC_UUID) : nullptr;
    NimBLERemoteCharacteristic* custom5Char = service ? service->getCharacteristic(SCALE_CUSTOM5_CHARACTERISTIC_UUID) : nullptr;
    NimBLERemoteCharacteristic* cmdChar = service ? service->getCharacteristic(SCALE_CMD_CHARACTERISTIC_UUID) : nullptr;

    if (weightChar && featureChar && custom5Char && cmdChar) {
      weightIndicationReceived = false;
      // false = indications, not notifications -- this device's
      // characteristics are indicate-only per every reverse-engineering
      // source found, confirmed against the real NimBLE subscribe()
      // signature (notifications=true is the default, deliberately
      // overridden here).
      bool subscribed = weightChar->subscribe(false, onWeightIndication) &&
                         featureChar->subscribe(false, onOtherIndication) &&
                         custom5Char->subscribe(false, onOtherIndication);

      if (subscribed) {
        const uint8_t trigger[] = SCALE_TRIGGER_COMMAND;
        cmdChar->writeValue(trigger, sizeof(trigger), false);

        unsigned long waitStart = millis();
        while (!weightIndicationReceived && millis() - waitStart < SCALE_INDICATION_TIMEOUT_MS) {
          delay(100);
        }

        if (weightIndicationReceived) {
          lastWeightKg = pendingWeightKg;
          lastReadMs = millis();
          hasReading = true;
          Serial.printf("Scale read: %.3f kg\n", lastWeightKg);
        } else {
          Serial.println("Scale read: no weight indication within timeout");
        }
      } else {
        Serial.println("Scale read: failed to subscribe to one or more characteristics");
      }
    } else {
      Serial.println("Scale read: service/characteristics not found on this device");
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
