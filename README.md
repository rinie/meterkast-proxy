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
If the board has a second USB connector wired directly to the C6's own
native USB-Serial/JTAG peripheral (VID:PID `303A:1001`) rather than the
CH343 bridge (`1A86:55D3`), use that COM port instead -- confirmed live
~3.3x faster (1829.6 kbit/s vs ~550-560 kbit/s over the CH343 one).

That same board also has an opt-in `esp32-c6-devkitc-1-zigbee` env for
`GET /scan/zigbee` (see [Known real limitations](#known-real-limitations-stated-not-hidden)
for what it actually reports) -- same isolated-cache requirement as
above, different env name:
```
$env:PLATFORMIO_CORE_DIR = "<separate-path>"
pio run -e esp32-c6-devkitc-1-zigbee -t upload --upload-port COMx
```

...and an `esp32-c6-devkitc-1-matter` env, bridging two configurable
Xiaomi Mijia (ATC-firmware) BLE thermometers as Matter
temperature/humidity endpoints -- **known to build and boot fine but not
to actually commission**, see
[Known real limitations](#known-real-limitations-stated-not-hidden):
```
$env:PLATFORMIO_CORE_DIR = "<separate-path>"
pio run -e esp32-c6-devkitc-1-matter -t upload --upload-port COMx
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
| `GET /scan/ble` | JSON array | `{address, name?, rssi, ageMs, serviceData?}` per device seen, continuous background scan. `serviceData` (present only when the advertisement carries any) is `{serviceUuid: hex, ...}` raw bytes -- generic capture, decoding is meterkast-dns's job (see below) |
| `GET /scan/mdns` | JSON array | `{serviceType, hostname, ip, port}` per entry, refreshed every `MDNS_QUERY_INTERVAL_MS` -- includes Matter-over-WiFi nodes if `_matter`/`_matterc` are in `MDNS_SERVICE_TYPES` (the default `config.h.example` already has both) |
| `GET /scan/zigbee` | JSON array | `{panId, extendedPanId, channel, permitJoining, routerCapacity, endDeviceCapacity}` per nearby Zigbee network -- `esp32-c6-devkitc-1-zigbee` build only, `[]` on every other board/env, see below |
| `GET /ble/discover?prefix=` | JSON array | Same shape as `/scan/ble`, filtered to addresses starting with `prefix` (default `E4:54:EB`) -- finds an unknown device's MAC without a dedicated active scan; empty `prefix` matches everything |
| `POST /gatt/session` | JSON object | Generic BLE GATT session: body `{"address":"..","serviceUuid":"..","write":{"characteristicUuid":"..","hex":"..","delayMs":..} (optional),"read":["charUuid",...]}`. Connects; if `write` is given, writes those bytes to that characteristic and waits `delayMs` before continuing (a hard error if that characteristic is missing/not writable, or the write itself fails); reads each listed characteristic; disconnects. Returns `{"ok":true,"readings":{"charUuid":"hexbytes",...}}` or `{"ok":false,"error":".."}`. No device-specific knowledge here at all -- see below |
| `GET /mija/discover?prefix=` | JSON array | Same shape as `/scan/ble`, filtered to addresses starting with `prefix` (default `A4:C1:38`, the Xiaomi/ATC range) -- finds a Mijia thermometer's MAC the same way `/ble/discover` does |
| `GET /mija/read?slot=` | JSON object | `{temperatureC, humidityPercent, ageMs}` for that slot, decoded from its BLE advertisement (buffered, no live round trip), or `{}` if unconfigured/no reading yet -- verifiable independent of Matter commissioning |
| `GET /mija/config?slot=` | JSON object | `{"mac":"A4:C1:38:.."}` or `{"mac":null}` -- the configured MAC for that thermometer slot (`esp32-c6-devkitc-1-matter` has 2 slots, default `?slot=0`) |
| `POST /mija/config?slot=` | JSON object | Body `{"mac":"A4:C1:38:.."}` sets that slot's MAC at runtime (persisted to NVS, no reflash); `400` on a malformed address or out-of-range slot |
| `POST /matter/commission` | JSON object | Starts the Matter stack (on demand, not automatic at boot -- see below); **currently hangs, known limitation** |
| `GET /matter/status` | JSON object | `{started, commissioned, manualPairingCode, qrCodeUrl}` -- real values on `esp32-c6-devkitc-1-matter`, empty/false on every other build |
| `POST /wifi/reset` | JSON object | Clears the saved WiFi credentials and reboots back into the setup captive portal -- see [Setup](#setup) |
| `GET /status` | JSON object | compact health-check shape, incl. `{"app":"meterkast-proxy","version":"<branch>@<hash>[+uncommitted],commit=<ts>,built=<ts>"}` -- confirms both that this is the proxy and exactly which build is running |

## Status display

Boards with an onboard screen (`m5stick-c`, `esp32-c6-waveshare-matter`,
`esp32-s3-sensecap-indicator`) show plain text -- no graphics, boot-log
style, per the actual ask -- on it: `meterkast-proxy` / `connecting...`
while WiFi is joining, then the device's own IP address once connected
(`src/status_display.h` and its three real `status_display_*.cpp`
implementations plus a no-op `status_display_stub.cpp` for every other
board -- same real-vs-stub `build_src_filter` split as
`zigbee_scanner.cpp`/`matter_bridge.cpp`, for the same PlatformIO Library
Dependency Finder reason). Each implementation's own header comment has the
hardware wiring/library reasoning and, for the Waveshare and SenseCAP
boards, the real sources it was verified against:
- `m5stick-c`: M5Unified, the board's own built-in display -- no surprises.
- `esp32-c6-waveshare-matter`: a JD9853 panel, driven via Arduino_GFX's
  `Arduino_ST7789` class (JD9853 speaks a close-enough-compatible command
  set) with pins, a full gamma/voltage init table, and PWM backlight
  control ported from [Volos Projects' own published example for this
  exact board](https://github.com/VolosR/WaveShareC6lvglexample). A
  different pin mapping and register-unlock sequence, sourced from a
  GitHub discussion that also names this board, was tried first and
  confirmed live -- on this specific physical unit -- to leave the panel
  backlit but showing nothing, not even a plain full-screen color fill;
  see `status_display_waveshare.cpp`'s own header comment for the full
  story. Real lesson: for oddball display boards, a second independent,
  actually-tested source beats a single forum post, even one that names
  the exact board and claims to be benchmark-confirmed.
- `esp32-s3-sensecap-indicator`: an ST7701S 480x480 RGB/DPI panel, driven
  directly by the ESP32-S3's own LCD_CAM peripheral -- **not** by the
  board's separate RP2040 co-processor, which earlier revisions of this
  README (and this project's own first research pass) incorrectly assumed
  owned the display; Seeed's own official Arduino guide and openHASP's
  board profile both drive it straight from the S3. Its panel's own
  init-command CS line routes through this board's PCA9535 I2C GPIO
  expander, not a plain GPIO. Seeed's own published example passes a
  PCA95x5 port constant straight in as Arduino_GFX's CS argument -- looks
  reasonable, but confirmed live it doesn't work: Arduino_GFX's databus
  classes treat CS as a plain GPIO number with no expander awareness at
  all, so that constant just toggles an unrelated real pin and the
  panel's init sequence never lands (backlit, but blank -- the exact
  same failure another user hit and worked around with unpublished
  custom code, per
  [this GitHub discussion](https://github.com/moononournation/Arduino_GFX/discussions/334)).
  `status_display_sensecap.cpp` has its own small `Arduino_DataBus`
  subclass instead: real-GPIO bit-banged SCK/MOSI plus raw I2C register
  writes to the expander for CS, following the same register map as
  Arduino_GFX's own bundled (but pin-incompatible) `Arduino_XL9535SWSPI`
  reference class. Also needed a non-zero `bounce_buffer_size_px` on the
  RGB panel -- confirmed live, without it the screen flickered/flashed
  roughly once a second while WiFi was active (PSRAM bus contention
  between the LCD DMA and WiFi, a known ESP32-S3 RGB-panel class of
  issue).

## Known real limitations (stated, not hidden)

- **The setup captive portal ([WiFiProvisioner](https://github.com/SanteriLindfors/WiFiProvisioner))
  has the usual cross-OS captive-portal quirks** -- inherent to the DNS-hijack
  technique itself (see that library's own testing notes), not specific to
  this integration: some OS/browser combinations may not auto-pop the setup
  page, requiring `http://192.168.4.1/` to be opened manually. The setup AP
  is open (no password) by design, the same tradeoff every consumer IoT
  device's first-time-setup AP makes.
- **Future option, not planned now: dropping the captive portal to save
  flash on `m5stick-c`.** That's the one env actually tight on space --
  confirmed live at 90% flash used (1,179,177 / 1,310,720 bytes, its
  default partition), versus 20-70% free on every C6/S3 env's 3MB+
  partition. WiFiProvisioner's own footprint there is roughly 40-60KB
  (its 36KB embedded HTML page plus `DNSServer` plus handler code -- it
  reuses the same `WebServer` class `web_server.cpp`'s own `/status`
  endpoint already needs, so no duplicate HTTP stack), a real chunk of
  the ~131KB currently left. The tradeoff: without the portal, a
  field-deployed `m5stick-c` with no saved WiFi could only be
  reconfigured over USB serial, not by connecting a phone to its setup
  AP -- the whole point of the runtime-provisioning work this project
  started with. Worth revisiting only if `m5stick-c` actually needs more
  flash headroom later (or just give it a bigger partition scheme, the
  same fix already used for the C6/S3 envs, instead).
- **`/scan/zigbee` reports nearby Zigbee *networks*, not their member
  devices** -- coordinators/PANs (PAN ID, channel, whether they're open
  to joins), the Zigbee equivalent of a WiFi network scan. Zigbee has no
  "see all nearby device traffic" mode the way BLE does; actually
  enumerating a network's member devices means joining it, a real,
  consequential action on someone's actual mesh (visible to its
  coordinator, occupies a device slot) that this deliberately never does
  on its own. Only ships in the separate `esp32-c6-devkitc-1-zigbee` env
  (needs the C6's 802.15.4 radio plus a dedicated build flag and
  partition table -- see `platformio.ini`); also note that build shares
  the C6's one 2.4GHz radio across BLE + WiFi + Zigbee via ESP-IDF's
  software coexistence scheduler, so scan cadence across all three may
  soften somewhat under concurrent load.
- **Matter discovery (via mDNS) works; being a real Matter accessory
  doesn't, yet.** Two separate things:
  - *Discovery*: `_matter._tcp` (operational nodes) and `_matterc._udp`
    (commissionable nodes actively seeking pairing) are just two more
    service types `mdns_browser.cpp` already knows how to browse, so
    Matter-over-WiFi nodes show up in `/scan/mdns` on any board. This
    works. Matter-over-Thread devices aren't visible this way -- they'd
    need this device to join their Thread mesh directly, out of scope.
  - *Accessory* (`esp32-c6-devkitc-1-matter`, arduino-esp32's built-in
    `Matter` library -- real, not ESP-IDF-only the way this README
    previously assumed): endpoint registration works fine
    (`matterBridgeBegin()`, confirmed live -- both `MatterTemperatureSensor`
    endpoints and both `MatterHumiditySensor` endpoints create cleanly),
    and the device boots and serves HTTP normally. **`POST
    /matter/commission` (which starts the actual Matter stack) hangs
    indefinitely, confirmed live and not yet resolved.** Root cause,
    pinned down with verbose (`CORE_DEBUG_LEVEL=5`) serial logging: after
    `ble_scanner.cpp`'s continuous BLE scan is paused, Matter's own BLE
    commissioning transport logs `NimBle host synced` -- the same message
    NimBLE-Arduino's own init already logged once at boot -- then hangs
    forever. This points to a structural conflict over the single shared
    underlying NimBLE host stack between NimBLE-Arduino and Matter's BLE
    transport, not a resolvable application-level timing/coexistence
    issue (pausing/resuming the scan, which `matter_bridge.cpp` still
    does, was the first fix attempted and did not help). Real fixes would
    need either dropping BLE scanning entirely from this build (defeats
    bridging the thermometers, the actual point), a deeper custom
    integration sharing one NimBLE host between both roles, or disabling
    Matter's BLE commissioning (`CONFIG_ENABLE_CHIPOBLE`) -- confirmed
    hard-baked into the only precompiled `sdkconfig` this platform ships
    for the C6 (checked both flash-mode variants), so that would need a
    from-source ESP-IDF/`esp-matter` rebuild. None attempted. See
    `matter_bridge.h`'s `matterBridgeStartCommissioning()` for the full
    writeup.
- **`mija_thermometer.cpp` decodes Xiaomi Mijia BLE thermometers,
  real-verified against actual hardware** -- a device on this LAN
  advertising under UUID `0xFE95` (Xiaomi's native MiBeacon protocol, not
  either atc1441/pvvx `0x181A` format also supported) decoded to
  `{"temperatureC":24.81,"humidityPercent":53.81}`, physically plausible
  and stable across repeated polls. Encrypted MiBeacon frames aren't
  decoded (would need a per-device bindkey this project has no way to
  configure) -- that device's reading just never updates. A single
  MiBeacon advertisement only ever carries one of {temperature,
  humidity}, not both at once, so the two fields update independently as
  the device round-robins which it reports.
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
- **The `m5stick-c` env's mDNS *querying* reports zero results, on real
  hardware, confirmed not fixed by rebooting.** `GET /scan/mdns` returns
  `[]` and `GET /status`'s `mdnsDeviceCount` stays `0` from the very
  first query after boot (not a gradual degradation -- confirmed live
  immediately after a real hardware reset, `uptimeMs` under 15 seconds).
  WiFi, BLE, and the HTTP server all work normally on the same board at
  the same time, and `wifi_setup.cpp`'s own `MDNS.begin(DEVICE_HOSTNAME)`
  call (the *responder*, advertising this device's own name -- a
  separate mechanism from `mdns_browser.cpp`'s own *querying*) succeeds,
  confirmed by a second, real `esp32-c6-devkitc-1` board correctly seeing
  this board's own hostname in *its* `/scan/mdns`. So this is
  specifically the querying side failing to receive any answers, most
  likely the WiFi station interface not joining the mDNS multicast group
  (`224.0.0.251`) at the IGMP level -- a real, documented class of issue
  in some `arduino-esp32` core versions, plausible here since `m5stick-c`
  builds against the official `platform-espressif32@6.13.0` core while
  the working `esp32-c6-devkitc-1` envs build against a completely
  different chip and the `pioarduino` fork's own core. Not yet root-caused
  further (would need verbose `CORE_DEBUG_LEVEL=5` serial logging, the
  same technique that pinned down the Matter/BLE coexistence issue
  above) -- left as a known, real, unresolved limitation specific to this
  one board/core combination, not something in this project's own
  `mdns_browser.cpp` logic, which is confirmed correct. Practical
  workaround on the meterkast-dns side: put a working proxy board first
  in `METERKAST_PROXY_HOSTS`, since mDNS resolution for already-claimed
  entries only ever queries the first configured proxy.
- **NimBLE-Arduino API version**: written against the current v2.x
  callback API (`NimBLEScanCallbacks`/`setScanCallbacks`). Older v1.x
  installs use `NimBLEAdvertisedDeviceCallbacks`/`setAdvertisedDeviceCallbacks`
  instead — check your installed library version if `ble_scanner.cpp`
  doesn't compile as-is.
- **The `m5stick-c`/generic-relay path has no device-specific BLE
  knowledge at all.** `POST /gatt/session` and `serviceData` on
  `/scan/ble` are both fully generic; which real device a MAC belongs
  to, what its service/characteristic UUIDs mean, and how to decode the
  bytes are meterkast-dns's own playlist's job now (see the companion
  write-up in that repo). This path doesn't know or care whether it's
  talking to a thermometer, a scale, or anything else. The
  `esp32-c6-devkitc-1-matter` env is a deliberate exception to that rule
  -- `mija_thermometer.cpp` bakes in real Xiaomi Mijia decode logic
  directly, because a Matter accessory has to expose an already-decoded
  value at the endpoint itself, with no meterkast-dns in the loop at
  all; see that section below.
- **`/gatt/session` supports one optional write-then-read step, still no
  subscribe/wait-for-indication.** An optional `write` object
  (`{"characteristicUuid":"..","hex":"..","delayMs":..}`) writes a
  trigger value to one characteristic and waits the given delay before
  the read loop runs -- motivated by a real, common device shape: a
  Xiaomi Mi Flora plant sensor's real-time-data characteristic reads
  back stale/default values until you write `0xA01F` to its mode-switch
  characteristic first. **Real-verified only at the mechanism level, not
  yet against a plausible Mi Flora reading**: flashed to the real
  M5StickC and round-tripped once against a real Mi Flora sensor
  (`c4:7c:8d:65:d2:d3`) -- the write succeeded, both requested
  characteristics came back (`ok:true`), proving the write-then-read
  plumbing itself works end to end. The actual bytes read back
  (`aabbccddeeff99887766000000000000`) don't look like real sensor data
  though -- decoded against the widely-documented community byte layout
  they'd imply a physically impossible ~4804°C, so either this specific
  characteristic needs a longer settle time, a notification/CCCD
  subscription first (some peripherals need that before a direct read
  reflects triggered data, a nuance this endpoint doesn't support yet),
  or the byte layout assumed for this exact hardware revision is wrong.
  Two follow-up connection attempts both failed outright
  (`{"ok":false,"error":"connect failed"}`) as the sensor's own
  advertised RSSI dropped to -92 -- plausibly just weak signal at BLE's
  connection-vs-passive-scan link-budget difference, not a code issue,
  but not independently confirmed either. Getting a real, decodable Mi
  Flora reading is real, honest follow-up work, not done here. That's
  different from what a Medisana BS440-family scale needs: its real
  measurement arrives *asynchronously*, as a BLE indication, at a time
  no fixed delay can reliably predict (previously handled by this
  firmware directly in a now-removed `scale_reader.cpp` -- real, working
  code, reverted to this generic-relay design instead once it became
  clear a second device needed protocol logic duplicated in C++ too).
  That case is still real, planned, deferred follow-up -- extending the
  relay to support subscribe-and-wait is additive to this same endpoint,
  not a redesign.
