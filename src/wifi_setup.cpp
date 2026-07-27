#include "wifi_setup.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFiProvisioner.h>

namespace {

constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

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
