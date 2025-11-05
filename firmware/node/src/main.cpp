#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Simple ESP32 firmware (non-mesh) PoC that reads a mock moisture value and posts to gateway HTTP endpoint

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";
const char* gatewayUrl = "http://192.168.1.100:5000/api/devices/gateway-001/telemetry"; // adjust

void setup() {
  Serial.begin(115200);
  delay(1000);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");
}

void loop() {
  float moisture = random(300, 700) / 10.0; // mock
  StaticJsonDocument<256> doc;
  doc["deviceId"] = "esp32-node-001";
  doc["timestamp"] = ""; // leave blank to let backend set timestamp
  JsonArray sensors = doc.createNestedArray("sensors");
  JsonObject s = sensors.createNestedObject();
  s["name"] = "moisture1";
  s["type"] = "capacitive";
  s["value"] = moisture;
  s["unit"] = "%";
  doc["battery"] = 3.7;
  doc["rssi"] = WiFi.RSSI();
  doc["meshHopCount"] = 0;
  doc["firmwareVersion"] = "0.1.0";

  char buffer[512];
  size_t n = serializeJson(doc, buffer);

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(gatewayUrl);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST((uint8_t*)buffer, n);
    Serial.println("Posted telemetry, code=" + String(code));
    http.end();
  }
  delay(60000);
}
