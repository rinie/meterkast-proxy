#include "mija_thermometer.h"
#include "config.h"
#include <Preferences.h>
#include <algorithm>
#include <cctype>

namespace {

constexpr const char* MIJA_PREFS_NAMESPACE = "mija";

struct MijaSlot {
  String mac;
  bool hasReading = false;
  float temperatureC = 0;
  float humidityPercent = 0;
  unsigned long lastReadingMs = 0;
};

MijaSlot slots[MIJA_SLOT_COUNT];

bool isValidMacFormat(const String& mac) {
  if (mac.length() != 17) return false;
  for (int i = 0; i < 17; i++) {
    char c = mac[i];
    if (i % 3 == 2) {
      if (c != ':') return false;
    } else if (!isxdigit(static_cast<unsigned char>(c))) {
      return false;
    }
  }
  return true;
}

bool macEquals(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  return a.equalsIgnoreCase(b);
}

int16_t readLE16(const uint8_t* bytes) {
  return static_cast<int16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

uint16_t readLEU16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0] | (static_cast<uint16_t>(bytes[1]) << 8));
}

// Both 0x181A formats' first 6 bytes are the device's own MAC, which we
// don't need to re-parse (the advertisement's address, already matched
// against the configured slot MAC by the caller, is the same
// information) -- so only the fields after it are decoded here.
void decodeAtc1441(const uint8_t* payload, MijaSlot& slot) {
  slot.temperatureC = readLE16(payload + 6) / 10.0f;
  slot.humidityPercent = payload[8];
}

void decodePvvxCustom(const uint8_t* payload, MijaSlot& slot) {
  slot.temperatureC = readLE16(payload + 6) / 100.0f;
  slot.humidityPercent = readLEU16(payload + 8) / 100.0f;
}

constexpr uint16_t UUID_ATC = 0x181A;
constexpr uint16_t UUID_MIBEACON = 0xFE95;

// Xiaomi's own MiBeacon protocol v5 (0xFE95) -- variable-length frame,
// Frame Control flag bits gate which fields are present, at most one
// {Object ID, length, data} triplet per advertisement. See
// mija_thermometer.h for the full writeup/doc references. Returns
// whether a temperature/humidity object was actually found and decoded
// (a device round-robins which single value it reports each cycle, so
// most calls will legitimately decode nothing new).
bool decodeMiBeacon(const uint8_t* payload, size_t length, MijaSlot& slot) {
  if (length < 5) return false;  // Frame Control(2) + Product ID(2) + Frame Counter(1) minimum

  uint16_t frameControl = readLEU16(payload);
  bool isEncrypted = frameControl & (1 << 3);
  bool hasMac = frameControl & (1 << 4);
  bool hasCapability = frameControl & (1 << 5);
  bool hasObject = frameControl & (1 << 6);

  // Encrypted frames need a per-device bindkey this project has no way
  // to configure -- see mija_thermometer.h. Nothing to decode without an
  // Object either.
  if (isEncrypted || !hasObject) return false;

  size_t offset = 5;
  if (hasMac) offset += 6;
  if (hasCapability) {
    if (offset >= length) return false;
    bool hasIoCapability = payload[offset] & (1 << 5);
    offset += 1;
    if (hasIoCapability) offset += 2;
  }

  if (offset + 3 > length) return false;  // Object ID(2) + Object Data Len(1)
  uint16_t objectId = readLEU16(payload + offset);
  uint8_t objectLen = payload[offset + 2];
  const uint8_t* objectData = payload + offset + 3;
  if (offset + 3 + objectLen > length) return false;

  if (objectId == 0x1004 && objectLen >= 2) {  // temperature, x0.1C
    slot.temperatureC = readLE16(objectData) / 10.0f;
    return true;
  }
  if (objectId == 0x1006 && objectLen >= 2) {  // humidity, x0.1%
    slot.humidityPercent = readLEU16(objectData) / 10.0f;
    return true;
  }
  return false;  // some other object type (battery, etc.) -- not tracked
}

}  // namespace

void mijaThermometerBegin() {
  Preferences prefs;
  prefs.begin(MIJA_PREFS_NAMESPACE, true);
  for (size_t i = 0; i < MIJA_SLOT_COUNT; i++) {
    slots[i].mac = prefs.getString(("mac" + String(i)).c_str(), "");
  }
  prefs.end();

#ifdef MIJA_THERMOMETER_MAC_0
  if (slots[0].mac.isEmpty()) slots[0].mac = MIJA_THERMOMETER_MAC_0;
#endif
#ifdef MIJA_THERMOMETER_MAC_1
  if (MIJA_SLOT_COUNT > 1 && slots[1].mac.isEmpty()) slots[1].mac = MIJA_THERMOMETER_MAC_1;
#endif
}

void mijaHandleAdvertisement(const String& address, uint16_t serviceDataUuid, const std::string& serviceData) {
  for (size_t i = 0; i < MIJA_SLOT_COUNT; i++) {
    if (slots[i].mac.isEmpty() || !macEquals(slots[i].mac, address)) continue;

    const uint8_t* payload = reinterpret_cast<const uint8_t*>(serviceData.data());
    size_t length = serviceData.length();
    bool decoded = false;

    if (serviceDataUuid == UUID_ATC) {
      // atc1441 original (13 bytes) vs pvvx's extended "custom" format
      // (15 bytes) -- told apart purely by payload length, see
      // mija_thermometer.h.
      if (length == 13) {
        decodeAtc1441(payload, slots[i]);
        decoded = true;
      } else if (length == 15) {
        decodePvvxCustom(payload, slots[i]);
        decoded = true;
      }
    } else if (serviceDataUuid == UUID_MIBEACON) {
      decoded = decodeMiBeacon(payload, length, slots[i]);
    }

    if (decoded) {
      slots[i].hasReading = true;
      slots[i].lastReadingMs = millis();
    }
  }
}

bool mijaSetSlotMac(size_t slot, const String& mac) {
  if (slot >= MIJA_SLOT_COUNT || !isValidMacFormat(mac)) return false;

  Preferences prefs;
  prefs.begin(MIJA_PREFS_NAMESPACE, false);
  prefs.putString(("mac" + String(slot)).c_str(), mac);
  prefs.end();

  slots[slot].mac = mac;
  slots[slot].hasReading = false;
  return true;
}

String mijaGetSlotMac(size_t slot) {
  if (slot >= MIJA_SLOT_COUNT) return "";
  return slots[slot].mac;
}

bool mijaSlotHasReading(size_t slot) {
  return slot < MIJA_SLOT_COUNT && slots[slot].hasReading;
}

float mijaSlotTemperatureC(size_t slot) {
  return slot < MIJA_SLOT_COUNT ? slots[slot].temperatureC : 0;
}

float mijaSlotHumidityPercent(size_t slot) {
  return slot < MIJA_SLOT_COUNT ? slots[slot].humidityPercent : 0;
}

unsigned long mijaSlotAgeMs(size_t slot) {
  return slot < MIJA_SLOT_COUNT ? millis() - slots[slot].lastReadingMs : 0;
}
