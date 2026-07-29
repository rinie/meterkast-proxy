#pragma once
#include <Arduino.h>

// Turns this device into a real, commissionable Matter accessory (Apple
// Home/Google Home/Home Assistant/etc.), bridging mija_thermometer.cpp's
// decoded readings as MatterTemperatureSensor/MatterHumiditySensor
// endpoints -- one pair per configured slot (see mija_thermometer.h for
// the slot MAC config, shared with /mija/discover and /mija/config,
// web_server.cpp). Real implementation only exists for the
// esp32-c6-devkitc-1-matter env; matter_bridge_stub.cpp provides the
// same functions as a no-op everywhere else, selected per env via
// build_src_filter rather than a preprocessor guard within one file --
// same real reason as zigbee_scanner.h/.cpp: PlatformIO's Library
// Dependency Finder pulls in a whole library from a bare #include text
// match regardless of any #ifdef around it.
//
// Registers the Matter endpoints (cheap, no BLE/network involvement) but
// does NOT start the Matter stack itself -- see
// matterBridgeStartCommissioning() below for why that's a separate,
// on-demand step.
void matterBridgeBegin();
void matterBridgeLoop();

// Starts the actual Matter stack (Matter.begin()) -- triggered on demand
// by POST /matter/commission (web_server.cpp) rather than automatically
// at boot, because Matter's default commissioning path runs its own BLE
// transport for pairing.
//
// KNOWN, CONFIRMED, UNRESOLVED LIMITATION: this currently hangs.
// Originally assumed to be a bounded application-level coexistence
// window with ble_scanner.cpp's continuous BLE central scan (pause ours,
// let Matter's have the radio, resume after) -- implemented exactly that
// (see below) and it did NOT fix it. Verbose (CORE_DEBUG_LEVEL=5) serial
// logging pinned the real cause down further: after our scan's stop()
// completes cleanly, Matter's own BLE transport logs "NimBle host
// synced" -- the same message NimBLE-Arduino's own NimBLEDevice::init()
// already logged once at boot -- then hangs forever. This points to a
// structural conflict over the single shared underlying NimBLE host
// stack between NimBLE-Arduino and Matter's BLE transport, not a
// resolvable timing/coexistence issue at the application level. Real
// fixes would need either dropping BLE scanning entirely from this
// build (defeats bridging the thermometers, the whole point here),
// deeper custom integration sharing one NimBLE host between both roles,
// or disabling Matter's BLE commissioning (CONFIG_ENABLE_CHIPOBLE) --
// confirmed hard-baked into the only precompiled sdkconfig this platform
// ships for the C6, so that would need a from-source ESP-IDF/esp-matter
// rebuild. None attempted -- see README's "Known real limitations".
//
// The pause/resume this still does (harmless, and correct in spirit for
// whenever a real fix lands) is: pause the BLE scanner (ble_scanner.h),
// start Matter, then resume the scan immediately if the device turns out
// to already be commissioned (no BLE needed), or once Matter's own BLE
// stack reports itself deinitialized (MATTER_BLE_DEINITIALIZED) --
// neither of which currently gets reached because of the hang above.
//
// Returns false if endpoints were never registered (matterBridgeBegin()
// pre-requisite) or the stack was already started.
bool matterBridgeStartCommissioning();

bool matterBridgeIsStarted();
bool matterBridgeIsCommissioned();
String matterBridgeManualPairingCode();
String matterBridgeOnboardingQrCodeUrl();
