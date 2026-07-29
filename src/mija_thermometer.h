#pragma once
#include <Arduino.h>
#include <string>

// Decodes Xiaomi Mijia (LYWSD03MMC-family) BLE thermometers -- readings
// come straight from ble_scanner.cpp's existing passive scan (Service
// Data in the advertisement itself), no active GATT connection needed
// (unlike scale_reader.cpp). Three real wire formats exist; which one a
// given device uses depends on its firmware/configuration, told apart by
// which Service Data UUID it shows up under:
//
// - UUID 0x181A, 13-byte payload: atc1441 original custom-firmware
//   format (MAC + int16 temp x0.1C + uint8 humidity% + uint8 battery% +
//   uint16 battery mV + uint8 counter).
// - UUID 0x181A, 15-byte payload: pvvx's extended "custom" format (MAC +
//   int16 temp x0.01C + uint16 humidity x0.01% + uint16 battery mV +
//   uint8 battery level + uint8 counter + uint8 flags).
// - UUID 0xFE95: Xiaomi's own native MiBeacon protocol (what stock
//   firmware -- and ATC firmware set to "Mi Like" advertising -- both
//   use). Confirmed live: a real ATC-named device on this network
//   ("ATC_F28AFA") turned out to broadcast this format, not either
//   0x181A one, hence needing this too. Real, documented format
//   (Xiaomi's own MiBeacon protocol v5 + Object Definition specs,
//   pvvx/ATC_MiThermometer's InfoMijiaBLE docs): a variable-length frame
//   (Frame Control flags gate which fields are present) carrying at most
//   one {Object ID, length, data} triplet per advertisement -- so a
//   device round-robins which single value (temperature 0x1004,
//   humidity 0x1006, battery, ...) it reports each cycle, not both at
//   once. Encrypted frames (Frame Control bit 3) aren't decoded -- that
//   needs a per-device bindkey this project has no way to configure --
//   so an encrypted device's reading just never updates.
//
// Called from ble_scanner.cpp's single NimBLE scan callback (NimBLE only
// supports one registered callback) whenever an advertisement carries
// Service Data under either UUID -- see the comment there.
void mijaHandleAdvertisement(const String& address, uint16_t serviceDataUuid, const std::string& serviceData);

// A fixed number of configurable "slots" -- Matter endpoints must be
// registered before Matter.begin() starts (no dynamic add-later API in
// this Arduino wrapper), so this bridges a small, known set of real
// thermometers rather than an open-ended discovered list. Each slot's
// backing MAC is runtime-settable (matter_bridge.cpp's /mija/config),
// mirroring scale_reader.cpp's scaleSetMac()/scaleGetMac() pattern.
constexpr size_t MIJA_SLOT_COUNT = 2;

// Loads each slot's MAC from Preferences, falling back to an optional
// config.h seed (MIJA_THERMOMETER_MAC_0/_1) for a slot with nothing
// saved yet -- same optional-seed treatment WIFI_SSID/SCALE_MAC_ADDRESS
// already get.
void mijaThermometerBegin();

bool mijaSetSlotMac(size_t slot, const String& mac);
String mijaGetSlotMac(size_t slot);

// Cached: true only if this slot has a configured MAC and a reading has
// actually been decoded for it. temperatureC/humidityPercent/ageMs are
// only meaningful when this returns true. Since a single advertisement
// only ever carries one of {temperature, humidity} for the MiBeacon
// (0xFE95) format, each field updates independently -- ageMs reflects
// whichever field was decoded most recently, not necessarily both.
bool mijaSlotHasReading(size_t slot);
float mijaSlotTemperatureC(size_t slot);
float mijaSlotHumidityPercent(size_t slot);
unsigned long mijaSlotAgeMs(size_t slot);
