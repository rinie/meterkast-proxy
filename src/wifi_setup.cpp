#include "wifi_setup.h"
#include "config.h"
#include <WiFi.h>
#include <ESPmDNS.h>

void wifiSetupBegin() {
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(DEVICE_HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected, IP: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin(DEVICE_HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS responder started: http://%s.local/\n", DEVICE_HOSTNAME);
  } else {
    Serial.println("mDNS responder failed to start (browsing for other devices still works)");
  }
}
