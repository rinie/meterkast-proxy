#pragma once
#include <Arduino.h>
#include <vector>

// Result of one generic BLE GATT session -- connect, optionally write a
// trigger value to one characteristic (waiting a caller-specified delay
// for the device to act on it), read each requested characteristic,
// disconnect. Still no subscribe/wait-for-indication -- a device whose
// real measurement arrives asynchronously *after* a write, with no fixed
// delay that reliably covers it (a Medisana scale, say) still needs
// that, separately, later. This covers the more common
// "write a mode/trigger byte, then read back an already-updated
// characteristic after a fixed delay" pattern instead -- a Xiaomi Mi
// Flora plant sensor's real-time-data characteristic works exactly this
// way (write 0xA01F to switch it into "realtime" mode, then its sensor
// characteristic reads back real values instead of stale/zeroed ones).
struct GattReading {
  String characteristicUuid;
  String hex;
};

// The write step, entirely optional -- present only when a profile
// actually needs one (most GATT devices, like the Health Thermometer
// profile, don't). No device-specific knowledge lives here either --
// which characteristic, what bytes, how long to wait all come from the
// caller (meterkast-dns's own playlist-driven profile), never compiled
// into this firmware.
struct GattWriteStep {
  String characteristicUuid;
  String hex;
  unsigned long delayMs = 0;
};

struct GattSessionResult {
  bool ok;
  String error;               // set when !ok
  std::vector<GattReading> readings;  // set when ok -- one entry per
                                       // characteristic that was both
                                       // found and readable; a
                                       // requested-but-missing/unreadable
                                       // characteristic is silently
                                       // skipped, not a hard failure, so
                                       // one bad UUID in a recipe doesn't
                                       // block the others.
};

// address: "XX:XX:XX:XX:XX:XX". serviceUuid/characteristicUuids: any
// string NimBLEUUID accepts (16-bit short form or full 128-bit). `write`
// is optional (nullptr means no write step, the original read-only
// behavior) -- when given, its characteristic must actually exist and
// accept a write, or the whole session fails with a clear error, rather
// than silently proceeding to read a characteristic that would then just
// return stale/zeroed data instead of the value the write was meant to
// trigger. No device-specific knowledge lives here at all -- every
// parameter comes from the caller (meterkast-dns, via its own playlist),
// never compiled into this firmware.
GattSessionResult gattSessionExecute(const String& address, const String& serviceUuid,
                                      const std::vector<String>& characteristicUuids,
                                      const GattWriteStep* write = nullptr);
