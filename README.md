# meterkast-proxy

BLE + mDNS discovery on an ESP32, served as JSON, plus a small
Tasmota-style status page — a hardware proxy for
[meterkast-dns](https://github.com/rinie/meterkast-dns) to eventually
pull from, the same "fetch a hub's own endpoint, map to candidates"
pattern `dirigera-adapter.js` already uses on the Node side. Named
`meterkast-proxy` for now, deliberately without a board name in it (this
side is board-agnostic — see below); `home-device-playlist` has also
been floated as a possibly more natural name later.

**Status: unverified sketch.** Written without access to an ESP32
toolchain — a real first draft to open in Arduino IDE and iterate on, not
flash-tested against real hardware yet.

Board-agnostic on purpose: this first skeleton uses no GPIOs at all (no
display, no external RF receiver), so it should build for whichever
ESP32 board you point Arduino IDE at. Board-specific pin wiring only
becomes necessary once
[pulsetape](https://github.com/rinie/pulsetape)-style 433 MHz capture
gets added as a third scanner alongside these two.

## Setup

1. Arduino IDE, board package **arduino-esp32** (3.x).
2. Library Manager: install **NimBLE-Arduino** (h2zero) and
   **ArduinoJson** (bblanchon, v7). `ESPmDNS`/`WebServer`/`WiFi` are
   bundled with the arduino-esp32 core already.
3. Copy `src/config.h.example` to `src/config.h`, fill in your real WiFi
   credentials. `config.h` is gitignored — personal, never committed, the
   same treatment meterkast-dns's own `.env`/`device-playlist.toml` get.
4. Open `meterkast-proxy.ino`, select your board, Upload.
5. Once connected, `http://<DEVICE_HOSTNAME>.local/` (or the IP printed
   over serial) shows the status page.

## Endpoints

| Path | Shape | Notes |
|---|---|---|
| `GET /` | HTML | Tasmota-style status page: uptime, free heap, WiFi RSSI, device counts |
| `GET /scan/ble` | JSON array | `{address, name?, rssi, ageMs}` per device seen, continuous background scan |
| `GET /scan/mdns` | JSON array | `{serviceType, hostname, ip, port}` per entry, refreshed every `MDNS_QUERY_INTERVAL_MS` |
| `GET /status` | JSON object | compact health-check shape |

## Known real limitations (stated, not hidden)

- **BLE scanning is continuous and passive/active** (NimBLE's own
  background task) — `MAX_TRACKED_DEVICES` (100, in `ble_scanner.cpp`)
  bounds memory by evicting the oldest-seen entry once full.
- **mDNS querying is not a true browse.** Arduino-ESP32's `ESPmDNS`
  library has no wildcard "what services exist" meta-query — only "ask
  about this one specific service type" (`MDNS.queryService(type,
  proto)`), so `MDNS_SERVICE_TYPES` in `config.h` has to name every type
  you care about up front. Each query call also blocks briefly (a few
  seconds worst case across all configured types once per
  `MDNS_QUERY_INTERVAL_MS`) — this pauses the main Arduino loop (so the
  webserver/mDNS timer stall briefly) but not BLE scanning, which runs in
  its own separate task.
- **NimBLE-Arduino API version**: written against the current v2.x
  callback API (`NimBLEScanCallbacks`/`setScanCallbacks`). Older v1.x
  installs use `NimBLEAdvertisedDeviceCallbacks`/`setAdvertisedDeviceCallbacks`
  instead — check your installed library version if `ble_scanner.cpp`
  doesn't compile as-is.
