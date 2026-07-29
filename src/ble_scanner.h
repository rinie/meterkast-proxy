#pragma once
#include <Arduino.h>

// Continuous, passive+active BLE scan, running in NimBLE's own background
// task -- not something bleScannerLoop() drives (it's a no-op today, kept
// for symmetry with the other two scanners and as an obvious place for a
// future stall/restart check).
void bleScannerBegin();
void bleScannerLoop();

// A JSON array of every device seen since boot (bounded, oldest evicted
// once full -- see MAX_TRACKED_DEVICES in the .cpp): [{address, name?,
// rssi, ageMs, serviceData?}, ...]. serviceData (present only when the
// device's advertisement actually carries any) is {serviceUuid: hex,
// ...} raw bytes, generic capture with no assumption about what any of
// it means -- decoding is meterkast-dns's playlist-driven job now, not
// firmware's. Mirrors the shape meterkast-dns's own unclaimed*Devices()
// functions already produce on the Node side, so proxy-adapter.js there
// is a straight fetch+map, the same pattern as dirigera-adapter.js.
String bleDevicesJson();
size_t bleDeviceCount();

// Same shape as bleDevicesJson(), filtered to addresses starting with
// prefix (case-insensitive; empty prefix matches everything). A
// discovery helper for finding an unknown-up-front device's MAC (e.g.
// GET /ble/discover) without a dedicated active scan.
String bleDevicesJsonByPrefix(const String& prefix);
