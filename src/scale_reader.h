#pragma once
#include <Arduino.h>

// Periodic, buffered BLE GATT read of a single configured device (a
// weight scale) -- unlike ble_scanner.cpp's continuous passive scan,
// this is an active connect/read/disconnect cycle against one known
// address, run on SCALE_READ_INTERVAL_MS, caching the last good result
// so a request never blocks on a live BLE round trip. The MAC is
// runtime-configurable (see scaleSetMac below); loop() is a no-op
// (nothing to connect to) until one has been set, either via
// config.h's optional SCALE_MAC_ADDRESS seed or POST /scale/config.
void scaleReaderBegin();
void scaleReaderLoop();

// Cached: {weightKg, ageMs} as JSON, or {} if no reading has ever
// succeeded yet. Never blocks -- always returns whatever's cached.
String scaleReadingJson();
bool scaleHasReading();

// Runtime scale MAC configuration -- persisted via Preferences, no
// reflash required. scaleSetMac validates "XX:XX:XX:XX:XX:XX" hex
// format, returning false (and leaving the existing config untouched)
// on a malformed address; on success it also resets the read timer so
// a connect attempt happens on the very next loop() pass.
bool scaleSetMac(const String& mac);
String scaleGetMac();
