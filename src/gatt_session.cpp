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

// Inverse of toHex -- a write step's own trigger bytes arrive from
// meterkast-dns as a hex string (JSON has no byte-string type, same
// reasoning readings go out as hex), decoded back to raw bytes here
// before handing them to NimBLE's own writeValue(). Malformed input
// (odd length, non-hex characters) isn't specially guarded against --
// strtoul silently returns 0 for a non-hex substring, which just means a
// malformed write step writes wrong-but-harmless bytes rather than
// crashing; the caller is meterkast-dns's own playlist-driven profile
// code, not untrusted external input.
std::string fromHex(const String& hex) {
  std::string out;
  out.reserve(hex.length() / 2);
  for (size_t i = 0; i + 1 < hex.length(); i += 2) {
    out += static_cast<char>(strtoul(hex.substring(i, i + 2).c_str(), nullptr, 16));
  }
  return out;
}

}  // namespace

GattSessionResult gattSessionExecute(const String& address, const String& serviceUuid,
                                      const std::vector<String>& characteristicUuids,
                                      const GattWriteStep* write) {
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

  if (write) {
    NimBLERemoteCharacteristic* writeCharacteristic = service->getCharacteristic(write->characteristicUuid.c_str());
    if (!writeCharacteristic || !writeCharacteristic->canWrite()) {
      client->disconnect();
      NimBLEDevice::deleteClient(client);
      result.error = "write characteristic not found or not writable";
      return result;
    }
    // A write failing here (unlike a missing read characteristic below)
    // is a hard error, not silently skipped -- every subsequent read
    // exists specifically to observe the effect of this write, so
    // proceeding past a failed write would just return stale/zeroed
    // data as if it were real.
    if (!writeCharacteristic->writeValue(fromHex(write->hex))) {
      client->disconnect();
      NimBLEDevice::deleteClient(client);
      result.error = "write failed";
      return result;
    }
    if (write->delayMs > 0) delay(write->delayMs);
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
