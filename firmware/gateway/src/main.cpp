#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Gateway PoC: receives HTTP from local nodes and forwards to Azure IoT Hub via HTTPS using IoT Hub REST API (SAS token required)

const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

const char* iothubHost = "myiothub.azure-devices.net"; // replace
const char* deviceId = "esp32-gateway";
const char* sasToken = "SharedAccessSignature sr=..."; // generate and set

AsyncWebServer server(80);

void forwardToIoTHub(const String &payload) {
  HTTPClient http;
  String url = String("https://") + iothubHost + "/devices/" + deviceId + "/messages/events?api-version=2018-06-30";
  http.begin(url);
  http.addHeader("Authorization", sasToken);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(payload);
  Serial.println("IoT Hub forward code: " + String(code));
  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" connected");

  server.on("/telemetry", HTTP_POST, [](AsyncWebServerRequest *request){
    // not used; body handled in onBody
    request->send(200);
  });

  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (request->url() == "/telemetry") {
      String body;
      body.reserve(total+1);
      for (size_t i=0;i<len;i++) body += (char)data[i];
      Serial.println("Received from node: " + body);
      forwardToIoTHub(body);
    }
  });

  server.begin();
}

void loop() {
  delay(1000);
}
