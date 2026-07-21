#pragma once

// A Tasmota-style status page at "/" (device info + counts, human-facing)
// plus the raw JSON the eventual meterkast-dns Node adapter will fetch:
// /scan/ble, /scan/mdns, /status. Synchronous (Arduino's built-in
// WebServer), not ESPAsyncWebServer -- fewer dependencies to start with;
// swap in the async version later if mDNS's own brief per-round blocking
// (see mdns_browser.cpp) ever becomes a real problem for concurrent
// requests.
void webServerBegin();
void webServerLoop();
