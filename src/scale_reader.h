#pragma once
#include <Arduino.h>

// Periodic, buffered BLE GATT read of a single configured device (a
// weight scale) -- unlike ble_scanner.cpp's continuous passive scan,
// this is an active connect/read/disconnect cycle against one known
// address, run on SCALE_READ_INTERVAL_MS, caching the last good result
// so a request never blocks on a live BLE round trip. Disabled
// entirely (both begin and loop become no-ops) when SCALE_MAC_ADDRESS
// isn't defined in config.h -- most boards won't have a scale.
void scaleReaderBegin();
void scaleReaderLoop();

// Cached: {weightKg, ageMs} as JSON, or {} if no reading has ever
// succeeded yet. Never blocks -- always returns whatever's cached.
String scaleReadingJson();
bool scaleHasReading();
