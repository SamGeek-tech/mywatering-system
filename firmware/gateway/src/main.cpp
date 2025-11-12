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
#define FS_TYPE LittleFS
#define FS_OPEN(path, mode) LittleFS.open(path, mode)
#define FS_EXISTS(path) LittleFS.exists(path)
#define FS_REMOVE(path) LittleFS.remove(path)

#if defined(ESP32)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include "AzureIotHub.h"
#include "Esp32MQTTClient.h"
#define FS_BEGIN() LittleFS.begin(true, "/littlefs")   // MOUNT AT ROOT
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
  #define FS_BEGIN() LittleFS.begin()
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
enum class DeviceMode { GATEWAY, NODE };
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
struct Sensor {
  String name, type;
  int pin = 0;
  int air_value = 4095, water_value = 0, index = 0;
  uint8_t address = 0;
  DHT* dht = nullptr;
  OneWire* oneWire = nullptr;
  DallasTemperature* sensors = nullptr;
  Adafruit_BME280* bme = nullptr;
  Adafruit_BMP280* bmp = nullptr;
  ~Sensor() { delete dht; delete bme; delete bmp; }
};
std::vector<Sensor> g_sensors;
std::map<int, OneWire*> g_onewire_map;
std::map<int, DallasTemperature*> g_dallas_map;

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
void meshReceivedCallback(uint32_t from, String &msg) {
  if (g_mode != DeviceMode::GATEWAY) return;
  JsonDocument doc;
  if (deserializeJson(doc, msg) != DeserializationError::Ok) return;
  doc["rssi"] = WiFi.RSSI();
  String out; serializeJson(doc, out);
  forwardToIoTHub(out);
}

// --- CONFIG FUNCTIONS ---
void clearSensors() {
  g_sensors.clear();
  for (auto &p : g_onewire_map) delete p.second;
  for (auto &p : g_dallas_map) delete p.second;
  g_onewire_map.clear(); g_dallas_map.clear();
}

void readConfig() {
  clearSensors();
  if (!FS_BEGIN()) return;
  File f = FS_OPEN("/config.json", "r");
  if (!f) return;

  JsonDocument doc;
  if (deserializeJson(doc, f) != DeserializationError::Ok) { f.close(); return; }
  f.close();


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

  for (JsonObject obj : doc["sensors"].as<JsonArray>()) {
    Sensor s;
    s.name = obj["name"] | "";
    s.type = obj["type"] | "";
    s.pin = obj["pin"] | 0;
    if (s.type == "cap_soil_moisture") {
      s.air_value = obj["air_value"] | 4095;
      s.water_value = obj["water_value"] | 0;
    } else if (s.type == "dht22") {
      s.dht = new DHT(s.pin, DHT22); s.dht->begin();
    } else if (s.type == "ds18b20") {
      s.index = obj["index"] | 0;
      if (g_dallas_map.find(s.pin) == g_dallas_map.end()) {
        OneWire* ow = new OneWire(s.pin);
        DallasTemperature* dt = new DallasTemperature(ow);
        dt->begin();
        g_onewire_map[s.pin] = ow;
        g_dallas_map[s.pin] = dt;
      }
      s.oneWire = g_onewire_map[s.pin];
      s.sensors = g_dallas_map[s.pin];
    } else if (s.type == "bme280") {
      s.address = obj["address"] | 0x76;
      s.bme = new Adafruit_BME280();
      s.bme->begin(s.address);
    } else if (s.type == "bmp280") {
      s.address = obj["address"] | 0x76;
      s.bmp = new Adafruit_BMP280();
      s.bmp->begin(s.address);
    }
    g_sensors.push_back(s);
  }
  g_configValid = true && !g_ssid.isEmpty() && !g_password.isEmpty() && !g_deviceId.isEmpty();
}

// --- WIFI ---
bool connectSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
  uint8_t attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500); attempts++;
  }
  return WiFi.status() == WL_CONNECTED;
}

void startAPMode() {
  Serial.println("[AP] Starting AP mode...");
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
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
void setupMesh() {
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION);
  mesh.init(MESH_PREFIX + g_deviceId, MESH_PASSWORD, MESH_PORT);
  mesh.onReceive(&meshReceivedCallback);
  g_meshInitialized = true;  // Mark as ready
}

// --- IOT HUB ---
#ifdef ESP32
void setupIoTHub() {
  if (g_protocol == "mqtt") {
    mqttClient.setServer(g_iothubHost.c_str(), 8883);
  } else if (g_protocol == "sdk") {
    if (platform_init() != 0) return;
    String conn = "HostName=" + g_iothubHost + ";DeviceId=" + g_deviceId +
                  ";SharedAccessSignature=" + g_sasToken;
    g_iotHubClient = IoTHubClient_LL_CreateFromConnectionString(conn.c_str(), MQTT_Protocol);
    if (g_iotHubClient) {
      IoTHubClient_LL_SetRetryPolicy(g_iotHubClient, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF_WITH_JITTER, 0);
    }
  }
}

void checkOTA() {
  if (g_firmwareUrl.length() < 10) return;
  Serial.println("[OTA] Checking for firmware at: " + g_firmwareUrl);
  espClient.setInsecure();
  t_httpUpdate_return ret = httpUpdate.update(espClient, g_firmwareUrl);
  switch (ret) {
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
void setupWebServer() {
  String html = R"raw(
<!DOCTYPE html><html><head><title>ESP Config</title></head><body>
<h2>Mode</h2><select id="mode"><option value="gateway">Gateway</option><option value="node">Node</option></select><br>
<h2>Wi-Fi</h2>SSID:<input id="SSID"><br>Password:<input type="password" id="PASSWORD"><br>
<h2>Azure</h2>Host:<input id="IOTHUB_HOST"><br>Device ID:<input id="DEVICE_ID"><br>
SAS Token:<input type="password" id="SAS_TOKEN"><br>
Protocol:<select id="PROTOCOL"><option value="http">HTTP</option><option value="mqtt">MQTT</option>
)raw";

#ifdef ESP32
  html += "<option value=\"sdk\">SDK</option>";
#endif

  html += R"raw(
</select><br>
OTA URL:<input id="firmwareUrl"><br>Sleep (s):<input type="number" id="sleepSeconds" value="60" min="10"><br>
<h2>Sensors</h2><div id="sensors"></div><button onclick="add()">+ Sensor</button><br><br>
<button onclick="save()">Save & Restart</button>
<script>
let cnt=0;
function add(){cnt++;let d=document.createElement('div');d.innerHTML=
`<hr>Name:<input name="name${cnt}"><br>Type:<select name="type${cnt}" onchange="extra(${cnt},this.value)">
<option value="cap_soil_moisture">Soil</option><option value="dht22">DHT22</option>
<option value="ds18b20">DS18B20</option><option value="bme280">BME280</option>
<option value="bmp280">BMP280</option></select><br>Pin:<input type="number" name="pin${cnt}"><br>
<div id="extra${cnt}"></div>`;document.getElementById('sensors').appendChild(d);}
function extra(i,t){let e=document.getElementById('extra'+i);e.innerHTML='';
if(t==='cap_soil_moisture')e.innerHTML='Air:<input name="air'+i+'" value="4095"> Water:<input name="water'+i+'" value="0">';
if(t==='ds18b20')e.innerHTML='Index:<input name="index'+i+'" value="0">';
if(t==='bme280'||t==='bmp280')e.innerHTML='Addr:<select name="addr'+i+'"><option value="118">0x76</option><option value="119">0x77</option></select>';}
function save(){
  let o={mode:document.getElementById('mode').value, SSID:document.getElementById('SSID').value,
         PASSWORD:document.getElementById('PASSWORD').value, IOTHUB_HOST:document.getElementById('IOTHUB_HOST').value,
         DEVICE_ID:document.getElementById('DEVICE_ID').value, SAS_TOKEN:document.getElementById('SAS_TOKEN').value,
         PROTOCOL:document.getElementById('PROTOCOL').value, firmwareUrl:document.getElementById('firmwareUrl').value,
         sleepSeconds:parseInt(document.getElementById('sleepSeconds').value), sensors:[]};
  for(let i=1;i<=cnt;i++){
    let s={name:document.querySelector(`[name=name${i}]`).value, type:document.querySelector(`[name=type${i}]`).value,
           pin:parseInt(document.querySelector(`[name=pin${i}]`).value)};
    if(s.type==='cap_soil_moisture'){s.air_value=parseInt(document.querySelector(`[name=air${i}]`).value);
      s.water_value=parseInt(document.querySelector(`[name=water${i}]`).value);}
    if(s.type==='ds18b20')s.index=parseInt(document.querySelector(`[name=index${i}]`).value||0);
    if(s.type==='bme280'||s.type==='bmp280')s.address=parseInt(document.querySelector(`[name=addr${i}]`).value);
    o.sensors.push(s);}
  fetch('/save_config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)})
   .then(()=>{alert('Saved – restarting');});
}
</script></body></html>
)raw";

  // CAPTURE 'html' in the lambda
  server.on("/", HTTP_GET, [html](AsyncWebServerRequest *request) {
    request->send(200, "text/html", html.c_str());
  });

  server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (FS_EXISTS("/config.json")) {
      request->send(FS_TYPE, "/config.json", "application/json");
    } else {
      request->send(404);
    }
  });

  server.on("/save_config", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200);
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    Serial.println("[CONFIG] save config");
    
    static String body;
    if (index == 0) body = "";
    body += String((char*)data).substring(0, len);
    Serial.println(body);

    if (index + len == total) {
      File f = FS_OPEN("/config.json", "w");
      if (f) { f.print(body); f.close(); }
      delay(800);
    Serial.println("[CONFIG] saveed");

      ESP.restart();
    }
  });

  server.begin();
}


// --- AZURE SEND ---
void forwardToIoTHub(const String &payload) {
  if (g_mode == DeviceMode::NODE) {
    mesh.sendBroadcast(payload);
    return;
  }
  if (g_protocol == "http") {
    HTTP_CLIENT http;
    String url = "https://" + g_iothubHost + "/devices/" + g_deviceId +
                 "/messages/events?api-version=2018-06-30";
    http.begin(espClient,url);
    http.addHeader("Authorization", g_sasToken);
    http.addHeader("Content-Type", "application/json");
    http.POST(payload);
    http.end();
  }
  else if (g_protocol == "mqtt") {
    if (!mqttClient.connected() && millis() - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = millis();
      String user = g_iothubHost + "/" + g_deviceId + "/?api-version=2018-06-30";
#ifdef ESP32
      espClient.setInsecure();
#endif
      mqttClient.connect(g_deviceId.c_str(), user.c_str(), g_sasToken.c_str());
    }
    if (mqttClient.connected()) {
      String topic = "devices/" + g_deviceId + "/messages/events/";
      mqttClient.publish(topic.c_str(), payload.c_str());
    }
#ifdef ESP32
  } else if (g_protocol == "sdk" && g_iotHubClient) {
    IOTHUB_MESSAGE_HANDLE msg = IoTHubMessage_CreateFromString(payload.c_str());
    if (msg) IoTHubClient_LL_SendEventAsync(g_iotHubClient, msg, nullptr, nullptr);
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

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("[BOOT] start");

  pinMode(PIN_BOOT, INPUT_PULLUP);
  g_bootCount++;

  // MOUNT LITTLEFS ONCE HERE
  Serial.println("[BOOT] Mounting LittleFS at /littlefs");
  if (!FS_BEGIN()) {
    Serial.println("[BOOT] Format needed");
    LittleFS.format();
    if (!FS_BEGIN()) {
      Serial.println("[BOOT] LittleFS FAILED");
      while (1) delay(1000);
    }
    Serial.println("[BOOT] LittleFS formatted & mounted");
  } else {
    Serial.println("[BOOT] LittleFS mounted");
  }

  readConfig();
  Serial.printf("[BOOT] configValid = %d\n", g_configValid);

  if (!g_configValid) {
    startAPMode();
    return;
  }

  if (g_mode == DeviceMode::GATEWAY) {
    if (!connectSTA()) {
      startAPMode();
      return;
    }
    setupMesh();
#ifdef ESP32
    setupIoTHub();
    checkOTA();
#endif
    setupWebServer();
  } else {
    WiFi.mode(WIFI_STA);
    setupMesh();
  }

  Serial.println("[BOOT] setup complete");
}

// --- LOOP ---
void loop() {
 dnsServer.processNextRequest();  // Add this
  if (digitalRead(PIN_BOOT) == LOW && g_buttonPressTime == 0) g_buttonPressTime = millis();
  if (digitalRead(PIN_BOOT) == LOW && millis() - g_buttonPressTime > 3000) {
    FS_REMOVE("/config.json"); delay(500); ESP.restart();
  }
  if (digitalRead(PIN_BOOT) == HIGH) g_buttonPressTime = 0;
  if (g_apMode && millis() - g_apStartTime > CONFIG_TIMEOUT_MS) ESP.restart();
  //mesh.update();
if (g_meshInitialized) {
  mesh.update();
}
   /*if (g_mode == DeviceMode::NODE && g_bootCount == 1) {
    static bool sent = false;
    if (!sent && (mesh.getNodeList().size() > 0 || millis() > 10000)) {
      JsonDocument doc;
      doc["deviceId"] = g_deviceId;
      doc["firmwareVersion"] = FIRMWARE_VERSION;
      doc["battery"] = analogRead(BATTERY_PIN) * 3.3 / 4095.0;
      doc["rssi"] = WiFi.RSSI();
      doc["meshHopCount"] = 0;
      doc["sleepSeconds"] = g_sleepSeconds;

      for (auto &s : g_sensors) {
        if (s.type == "cap_soil_moisture") {
          int raw = analogRead(s.pin);
          float pct = (float)(s.air_value - raw) / (s.air_value - s.water_value) * 100.0;
          doc[s.name] = constrain(pct, 0, 100);
        } else if (s.type == "dht22") {
          doc[s.name + "_temp"] = s.dht->readTemperature();
          doc[s.name + "_hum"] = s.dht->readHumidity();
        } else if (s.type == "ds18b20") {
          s.sensors->requestTemperatures();
          doc[s.name] = s.sensors->getTempCByIndex(s.index);
        } else if (s.type == "bme280") {
          doc[s.name + "_temp"] = s.bme->readTemperature();
          doc[s.name + "_hum"] = s.bme->readHumidity();
          doc[s.name + "_pres"] = s.bme->readPressure();
        } else if (s.type == "bmp280") {
          doc[s.name + "_temp"] = s.bmp->readTemperature();
          doc[s.name + "_pres"] = s.bmp->readPressure();
        }
      }

      String payload; serializeJson(doc, payload);
      forwardToIoTHub(payload);
      sent = true;
      delay(3000);
      ESP.deepSleep(g_sleepSeconds * 1000000ULL);
    }
  }

  #ifdef ESP32
  if (g_iotHubClient) IoTHubClient_LL_DoWork(g_iotHubClient);
  #endif*/
}