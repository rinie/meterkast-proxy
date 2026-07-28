#pragma once
#include <Arduino.h>

// Periodic Zigbee *network* scan (the Zigbee equivalent of a WiFi network
// scan -- nearby coordinators/PANs, not their member devices) -- unlike
// ble_scanner.cpp's passive scan, Zigbee has no "see all nearby traffic"
// mode; this actively scans channels as a router/end-device candidate,
// which is also why a coordinator can't use this (a coordinator can't
// see other networks, only devices already looking to join one). Deals
// only in observation: never joins a network, which would be a real,
// consequential action on it (visible to its coordinator, occupies a
// device slot), not passive scanning -- see README for why that's
// deliberately out of scope here.
//
// Real implementation only exists for the esp32-c6-devkitc-1-zigbee env
// (see platformio.ini); zigbee_scanner_stub.cpp provides the same
// functions as a no-op for every other board/env (m5stick-c doesn't have
// the hardware at all, and even esp32-c6-devkitc-1's plain build doesn't
// have the ZIGBEE_MODE_ZCZR/_ED flag or the dedicated Zigbee partition
// table the real implementation needs). Exactly one of the two .cpp
// files is compiled per env via build_src_filter, not a preprocessor
// guard within one file -- see the comment at the top of
// zigbee_scanner.cpp for why.
void zigbeeScannerBegin();
void zigbeeScannerLoop();

// A JSON array of the most recent scan round's results: [{panId,
// extendedPanId, channel, permitJoining, routerCapacity,
// endDeviceCapacity}, ...] -- replaced wholesale each round, same
// "queryComplete() returns a full snapshot" spirit as mdns_browser.cpp.
String zigbeeNetworksJson();
size_t zigbeeNetworkCount();
