#include "matter_bridge.h"
#include "mija_thermometer.h"
#include "ble_scanner.h"
#include <Matter.h>

namespace {

// Fixed endpoint topology -- MIJA_SLOT_COUNT temperature+humidity pairs,
// always registered regardless of whether a slot has a configured MAC
// yet (Matter endpoints are a structural part of this device's identity,
// not something that should appear/disappear based on runtime BLE
// config; an unconfigured slot just never gets a reading above its
// begin() default of 0.0, the Matter equivalent of every other
// not-yet-configured feature in this project reporting an empty/default
// state rather than omitting the endpoint).
MatterTemperatureSensor tempSensors[MIJA_SLOT_COUNT];
MatterHumiditySensor humiditySensors[MIJA_SLOT_COUNT];

// Only push an update to Matter when the decoded value actually changed
// -- avoids a redundant attribute report (and the resulting network
// traffic to every subscribed Matter controller) every loop() pass.
float lastReportedTemperatureC[MIJA_SLOT_COUNT] = {};
float lastReportedHumidityPercent[MIJA_SLOT_COUNT] = {};

bool matterStarted = false;

void onMatterEvent(matterEvent_t event, const chip::DeviceLayer::ChipDeviceEvent*) {
  // Matter's own BLE (used only for commissioning) has released the
  // radio -- safe to give it back to ble_scanner.cpp's continuous scan.
  if (event == MATTER_BLE_DEINITIALIZED) {
    bleScannerResume();
  }
}

}  // namespace

void matterBridgeBegin() {
  for (size_t i = 0; i < MIJA_SLOT_COUNT; i++) {
    tempSensors[i].begin(0.0);
    humiditySensors[i].begin(0.0);
  }
  Matter.onEvent(onMatterEvent);
}

void matterBridgeLoop() {
  if (!matterStarted) return;

  for (size_t i = 0; i < MIJA_SLOT_COUNT; i++) {
    if (!mijaSlotHasReading(i)) continue;

    float temperatureC = mijaSlotTemperatureC(i);
    if (temperatureC != lastReportedTemperatureC[i]) {
      tempSensors[i].setTemperature(temperatureC);
      lastReportedTemperatureC[i] = temperatureC;
    }

    float humidityPercent = mijaSlotHumidityPercent(i);
    if (humidityPercent != lastReportedHumidityPercent[i]) {
      humiditySensors[i].setHumidity(humidityPercent);
      lastReportedHumidityPercent[i] = humidityPercent;
    }
  }
}

bool matterBridgeStartCommissioning() {
  if (matterStarted) return false;

  // KNOWN LIMITATION: Matter.begin() hangs here -- confirmed to be a
  // structural NimBLE-Arduino/Matter BLE-transport conflict, not fixed by
  // this pause. See the full writeup on this function's declaration in
  // matter_bridge.h and the README's "Known real limitations".
  bleScannerPause();
  Matter.begin();
  matterStarted = true;

  // Already commissioned (a repeat call, or a persisted fabric from
  // before) -- Matter never touches BLE in that case, so nothing will
  // ever fire MATTER_BLE_DEINITIALIZED to resume the scan otherwise.
  if (Matter.isDeviceCommissioned()) {
    bleScannerResume();
  } else {
    Serial.println("Matter accessory is not commissioned yet.");
    Serial.println("Commission it from your Matter hub app with:");
    Serial.printf("  Manual pairing code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("  QR code URL: %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
  return true;
}

bool matterBridgeIsStarted() {
  return matterStarted;
}

bool matterBridgeIsCommissioned() {
  return matterStarted && Matter.isDeviceCommissioned();
}

String matterBridgeManualPairingCode() {
  return Matter.getManualPairingCode();
}

String matterBridgeOnboardingQrCodeUrl() {
  return Matter.getOnboardingQRCodeUrl();
}
