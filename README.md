# meterkast-proxy

BLE + mDNS discovery on an ESP32, served as JSON, plus a small
Tasmota-style status page — a hardware proxy for
[meterkast-dns](https://github.com/rinie/meterkast-dns) to eventually
pull from, the same "fetch a hub's own endpoint, map to candidates"
pattern `dirigera-adapter.js` already uses on the Node side. Named
`meterkast-proxy` for now, deliberately without a board name in it (this
side is board-agnostic — see below); `home-device-playlist` has also
been floated as a possibly more natural name later.

**Status: real, running hardware.** Built, flashed, and verified live
against a real M5StickC (ESP32-PICO-D4) on the build-toolchain laptop --
joins WiFi for real, serves real `/scan/ble` and `/scan/mdns` data
(real nearby BLE devices, real mDNS services including a real Home
Assistant/domoticz/Google Cast/HomeKit instances on the LAN), and
[meterkast-dns](https://github.com/rinie/meterkast-dns) discovers
through it live. Two real bugs surfaced by the first actual compile,
both fixed: `MDNS.address()` isn't a real method on `MDNSResponder`
(the correct accessor, confirmed against the installed `ESPmDNS.h`, is
`MDNS.IP()`); and an initial custom `build_src_filter` in
`platformio.ini` silently excluded the root `.ino` from the build
entirely (converted but never compiled -- PlatformIO's default filter
picks it up correctly, so the override was dropped).

Board-agnostic on purpose: this first skeleton uses no GPIOs at all (no
display, no external RF receiver), so it should build for whichever
ESP32 board you point Arduino IDE at. Board-specific pin wiring only
becomes necessary once
[pulsetape](https://github.com/rinie/pulsetape)-style 433 MHz capture
gets added as a third scanner alongside these two.

## Setup

Two build paths work; pick whichever you already have installed. Both
read the same `src/config.h` and produce the same firmware.

**Arduino IDE**
1. Board package **arduino-esp32** (3.x).
2. Library Manager: install **NimBLE-Arduino** (h2zero) and
   **ArduinoJson** (bblanchon, v7). `ESPmDNS`/`WebServer`/`WiFi` are
   bundled with the arduino-esp32 core already.
3. Open `meterkast-proxy.ino`, select your board, Upload.

**PlatformIO** (same tool [pulsetape](https://github.com/rinie/pulsetape)
uses, for anyone who already has it set up rather than Arduino IDE)
```
pio run -e m5stick-c              build
pio run -e m5stick-c -t upload    flash (add --upload-port COMx if it can't autodetect)
pio device monitor -b 115200      serial log
```
`platformio.ini` pins the `m5stick-c` board; other ESP32 boards need
their own `[env:...]` added (board-agnostic sketch, but PlatformIO still
wants a named board target).

**Either way:**
1. Copy `src/config.h.example` to `src/config.h`, fill in your real WiFi
   credentials. `config.h` is gitignored — personal, never committed, the
   same treatment meterkast-dns's own `.env`/`device-playlist.toml` get.
2. Build + upload.
3. Once connected, `http://<DEVICE_HOSTNAME>.local/` (or the IP printed
   over serial) shows the status page.

## Endpoints

| Path | Shape | Notes |
|---|---|---|
| `GET /` | HTML | Tasmota-style status page: uptime, free heap, WiFi RSSI, device counts |
| `GET /scan/ble` | JSON array | `{address, name?, rssi, ageMs}` per device seen, continuous background scan |
| `GET /scan/mdns` | JSON array | `{serviceType, hostname, ip, port}` per entry, refreshed every `MDNS_QUERY_INTERVAL_MS` |
| `GET /scale/read` | JSON object | `{weightKg, ageMs}` buffered (never a live BLE round trip), or `{}` if no `SCALE_MAC_ADDRESS` is configured or no read has succeeded yet -- see below |
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
- **`/scale/read` decodes the Bluetooth SIG-standard Weight Scale
  Service/Weight Measurement characteristic by default** — real for any
  scale that implements the standard profile, but many cheap consumer
  scales (Xiaomi Mi Scale and similar) use a proprietary format instead.
  Override `SCALE_SERVICE_UUID`/`SCALE_CHARACTERISTIC_UUID` in
  `config.h` for a different profile; the decode itself
  (`decodeWeightMeasurement` in `scale_reader.cpp`) would need real
  reverse-engineering against that specific device's actual bytes if so.
  Real-build-verified (compiles clean, confirmed against the real
  installed NimBLE-Arduino 2.5.0 headers) but not yet tested against an
  actual scale — no specific device was available when this was
  written.
