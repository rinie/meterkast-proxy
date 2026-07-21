#pragma once
#include <Arduino.h>

// Periodically queries every service type in MDNS_SERVICE_TYPES
// (config.h) and keeps the latest results -- unlike BLE's continuous
// passive scan, Arduino-ESP32's ESPmDNS has no wildcard "browse
// everything" query, only "ask about this one specific service type",
// so this is closer in spirit to meterkast-dns's own Node-side
// resolveService() (query, wait, take the answer) than to a live-updating
// cache. mdnsBrowserLoop() drives the interval timer from the main loop
// since (unlike BLE) there's no background task doing this on its own.
void mdnsBrowserBegin();
void mdnsBrowserLoop();

// A JSON array of the most recent query round's results: [{serviceType,
// hostname, ip, port}, ...] -- replaced wholesale each round, not merged
// with older results, since queryService() itself already returns a full
// snapshot for that type each time it's called.
String mdnsDevicesJson();
size_t mdnsDeviceCount();
