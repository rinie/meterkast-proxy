#include "wifi_setup.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiProvisioner.h>
#include <ArduinoJson.h>

namespace {

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr size_t SERIAL_LINE_MAX_LEN = 256;

String serialLineBuffer;

// Bounded connect attempt against Preferences-saved credentials -- unlike a
// bare WiFi.begin() + infinite status-poll loop, this can fail and return,
// so the caller can fall through to the captive portal on bad/stale/missing
// credentials instead of hanging forever.
bool connectWithSavedCredentials() {
  Preferences prefs;
  prefs.begin(WIFI_PREFS_NAMESPACE, true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("password", "");
  prefs.end();

#ifdef WIFI_SSID
  // config.h's WIFI_SSID/WIFI_PASSWORD are an optional one-time seed --
  // only used before anything has ever been saved via the captive portal,
  // never required (see README).
  if (ssid.isEmpty()) {
    ssid = WIFI_SSID;
#ifdef WIFI_PASSWORD
    password = WIFI_PASSWORD;
#endif
  }
#endif

  if (ssid.isEmpty()) return false;

  Serial.printf("Connecting to saved WiFi: %s\n", ssid.c_str());
  if (password.isEmpty()) {
    WiFi.begin(ssid.c_str());
  } else {
    WiFi.begin(ssid.c_str(), password.c_str());
  }

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - start > WIFI_CONNECT_TIMEOUT_MS) {
      Serial.println("Saved WiFi credentials failed to connect within timeout");
      WiFi.disconnect(true);
      return false;
    }
    delay(250);
  }
  return true;
}

// AP+captive-portal fallback (WiFiProvisioner) -- open AP named after
// DEVICE_HOSTNAME, no separate setup password to hardcode. Blocks until a
// real WiFi connection succeeds.
void runProvisioningPortal() {
  WiFiProvisioner provisioner;
  provisioner.getConfig().AP_NAME = DEVICE_HOSTNAME;
  provisioner.getConfig().PROJECT_TITLE = DEVICE_HOSTNAME;
  provisioner.getConfig().PROJECT_SUB_TITLE = "WiFi setup";
  provisioner.getConfig().SHOW_INPUT_FIELD = false;
  provisioner.getConfig().SHOW_RESET_FIELD = true;

  provisioner.onSuccess([](const char* ssid, const char* password, const char*) {
    Preferences prefs;
    prefs.begin(WIFI_PREFS_NAMESPACE, false);
    prefs.putString("ssid", String(ssid));
    prefs.putString("password", password ? String(password) : String());
    prefs.end();
    Serial.println("WiFi credentials saved.");
  });

  provisioner.onFactoryReset([]() {
    Preferences prefs;
    prefs.begin(WIFI_PREFS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    Serial.println("WiFi credentials cleared via portal factory reset.");
  });

  Serial.printf("Starting WiFi setup portal \"%s\" -- connect to it and open http://192.168.4.1/\n", DEVICE_HOSTNAME);
  provisioner.startProvisioning();
}

// Handles one complete line read from Serial by wifiSetupLoop() below --
// expects `wifi:{"ssid":"...","password":"..."}`. Saves to the same
// Preferences namespace the portal uses and reboots to apply it, so a
// tethered device doesn't need to join its own setup AP to reconfigure.
void handleSerialLine(const String& line) {
  if (!line.startsWith("wifi:")) return;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, line.substring(5));
  if (err || !doc["ssid"].is<const char*>()) {
    Serial.println("wifi: command needs JSON like {\"ssid\":\"...\",\"password\":\"...\"}");
    return;
  }

  String ssid = doc["ssid"].as<const char*>();
  if (ssid.isEmpty()) {
    Serial.println("wifi: command needs a non-empty \"ssid\"");
    return;
  }
  String password = doc["password"].is<const char*>() ? doc["password"].as<const char*>() : "";

  Preferences prefs;
  prefs.begin(WIFI_PREFS_NAMESPACE, false);
  prefs.putString("ssid", ssid);
  prefs.putString("password", password);
  prefs.end();

  Serial.printf("WiFi credentials for \"%s\" saved via serial, rebooting...\n", ssid.c_str());
  delay(200);  // let the message actually flush before the reboot cuts the connection
  ESP.restart();
}

}  // namespace

void wifiSetupBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);

  if (!connectWithSavedCredentials()) {
    runProvisioningPortal();
  }

  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS responder started: http://%s.local/\n", DEVICE_HOSTNAME);
  } else {
    Serial.println("mDNS responder failed to start (browsing for other devices still works)");
  }
}

void wifiSetupLoop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      serialLineBuffer.trim();
      if (serialLineBuffer.length() > 0) handleSerialLine(serialLineBuffer);
      serialLineBuffer = "";
    } else if (c != '\r') {
      serialLineBuffer += c;
      // Guard against unbounded growth from stray noise/binary data on the
      // line with no newline in sight -- drop and resync rather than let
      // this grow forever.
      if (serialLineBuffer.length() > SERIAL_LINE_MAX_LEN) serialLineBuffer = "";
    }
  }
}
