#pragma once
#include <Arduino.h>
#include <vector>

// Result of one generic BLE GATT read session -- connect, read each
// requested characteristic, disconnect. Read-only for now: no
// subscribe/write/wait-for-indication (a real device -- a weight scale
// -- needs that, but it's deferred; this covers plain connect-and-read
// devices like standard Bluetooth SIG profiles and most GATT
// thermometers first -- see README "Known real limitations").
struct GattReading {
  String characteristicUuid;
  String hex;
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
// string NimBLEUUID accepts (16-bit short form or full 128-bit). No
// device-specific knowledge lives here at all -- every parameter comes
// from the caller (meterkast-dns, via its own playlist), never compiled
// into this firmware.
GattSessionResult gattSessionExecute(const String& address, const String& serviceUuid,
                                      const std::vector<String>& characteristicUuids);
