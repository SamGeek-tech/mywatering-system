#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_BMP280.h>
#include <vector>
#include <map>
#include <set>

// Azure SDK includes
#include "AzureIotHub.h"
#include "Esp32MQTTClient.h"

#define RESET_BUTTON_PIN 0  // GPIO0 (Boot button on most ESP32 boards)
#define AP_SSID "ESP32_Config"
#define AP_PASSWORD "12345678"
#define CONFIG_TIMEOUT 300000  // 5 minutes in AP mode

String g_ssid;
String g_password;
String g_iothubHost;
String g_deviceId;
String g_sasToken;
String g_protocol = "http";

bool g_apMode = false;
unsigned long g_apStartTime = 0;
unsigned long g_buttonPressTime = 0;

struct Sensor {
  String name;
  String type;
  int pin;
  int air_value = 4095;
  int water_value = 0;
  int index = 0;
  uint8_t address = 0;
  DHT* dht = nullptr;
  OneWire* oneWire = nullptr;
  DallasTemperature* sensors = nullptr;
  Adafruit_BME280* bme = nullptr;
  Adafruit_BMP280* bmp = nullptr;

  ~Sensor() {
    delete dht;
    delete bme;
    delete bmp;
  }
};

std::vector<Sensor> g_sensors;
std::map<int, OneWire*> g_onewire_map;
std::map<int, DallasTemperature*> g_dallas_map;

AsyncWebServer server(80);
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
IOTHUB_CLIENT_LL_HANDLE g_iotHubClient = nullptr;
unsigned long lastReconnectAttempt = 0;

// === CALLBACKS ===
static void sendConfirmationCallback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* userContextCallback) {
  if (result == IOTHUB_CLIENT_CONFIRMATION_OK) {
    Serial.println("Message sent successfully");
  } else {
    Serial.print("Send failed: ");
    switch (result) {
      case IOTHUB_CLIENT_CONFIRMATION_BECAUSE_DESTROY: Serial.println("Client destroyed"); break;
      case IOTHUB_CLIENT_CONFIRMATION_MESSAGE_TIMEOUT: Serial.println("Timeout"); break;
      case IOTHUB_CLIENT_CONFIRMATION_ERROR: Serial.println("Error"); break;
      default: Serial.println("Unknown"); break;
    }
  }
}

static void connectionStatusCallback(IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback) {
  if (result == IOTHUB_CLIENT_CONNECTION_AUTHENTICATED) {
    Serial.println("IoT Hub: Connected");
  } else {
    Serial.print("IoT Hub: Disconnected. Reason: ");
    switch (reason) {
      case IOTHUB_CLIENT_CONNECTION_EXPIRED_SAS_TOKEN: Serial.println("SAS expired"); break;
      case IOTHUB_CLIENT_CONNECTION_DEVICE_DISABLED: Serial.println("Device disabled"); break;
      case IOTHUB_CLIENT_CONNECTION_BAD_CREDENTIAL: Serial.println("Bad credential"); break;
      case IOTHUB_CLIENT_CONNECTION_RETRY_EXPIRED: Serial.println("Retry expired"); break;
      case IOTHUB_CLIENT_CONNECTION_NO_NETWORK: Serial.println("No network"); break;
      default: Serial.println("Unknown"); break;
    }
  }
}

// === MQTT RECONNECT ===
bool reconnect() {
  espClient.setInsecure();
  String username = g_iothubHost + "/" + g_deviceId + "/?api-version=2018-06-30";
  if (mqttClient.connect(g_deviceId.c_str(), username.c_str(), g_sasToken.c_str())) {
    Serial.println("MQTT connected");
    return true;
  } else {
    Serial.println("MQTT failed: " + String(mqttClient.state()));
    return false;
  }
}

// === SEND TO IOT HUB ===
void forwardToIoTHub(const String &payload) {
  if (g_protocol == "http") {
    HTTPClient http;
    String url = "https://" + g_iothubHost + "/devices/" + g_deviceId + "/messages/events?api-version=2018-06-30";
    http.begin(url);
    http.addHeader("Authorization", g_sasToken);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(payload);
    Serial.println("HTTP POST: " + String(code));
    http.end();
  } else if (g_protocol == "mqtt" && mqttClient.connected()) {
    String topic = "devices/" + g_deviceId + "/messages/events/";
    mqttClient.publish(topic.c_str(), payload.c_str());
  } else if (g_protocol == "sdk" && g_iotHubClient != nullptr) {
    IOTHUB_MESSAGE_HANDLE msg = IoTHubMessage_CreateFromString(payload.c_str());
    if (msg) {
      IoTHubClient_LL_SendEventAsync(g_iotHubClient, msg, sendConfirmationCallback, nullptr);
      IoTHubMessage_Destroy(msg);
    }
  }
}

// === MEMORY CLEANUP ===
void clearSensors() {
  g_sensors.clear();
  for (auto& p : g_onewire_map) delete p.second;
  for (auto& p : g_dallas_map) delete p.second;
  g_onewire_map.clear();
  g_dallas_map.clear();
}

// === CONFIG ===
void readConfig() {
  clearSensors();
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  File f = SPIFFS.open("/config.json", "r");
  if (!f) {
    Serial.println("No config file");
    return;
  }
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, f) != DeserializationError::Ok) {
    Serial.println("Config parse failed");
    f.close();
    return;
  }
  g_ssid = doc["SSID"].as<String>();
  g_password = doc["PASSWORD"].as<String>();
  g_iothubHost = doc["IOTHUB_HOST"].as<String>();
  g_deviceId = doc["DEVICE_ID"].as<String>();
  g_sasToken = doc["SAS_TOKEN"].as<String>();
  g_protocol = doc["PROTOCOL"].as<String>();

  for (JsonObject obj : doc["sensors"].as<JsonArray>()) {
    Sensor s;
    s.name = obj["name"].as<String>();
    s.type = obj["type"].as<String>();
    s.pin = obj["pin"].as<int>();
    if (s.type == "cap_soil_moisture") {
      s.air_value = obj["air_value"] | 4095;
      s.water_value = obj["water_value"] | 0;
    } else if (s.type == "dht22") {
      s.dht = new DHT(s.pin, DHT22);
      s.dht->begin();
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
  f.close();
}

// === HTML (same as before, omitted for brevity) ===
 const char* configHtml = R"raw(
<!DOCTYPE html>
<html>
<head>
<title>Sensor Config</title>
</head>
<body>
<h1>Configure Sensors</h1>
<form id="configForm">
  <h2>WiFi and IoT Hub</h2>
  SSID: <input type="text" name="SSID"><br>
  Password: <input type="password" name="PASSWORD"><br>
  IoT Hub Host: <input type="text" name="IOTHUB_HOST"><br>
  Device ID: <input type="text" name="DEVICE_ID"><br>
  SAS Token: <input type="text" name="SAS_TOKEN"><br>
  Protocol: <select name="PROTOCOL">
    <option value="http">HTTP (JSON)</option>
    <option value="mqtt">MQTT</option>
    <option value="sdk">Azure IoT SDK</option>
  </select><br>
  <h2>Sensors</h2>
  <div id="sensors"></div>
  <button type="button" onclick="addSensor()">Add Sensor</button><br>
  <button type="button" onclick="submitForm()">Save</button>
</form>
<script>
let sensorCount = 0;
function addSensor() {
  sensorCount++;
  let div = document.createElement('div');
  div.id = 'sensor' + sensorCount;
  div.innerHTML = `
    Name: <input type="text" name="name${sensorCount}"><br>
    Type: <select name="type${sensorCount}" onchange="toggleExtra(this, ${sensorCount})">
      <option value="cap_soil_moisture">Capacitive Soil Moisture</option>
      <option value="dht22">DHT22 (Temp & Humidity)</option>
      <option value="ds18b20">DS18B20 Temperature</option>
      <option value="bme280">BME280 (Temp, Hum, Press)</option>
      <option value="bmp280">BMP280 (Temp, Press)</option>
    </select><br>
    Pin: <input type="number" name="pin${sensorCount}"><br>
    <div id="extra${sensorCount}"></div>
  `;
  document.getElementById('sensors').appendChild(div);
  toggleExtra(div.querySelector('select'), sensorCount);
}
function toggleExtra(select, count) {
  let extra = document.getElementById('extra' + count);
  extra.innerHTML = '';
  if (select.value === 'cap_soil_moisture') {
    extra.innerHTML = `
      Air Value (dry): <input type="number" name="air${count}" value="4095"><br>
      Water Value (wet): <input type="number" name="water${count}" value="0"><br>
    `;
  } else if (select.value === 'ds18b20') {
    extra.innerHTML = `
      Device Index: <input type="number" name="index${count}" value="0"><br>
    `;
  } else if (select.value === 'bme280' || select.value === 'bmp280') {
    extra.innerHTML = `
      Address: <select name="address${count}">
        <option value="118">0x76</option>
        <option value="119">0x77</option>
      </select><br>
    `;
  }
}
function submitForm() {
  let form = document.getElementById('configForm');
  let data = {
    SSID: form.SSID.value,
    PASSWORD: form.PASSWORD.value,
    IOTHUB_HOST: form.IOTHUB_HOST.value,
    DEVICE_ID: form.DEVICE_ID.value,
    SAS_TOKEN: form.SAS_TOKEN.value,
    PROTOCOL: form.PROTOCOL.value,
    sensors: []
  };
  for (let i = 1; i <= sensorCount; i++) {
    let s = {
      name: form['name' + i].value,
      type: form['type' + i].value,
      pin: parseInt(form['pin' + i].value)
    };
    if (s.type === 'cap_soil_moisture') {
      s.air_value = parseInt(form['air' + i].value);
      s.water_value = parseInt(form['water' + i].value);
    } else if (s.type === 'ds18b20') {
      s.index = parseInt(form['index' + i].value || 0);
    } else if (s.type === 'bme280' || s.type === 'bmp280') {
      s.address = parseInt(form['address' + i].value);
    }
    data.sensors.push(s);
  }
  fetch('/save_config', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data)
  }).then(res => {
    if (res.ok) alert('Saved. Restart device to apply changes.');
  });
}
window.onload = function() {
  fetch('/get_config')
    .then(res => { if (res.ok) return res.json(); else throw new Error(); })
    .then(data => {
      if (!data) return;
      form.SSID.value = data.SSID || '';
      form.PASSWORD.value = data.PASSWORD || '';
      form.IOTHUB_HOST.value = data.IOTHUB_HOST || '';
      form.DEVICE_ID.value = data.DEVICE_ID || '';
      form.SAS_TOKEN.value = data.SAS_TOKEN || '';
      form.PROTOCOL.value = data.PROTOCOL || 'http';
      if (data.sensors) {
        data.sensors.forEach(s => {
          addSensor();
          let i = sensorCount;
          form['name' + i].value = s.name;
          form['type' + i].value = s.type;
          form['pin' + i].value = s.pin;
          toggleExtra(form['type' + i], i);
          if (s.type === 'cap_soil_moisture') {
            form['air' + i].value = s.air_value;
            form['water' + i].value = s.water_value;
          } else if (s.type === 'ds18b20') {
            form['index' + i].value = s.index;
          } else if (s.type === 'bme280' || s.type === 'bmp280') {
            form['address' + i].value = s.address;
          }
        });
      }
    }).catch(() => {});
};
</script>
</body>
</html>
)raw";

// === WEB SERVER (AP MODE) ===
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *req) {
    req->send(200, "text/html", configHtml);
  });

  server.on("/get_config", HTTP_GET, [](AsyncWebServerRequest *req) {
    if (SPIFFS.exists("/config.json")) {
      req->send(SPIFFS, "/config.json", "application/json");
    } else {
      req->send(404);
    }
  });

  server.on("/save_config", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200);
  }, NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data).substring(0, len);
    if (index + len == total) {
      File f = SPIFFS.open("/config.json", "w");
      if (f) {
        f.print(body);
        f.close();
        Serial.println("Config saved. Restarting...");
        delay(1000);
        ESP.restart();
      }
    }
  });

  server.on("/telemetry", HTTP_POST, [](AsyncWebServerRequest *req) {
    req->send(200);
  }, NULL, [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
    static String body;
    if (index == 0) body = "";
    body += String((char*)data).substring(0, len);
    if (index + len == total) {
      forwardToIoTHub(body);
    }
  });

  server.begin();
}
// === START AP MODE ===
void startAPMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println("AP Mode: " + String(AP_SSID));
  Serial.println("IP: " + WiFi.softAPIP().toString());
  g_apMode = true;
  g_apStartTime = millis();
  setupWebServer();
}

// === CONNECT TO STA ===
bool connectSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_password.c_str());
  Serial.print("Connecting to WiFi");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected: " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println("\nWiFi failed");
    return false;
  }
}

// === SETUP ===
void setup() {
  Serial.begin(115200);
  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS failed");
  }

  readConfig();

  // Try STA first
  if (g_ssid.length() > 0 && connectSTA()) {
    g_apMode = false;
  } else {
    startAPMode();
  }

  if (!g_apMode) {
    if (g_protocol == "mqtt") {
      mqttClient.setServer(g_iothubHost.c_str(), 8883);
    } else if (g_protocol == "sdk") {
      if (platform_init() == 0) {
        String connStr = "HostName=" + g_iothubHost + ";DeviceId=" + g_deviceId + ";SharedAccessSignature=" + g_sasToken;
        g_iotHubClient = IoTHubClient_LL_CreateFromConnectionString(connStr.c_str(), MQTT_Protocol);
        if (g_iotHubClient) {
          IoTHubClient_LL_SetRetryPolicy(g_iotHubClient, IOTHUB_CLIENT_RETRY_EXPONENTIAL_BACKOFF_WITH_JITTER, 0);
          IoTHubClient_LL_SetConnectionStatusCallback(g_iotHubClient, connectionStatusCallback, nullptr);
        }
      }
    }
    setupWebServer();  // Still serve config page in STA mode
  }
}

// === LOOP ===
void loop() {
  // === Reset button: hold 3s to enter AP mode ===
  if (digitalRead(RESET_BUTTON_PIN) == LOW) {
    if (g_buttonPressTime == 0) g_buttonPressTime = millis();
    if (millis() - g_buttonPressTime > 3000) {
      Serial.println("Reset button held → entering AP mode");
      SPIFFS.remove("/config.json");
      delay(500);
      ESP.restart();
    }
  } else {
    g_buttonPressTime = 0;
  }

  // === AP Mode timeout ===
  if (g_apMode && (millis() - g_apStartTime > CONFIG_TIMEOUT)) {
    Serial.println("AP timeout → restart");
    ESP.restart();
  }

  // === Telemetry loop ===
  static unsigned long lastSend = 0;
  if (!g_apMode && millis() - lastSend > 30000) {
    if (!g_sensors.empty()) {
      std::set<int> ds_pins;
      for (const auto& s : g_sensors) {
        if (s.type == "ds18b20") ds_pins.insert(s.pin);
      }
      for (int p : ds_pins) g_dallas_map[p]->requestTemperatures();

      DynamicJsonDocument doc(1024);
      for (const auto& s : g_sensors) {
        if (s.type == "cap_soil_moisture") {
          int raw = analogRead(s.pin);
          float m = 100.0 * (s.air_value - raw) / (s.air_value - s.water_value);
          doc[s.name] = m;
        } else if (s.type == "dht22" && s.dht) {
          float t = s.dht->readTemperature();
          float h = s.dht->readHumidity();
          if (!isnan(t)) doc[s.name + "_temp"] = t;
          if (!isnan(h)) doc[s.name + "_hum"] = h;
        } else if (s.type == "ds18b20" && s.sensors) {
          float t = s.sensors->getTempCByIndex(s.index);
          if (t != DEVICE_DISCONNECTED_C) doc[s.name] = t;
        } else if (s.type == "bme280" && s.bme) {
          doc[s.name + "_temp"] = s.bme->readTemperature();
          doc[s.name + "_hum"] = s.bme->readHumidity();
          doc[s.name + "_press"] = s.bme->readPressure() / 100.0F;
        } else if (s.type == "bmp280" && s.bmp) {
          doc[s.name + "_temp"] = s.bmp->readTemperature();
          doc[s.name + "_press"] = s.bmp->readPressure() / 100.0F;
        }
      }
      String payload;
      serializeJson(doc, payload);
      forwardToIoTHub(payload);
    }
    lastSend = millis();
  }

  // === MQTT & SDK loop ===
  if (g_protocol == "mqtt" && !g_apMode) {
    if (!mqttClient.connected()) {
      if (millis() - lastReconnectAttempt > 5000) {
        lastReconnectAttempt = millis();
        reconnect();
      }
    } else {
      mqttClient.loop();
    }
  } else if (g_protocol == "sdk" && g_iotHubClient) {
    IoTHubClient_LL_DoWork(g_iotHubClient);
    ThreadAPI_Sleep(100);
  }

  delay(10);
}