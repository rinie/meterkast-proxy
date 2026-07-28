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
`platformio.ini` has one `[env:...]` per board (board-agnostic sketch, but
PlatformIO still wants a named board target); add another for any other
ESP32 board the same way.

**Generic ESP32-C6 boards** (`esp32-c6-devkitc-1` env, CH343 USB-serial
bridge) need an extra step and their own isolated PlatformIO cache dir --
see the comments on that `[env:...]` in `platformio.ini` for the real
reasons (a NimBLE/core version mismatch, and the platform package
colliding with `m5stick-c`'s in the default shared cache):
```
$env:PLATFORMIO_CORE_DIR = "<separate-path>"   # PowerShell; use export on macOS/Linux
pio run -e esp32-c6-devkitc-1 -t upload --upload-port COMx
```

**Either way:**
1. Copy `src/config.h.example` to `src/config.h`. `config.h` is
   gitignored — personal, never committed, the same treatment
   meterkast-dns's own `.env`/`device-playlist.toml` get. WiFi credentials
   are optional in it now (see below) — no editing required to get to a
   working build.
2. Build + upload.
3. **First boot with no saved WiFi:** the device starts an open access
   point named after `DEVICE_HOSTNAME` (default `meterkast-proxy`).
   Connect to it from any phone/laptop and a setup page opens
   automatically (or open `http://192.168.4.1/` manually); enter your
   real WiFi SSID/password there. Credentials are saved to the device's
   NVS flash (not `config.h`) and reused on every future boot — no
   reflash needed to join a network, and none needed to move the device
   to a different network later (`POST /wifi/reset` clears the saved
   credentials and reboots back into this setup AP). If the device is
   tethered over USB instead, sending a serial line
   `wifi:{"ssid":"...","password":"..."}` (e.g. `pio device monitor`'s
   input, or any serial terminal) does the same thing without needing to
   join the setup AP at all.
4. Once connected, `http://<DEVICE_HOSTNAME>.local/` (or the IP printed
   over serial) shows the status page.

## Endpoints

| Path | Shape | Notes |
|---|---|---|
| `GET /` | HTML | Tasmota-style status page: uptime, free heap, WiFi RSSI, device counts |
| `GET /scan/ble` | JSON array | `{address, name?, rssi, ageMs}` per device seen, continuous background scan |
| `GET /scan/mdns` | JSON array | `{serviceType, hostname, ip, port}` per entry, refreshed every `MDNS_QUERY_INTERVAL_MS` |
| `GET /scale/read` | JSON object | `{weightKg, ageMs}` buffered (never a live BLE round trip), or `{}` if no scale MAC is configured or no read has succeeded yet -- see below |
| `GET /scale/discover?prefix=` | JSON array | Same shape as `/scan/ble`, filtered to addresses starting with `prefix` (default `E4:54:EB`, the Medisana range) -- finds the scale's MAC without hardcoding a guess; step on the scale, then poll this |
| `GET /scale/config` | JSON object | `{"mac":"E4:54:EB:.."}` or `{"mac":null}` -- the currently configured scale MAC |
| `POST /scale/config` | JSON object | Body `{"mac":"E4:54:EB:.."}` sets the scale MAC at runtime (persisted to NVS, no reflash); `400` on a malformed address |
| `POST /wifi/reset` | JSON object | Clears the saved WiFi credentials and reboots back into the setup captive portal -- see [Setup](#setup) |
| `GET /status` | JSON object | compact health-check shape |

## Known real limitations (stated, not hidden)

- **The setup captive portal ([WiFiProvisioner](https://github.com/SanteriLindfors/WiFiProvisioner))
  has the usual cross-OS captive-portal quirks** -- inherent to the DNS-hijack
  technique itself (see that library's own testing notes), not specific to
  this integration: some OS/browser combinations may not auto-pop the setup
  page, requiring `http://192.168.4.1/` to be opened manually. The setup AP
  is open (no password) by design, the same tradeoff every consumer IoT
  device's first-time-setup AP makes.
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
- **`/scale/read` targets a Medisana BS440-family scale (BS410/BS430/
  BS440/BS444) by default** — this device has no standard Bluetooth SIG
  weight-scale profile at all; every UUID and the trigger command it
  needs come from community reverse-engineering (openScale wiki,
  `keptenkurk/BS440`, `ha-medisana-scale`, cross-referenced across all
  three, not an official spec). The protocol itself is also more
  involved than a simple read: subscribe to indications on three
  characteristics, write a trigger command to a fourth, then wait for
  the weight measurement to arrive asynchronously as an indication (see
  `scale_reader.cpp` for the full writeup). Only the weight field is
  decoded — body composition (fat/water/muscle/bone %, present in the
  same feature-characteristic packet this already subscribes to) is
  real, parked follow-up work. For a different scale entirely, override
  every `SCALE_*` UUID/command in `config.h` and expect to reverse-engineer
  its own protocol from scratch. Real-build-verified (compiles clean,
  confirmed against the real installed NimBLE-Arduino 2.5.0 headers,
  including its real `subscribe()`/`writeValue()` signatures) but not
  yet tested against the actual physical scale.
