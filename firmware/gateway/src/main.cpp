/*********************************************************************
 * ESP32 / ESP8266 – painlessMesh + Azure IoT + Deep Sleep + OTA
 * -------------------------------------------------------
 * • One binary: ESP32 & ESP8266
 * • Gateway: Wi-Fi to Azure (HTTP/MQTT/SDK)
 * • Node: Mesh to Gateway to Deep Sleep
 * • Web UI + OTA + Sleep Interval
 * • BOOT button: Reset config
 *********************************************************************/

#include <Arduino.h>
#include <ArduinoJson.h>
#include <painlessMesh.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <set>
#include <map>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <LittleFS.h>
// #define FS_TYPE LittleFS
// #define FS_OPEN(path, mode) LittleFS.open("/littlefs", mode)
// #define FS_EXISTS(path) LittleFS.exists("/littlefs")
// #define FS_REMOVE(path) LittleFS.remove("/littlefs")

#if defined(ESP32)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "AzureIotHub.h"
#include "Esp32MQTTClient.h"
// #define FS_BEGIN() LittleFS.begin(true, "/littlefs") // MOUNT AT ROOT
#define RTC_ATTR RTC_DATA_ATTR
#define HTTP_CLIENT HTTPClient
#define WebRequest AsyncWebServerRequest
#define PIN_BOOT 0
#define BATTERY_PIN 34
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266httpUpdate.h>
#include <ESP8266HTTPClient.h>
#include <user_interface.h>
// #define FS_BEGIN() LittleFS.begin()
#define RTC_ATTR
#define HTTP_CLIENT HTTPClient
#define WebRequest AsyncWebServerRequest
#define PIN_BOOT 0
#define BATTERY_PIN A0
#else
#error "Unsupported platform"
#endif

#include <PubSubClient.h>
#include <DNSServer.h>
DNSServer dnsServer;

// --- CONSTANTS ---
#define FIRMWARE_VERSION "1.3.4"
#define AP_SSID "ESP_Config"
#define AP_PASSWORD "admin123"
#define CONFIG_TIMEOUT_MS 300000
#define MESH_PREFIX "MESH_"
#define MESH_PASSWORD "meshpass"
#define MESH_PORT 5555

// --- MODE ---
enum class DeviceMode
{
  GATEWAY,
  NODE
};
DeviceMode g_mode = DeviceMode::GATEWAY;

// --- CONFIG ---
String g_ssid, g_password, g_iothubHost, g_deviceId, g_sasToken;
String g_protocol = "http";
String g_firmwareUrl = "";
uint32_t g_sleepSeconds = 60;

// --- RTC VARIABLES ---
RTC_ATTR uint32_t g_bootCount = 0;
RTC_ATTR bool g_configValid = false;

// --- MESH ---
painlessMesh mesh;

// --- WEB ---
AsyncWebServer server(80);
bool g_apMode = false;
unsigned long g_apStartTime = 0;
unsigned long g_buttonPressTime = 0;

// --- AZURE ---
#ifdef ESP32
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
IOTHUB_CLIENT_LL_HANDLE g_iotHubClient = nullptr;
#else
WiFiClient espClient;
PubSubClient mqttClient(espClient);
#endif
unsigned long lastReconnectAttempt = 0;

// Add this global
bool g_meshInitialized = false;

// --- SENSORS ---
struct Sensor
{
  String name, type;
  int pin = 0;
  int air_value = 4095, water_value = 0, index = 0;
  uint8_t address = 0;
  DHT *dht = nullptr;
  OneWire *oneWire = nullptr;
  DallasTemperature *sensors = nullptr;
  Adafruit_BME280 *bme = nullptr;
  Adafruit_BMP280 *bmp = nullptr;
  ~Sensor()
  {
    delete dht;
    delete bme;
    delete bmp;
  }
};
std::vector<Sensor> g_sensors;
std::map<int, OneWire *> g_onewire_map;
std::map<int, DallasTemperature *> g_dallas_map;

// --- FORWARD DECLARATIONS ---
void forwardToIoTHub(const String &payload);
void meshReceivedCallback(uint32_t from, String &msg);
void clearSensors();
void readConfig();
bool connectSTA();
void startAPMode();
void setupMesh();
#ifdef ESP32
void setupIoTHub();
void checkOTA();
#endif

void setupWebServer();

// --- MESH CALLBACK ---
void meshReceivedCallback(uint32_t from, String &msg)
{
  if (g_mode != DeviceMode::GATEWAY)
    return;
  JsonDocument doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok)
    return;
  doc["rssi"] = WiFi.RSSI();
  String out;
  serializeJson(doc, out);
  forwardToIoTHub(out);
}

// --- CONFIG FUNCTIONS ---
void clearSensors()
{
  g_sensors.clear();
  for (auto &p : g_onewire_map)
    delete p.second;
  for (auto &p : g_dallas_map)
    delete p.second;
  g_onewire_map.clear();
  g_dallas_map.clear();
}

void readConfig()
{
  // clearSensors();
  //  if (!FS_BEGIN())
  //    return;
  //  File f = LittleFS.open("/littlefs/config.json", "r");

  // if (!f)
  // return;
  File f = LittleFS.open("/littlefs/config.json", "r");
  if (!f)
  {
    Serial.println("[CONFIG] No config.json found");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok)
  {
    f.close();
    return;
  }
  f.close();

  String payload;
  serializeJson(doc, payload);
  Serial.println("[BOOT] readConfig: " + payload);

  String mode = doc["mode"] | "gateway";
  g_mode = (mode == "node") ? DeviceMode::NODE : DeviceMode::GATEWAY;
  g_ssid = doc["SSID"] | "";
  g_password = doc["PASSWORD"] | "";
  g_iothubHost = doc["IOTHUB_HOST"] | "";
  g_deviceId = doc["DEVICE_ID"] | "";
  g_sasToken = doc["SAS_TOKEN"] | "";
  g_protocol = doc["PROTOCOL"] | "http";
  g_firmwareUrl = doc["firmwareUrl"] | "";
  g_sleepSeconds = doc["sleepSeconds"] | 60;

  for (JsonObject obj : doc["sensors"].as<JsonArray>())
  {
    Sensor s;
    s.name = obj["name"] | "";
    s.type = obj["type"] | "";
    s.pin = obj["pin"] | 0;
    if (s.type == "cap_soil_moisture")
    {
      s.air_value = obj["air_value"] | 4095;
      s.water_value = obj["water_value"] | 0;
    }
    else if (s.type == "dht22")
    {
      s.dht = new DHT(s.pin, DHT22);
      s.dht->begin();
    }
    else if (s.type == "ds18b20")
    {
      s.index = obj["index"] | 0;
      if (g_dallas_map.find(s.pin) == g_dallas_map.end())
      {
        OneWire *ow = new OneWire(s.pin);
        DallasTemperature *dt = new DallasTemperature(ow);
        dt->begin();
        g_onewire_map[s.pin] = ow;
        g_dallas_map[s.pin] = dt;
      }
      s.oneWire = g_onewire_map[s.pin];
      s.sensors = g_dallas_map[s.pin];
    }
    else if (s.type == "bme280")
    {
      s.address = obj["address"] | 0x76;
      s.bme = new Adafruit_BME280();
      s.bme->begin(s.address);
    }
    else if (s.type == "bmp280")
    {
      s.address = obj["address"] | 0x76;
      s.bmp = new Adafruit_BMP280();
      s.bmp->begin(s.address);
    }
    g_sensors.push_back(s);
  }
  g_configValid = true && !g_ssid.isEmpty() && !g_password.isEmpty() && !g_deviceId.isEmpty();
}

// --- WIFI ---
bool connectSTA()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40)
  {
    Serial.print(".");
    delay(500);
    attempts++;
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[STA] Failed to connect!");
    return false;
  }

  Serial.printf("[STA] Connected — IP: %s\n", WiFi.localIP().toString().c_str());
  return true;
}

void startAPMode()
{
  Serial.println("[AP] Starting AP mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  dnsServer.start(53, "*", WiFi.softAPIP());

  // if (!FS_BEGIN()) {
  //   Serial.println("[AP] LittleFS failed! Formatting...");
  //   LittleFS.format();  // Works on both ESP32 & ESP8266
  //   if (FS_BEGIN()) {
  //     Serial.println("[AP] LittleFS formatted & mounted");
  //   } else {
  //     Serial.println("[AP] LittleFS still failed!");
  //   }
  // }

  setupWebServer();

  g_apMode = true;
  g_apStartTime = millis();
  Serial.printf("[AP] Open: http://%s\n", WiFi.softAPIP().toString().c_str());
}

// --- MESH ---
// In setupMesh()
void setupMesh()
{
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX + g_deviceId, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&meshReceivedCallback);
  g_meshInitialized = true; // Mark as ready
}

// --- IOT HUB ---
#ifdef ESP32
void setupIoTHub()
{
  if (g_protocol == "mqtt")
  {
    mqttClient.setServer(g_iothubHost.c_str(), 8883);
  }
  else if (g_protocol == "sdk")
  {
    if (platform_init() != 0)
      return;
    String conn = "HostName=" + g_iothubHost + ";DeviceId=" + g_deviceId +
                  ";SharedAccessSignature=" + g_sasToken;
    g_iotHubClient = IoTHubClient_LL_CreateFromConnectionString(conn.c_str(), MQTT_Protocol);
    if (g_iotHubClient)
    {
      IoTHubClient_LL_SetRetryPolicy(g_iotHubClient, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF_WITH_JITTER, 0);
    }
  }
}

void checkOTA()
{
  if (g_firmwareUrl.length() < 10)
    return;
  Serial.println("[OTA] Checking for firmware at: " + g_firmwareUrl);
  espClient.setInsecure();
  t_httpUpdate_return ret = httpUpdate.update(espClient, g_firmwareUrl);
  switch (ret)
  {
  case HTTP_UPDATE_FAILED:
    Serial.printf("[OTA] Failed: %d %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
    break;
  case HTTP_UPDATE_NO_UPDATES:
    Serial.println("[OTA] No update available");
    break;
  case HTTP_UPDATE_OK:
    Serial.println("[OTA] Update applied successfully. Rebooting...");
    break;
  }
}
#endif

// --- WEB SERVER ---
void setupWebServer()
{
  // 1. Serve static files (HTML, CSS, …)
  server.serveStatic("/", LittleFS, "/littlefs/").setDefaultFile("index.html");
  // server.serveStatic("/style.css", LittleFS, "/littlefs/style.css");  // Explicit for CSS
  //  2. GET current config (for auto-fill on page load)
  server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (LittleFS.exists("/littlefs/config.json")) {
      request->send(LittleFS, "/littlefs/config.json", "application/json");
    } else {
      request->send(404, "text/plain", "No config yet");
    } });

  // 3. POST new config → write to LittleFS and restart
  server.on("/save_config", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              // This runs when headers are received
              request->send(200); // ACK immediately
            },
            nullptr, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
            {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data).substring(0, len);

    if (index + len == total) {
      Serial.println("[WEB] Saving config:");
      Serial.println(body);

      File f = LittleFS.open("/littlefs/config.json", "w");
      if (f) {
        f.print(body);
        f.close();
        Serial.println("[WEB] Config saved – restarting...");
        request->send(200, "application/json", "{\"status\":\"ok\"}");
        delay(1000);
        ESP.restart();
      } else {
        Serial.println("[WEB] Failed to write config.json");
        request->send(500, "text/plain", "Failed to save config");
      }
    } });

  // --- LIVE DATA ENDPOINT ---
  server.on("/live_data", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();

  for (const auto &s : g_sensors) {
    JsonObject sensor = root.createNestedObject(s.name);

    if (s.type == "cap_soil_moisture") {
      int raw = analogRead(s.pin);
      float pct = 100.0 * (s.air_value - raw) / (float)(s.air_value - s.water_value);
      sensor["moisture"] = constrain(pct, 0, 100);
    }
    else if (s.type == "dht22") {
      sensor["temp"] = s.dht->readTemperature();
      sensor["hum"] = s.dht->readHumidity();
    }
    else if (s.type == "ds18b20") {
      sensor["temp"] = s.sensors->getTempCByIndex(s.index);
    }
    else if (s.type == "bme280") {
      sensor["temp"] = s.bme->readTemperature();
      sensor["hum"] = s.bme->readHumidity();
      sensor["pres"] = s.bme->readPressure() / 100.0F;
    }
    else if (s.type == "bmp280") {
      sensor["temp"] = s.bmp->readTemperature();
      sensor["pres"] = s.bmp->readPressure() / 100.0F;
    }
  }

  String response;
  serializeJson(doc, response);
  request->send(200, "application/json", response); });
  server.begin();
  Serial.println("[WEB] HTTP server started");
}

// --- AZURE SEND ---
void forwardToIoTHub(const String &payload)
{
  if (g_mode == DeviceMode::NODE)
  {
    mesh.sendBroadcast(payload);
    return;
  }
  if (g_protocol == "http")
  {
    HTTP_CLIENT http;
    String url = "https://" + g_iothubHost + "/devices/" + g_deviceId +
                 "/messages/events?api-version=2018-06-30";
    http.begin(espClient, url);
    http.addHeader("Authorization", g_sasToken);
    http.addHeader("Content-Type", "application/json");
    http.POST(payload);
    http.end();
  }
  else if (g_protocol == "mqtt")
  {
    if (!mqttClient.connected() && millis() - lastReconnectAttempt > 5000)
    {
      lastReconnectAttempt = millis();
      String user = g_iothubHost + "/" + g_deviceId + "/?api-version=2018-06-30";
#ifdef ESP32
      espClient.setInsecure();
#endif
      mqttClient.connect(g_deviceId.c_str(), user.c_str(), g_sasToken.c_str());
    }
    if (mqttClient.connected())
    {
      String topic = "devices/" + g_deviceId + "/messages/events/";
      mqttClient.publish(topic.c_str(), payload.c_str());
    }
#ifdef ESP32
  }
  else if (g_protocol == "sdk" && g_iotHubClient)
  {
    IOTHUB_MESSAGE_HANDLE msg = IoTHubMessage_CreateFromString(payload.c_str());
    if (msg)
      IoTHubClient_LL_SendEventAsync(g_iotHubClient, msg, nullptr, nullptr);
#endif
  }
}

// void loadConfigFromSerial() {
//   Serial.println("[CONFIG] Enter JSON config, end with a blank line:");
//   String jsonStr = "";
//   unsigned long start = millis();
//   while (millis() - start < 30000) {  // 30s timeout
//     while (Serial.available()) {
//       char c = Serial.read();
//       if (c == '\n' && jsonStr.endsWith("\n")) break;  // double newline ends input
//       jsonStr += c;
//       start = millis(); // reset timeout on input
//     }
//   }

//   if (jsonStr.length() > 0) {
//     Serial.println("[CONFIG] Writing to SPIFFS...");
//     if (FS_BEGIN()) {
//       File f = FS_OPEN("/config.json", "w");
//       if (f) {
//         f.print(jsonStr);
//         f.close();
//         Serial.println("[CONFIG] Saved successfully, restarting...");
//         delay(500);
//         ESP.restart();
//       } else {
//         Serial.println("[CONFIG] Failed to open config.json for writing");
//       }
//     } else {
//       Serial.println("[CONFIG] Failed to mount filesystem");
//     }
//   } else {
//     Serial.println("[CONFIG] No input received");
//   }
// }

// --- SETUP ---

void setup()
{
  Serial.begin(115200);
  delay(200);
  Serial.println("[BOOT] start");

  pinMode(PIN_BOOT, INPUT_PULLUP);
  g_bootCount++;

  // MOUNT LITTLEFS ONCE HERE
Serial.println("[BOOT] Mounting LittleFS at /littlefs");

if (!LittleFS.begin(true, "/littlefs")) {
    Serial.println("[BOOT] Format needed");
    LittleFS.format();
    if (!LittleFS.begin(true, "/littlefs")) {
        Serial.println("[BOOT] LittleFS FAILED");
        while (1) delay(1000);
    }
    Serial.println("[BOOT] LittleFS formatted & mounted");
} else {
    Serial.println("[BOOT] LittleFS mounted");
}

Serial.println("[DEBUG] LittleFS contents:");
File root = LittleFS.open("/littlefs/");
File file = root.openNextFile();
while (file) {
  Serial.printf("  %s (%u bytes)\n", file.name(), file.size());
  file = root.openNextFile();
}


  readConfig();
  Serial.printf("[BOOT] configValid = %d\n", g_configValid);

  if (!g_configValid)
  {
    startAPMode();
    return;
  }

  if (g_mode == DeviceMode::GATEWAY)
  {
    if (!connectSTA())
    {
      startAPMode();
      return;
    }
    setupMesh();
#ifdef ESP32
    setupIoTHub();
    checkOTA();
#endif
    setupWebServer();
  }
  else
  {
    WiFi.mode(WIFI_STA);
    setupMesh();
  }

  Serial.println("[BOOT] setup complete");
}

// --- LOOP ---
void loop()
{
  if (g_apMode) {
        dnsServer.processNextRequest();
    }
      if (digitalRead(PIN_BOOT) == LOW && g_buttonPressTime == 0)
    g_buttonPressTime = millis();
  if (digitalRead(PIN_BOOT) == LOW && millis() - g_buttonPressTime > 3000)
  {
    LittleFS.remove("/littlefs/config.json");
    delay(500);
    ESP.restart();
  }
  if (digitalRead(PIN_BOOT) == HIGH)
    g_buttonPressTime = 0;
  if (g_apMode && millis() - g_apStartTime > CONFIG_TIMEOUT_MS)
    ESP.restart();
  // mesh.update();
  if (g_meshInitialized)
  {
    mesh.update();
  }
  // === GATEWAY: READ OWN SENSORS EVERY 60 SECONDS ===
  static unsigned long lastSensorRead = 0;
  // Serial.printf("[GATEWAY] g_mode: %s, g_configValid: %d, time_since_last: %lu, send_now: %d\n",
  //               (g_mode == DeviceMode::GATEWAY ? "GATEWAY" : "NODE"),
  //               g_configValid,
  //               (millis() - lastSensorRead),
  //               (g_mode == DeviceMode::GATEWAY && g_configValid && millis() - lastSensorRead > 10000));
  //               static uint32_t lastHeap = 0;
  // if (millis() - lastHeap > 10000) {
  //   lastHeap = millis();
  //   Serial.printf("[MEM] Free heap: %d\n", ESP.getFreeHeap());
  // }
  if (g_mode == DeviceMode::GATEWAY && g_configValid && millis() - lastSensorRead > 10000)
  {
    lastSensorRead = millis();

    JsonDocument doc;
    doc["deviceId"] = g_deviceId;
    doc["firmwareVersion"] = FIRMWARE_VERSION;
    doc["rssi"] = WiFi.RSSI();
    doc["gateway"] = true;

    // Request temperature for all DS18B20
    std::set<int> dsPins;
    for (const auto &s : g_sensors)
      if (s.type == "ds18b20")
        dsPins.insert(s.pin);
    for (int p : dsPins)
      g_dallas_map[p]->requestTemperatures();

    for (const auto &s : g_sensors)
    {
      if (s.type == "cap_soil_moisture")
      {
        int raw = analogRead(s.pin);
        float pct = 100.0 * (s.air_value - raw) / (float)(s.air_value - s.water_value);
        doc[s.name] = constrain(pct, 0, 100);
      }
      else if (s.type == "dht22")
      {
        doc[s.name + "_temp"] = s.dht->readTemperature();
        doc[s.name + "_hum"] = s.dht->readHumidity();
      }
      else if (s.type == "ds18b20")
      {
        doc[s.name] = s.sensors->getTempCByIndex(s.index);
      }
      else if (s.type == "bme280")
      {
        doc[s.name + "_temp"] = s.bme->readTemperature();
        doc[s.name + "_hum"] = s.bme->readHumidity();
        doc[s.name + "_pres"] = s.bme->readPressure() / 100.0F;
      }
      else if (s.type == "bmp280")
      {
        doc[s.name + "_temp"] = s.bmp->readTemperature();
        doc[s.name + "_pres"] = s.bmp->readPressure() / 100.0F;
      }
    }

    String payload;
    serializeJson(doc, payload);
    Serial.println("[GATEWAY] Sending own sensors: " + payload);
    forwardToIoTHub(payload);
  }

  if (g_mode == DeviceMode::NODE && g_bootCount == 1)
  {
    static bool sent = false;
    if (!sent && (mesh.getNodeList().size() > 0 || millis() > 10000))
    {
      JsonDocument doc;
      doc["deviceId"] = g_deviceId;
      doc["firmwareVersion"] = FIRMWARE_VERSION;
      doc["battery"] = analogRead(BATTERY_PIN) * 3.3 / 4095.0;
      doc["rssi"] = WiFi.RSSI();
      doc["meshHopCount"] = 0;
      doc["sleepSeconds"] = g_sleepSeconds;

      for (auto &s : g_sensors)
      {
        if (s.type == "cap_soil_moisture")
        {
          int raw = analogRead(s.pin);
          float pct = (float)(s.air_value - raw) / (s.air_value - s.water_value) * 100.0;
          doc[s.name] = constrain(pct, 0, 100);
        }
        else if (s.type == "dht22")
        {
          doc[s.name + "_temp"] = s.dht->readTemperature();
          doc[s.name + "_hum"] = s.dht->readHumidity();
        }
        else if (s.type == "ds18b20")
        {
          s.sensors->requestTemperatures();
          doc[s.name] = s.sensors->getTempCByIndex(s.index);
        }
        else if (s.type == "bme280")
        {
          doc[s.name + "_temp"] = s.bme->readTemperature();
          doc[s.name + "_hum"] = s.bme->readHumidity();
          doc[s.name + "_pres"] = s.bme->readPressure();
        }
        else if (s.type == "bmp280")
        {
          doc[s.name + "_temp"] = s.bmp->readTemperature();
          doc[s.name + "_pres"] = s.bmp->readPressure();
        }
      }

      String payload;
      serializeJson(doc, payload);
      Serial.println("[payload] " + payload);

      forwardToIoTHub(payload);
      sent = true;
      delay(3000);
      ESP.deepSleep(g_sleepSeconds * 1000000ULL);
    }
  }

#ifdef ESP32
  if (g_iotHubClient)
    IoTHubClient_LL_DoWork(g_iotHubClient);
#endif
}