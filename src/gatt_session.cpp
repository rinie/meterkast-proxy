#include "gatt_session.h"
#include <NimBLEDevice.h>

namespace {

// Raw bytes -> lowercase hex, for JSON transport (JSON has no byte-string
// type) -- same small helper ble_scanner.cpp's own service-data capture
// uses, duplicated rather than shared across a header for two call sites
// this small.
String toHex(const std::string& raw) {
  static const char* digits = "0123456789abcdef";
  String out;
  out.reserve(raw.size() * 2);
  for (unsigned char b : raw) {
    out += digits[b >> 4];
    out += digits[b & 0x0f];
  }
  return out;
}

}  // namespace

GattSessionResult gattSessionExecute(const String& address, const String& serviceUuid,
                                      const std::vector<String>& characteristicUuids) {
  GattSessionResult result;
  result.ok = false;

  NimBLEClient* client = NimBLEDevice::createClient();
  // Same bounded-timeout treatment scale_reader.cpp already proved out
  // this session: NimBLE's own defaults (30s connect timeout x 3
  // retries) could otherwise wedge this whole request -- and with it
  // the main loop()/web server -- for well over a minute against an
  // unreachable or mistyped address.
  client->setConnectTimeout(5000);
  NimBLEClient::Config clientConfig = client->getConfig();
  clientConfig.connectFailRetries = 0;
  client->setConfig(clientConfig);

  bool connected = client->connect(NimBLEAddress(address.c_str(), BLE_ADDR_PUBLIC));
  if (!connected) {
    NimBLEDevice::deleteClient(client);
    result.error = "connect failed";
    return result;
  }

  NimBLERemoteService* service = client->getService(serviceUuid.c_str());
  if (!service) {
    client->disconnect();
    NimBLEDevice::deleteClient(client);
    result.error = "service not found on this device";
    return result;
  }

  for (const String& characteristicUuid : characteristicUuids) {
    NimBLERemoteCharacteristic* characteristic = service->getCharacteristic(characteristicUuid.c_str());
    if (!characteristic || !characteristic->canRead()) continue;
    NimBLEAttValue value = characteristic->readValue();
    result.readings.push_back({characteristicUuid, toHex(std::string(value))});
  }

  client->disconnect();
  NimBLEDevice::deleteClient(client);

  result.ok = true;
  return result;
}
