/*
 * ═══════════════════════════════════════════════════════════════
 *  Open IoT – ESP32/ESP8266 Firmware Example
 *  
 *  This sketch:
 *  1. Creates a WiFi captive portal for initial WiFi setup
 *  2. Connects to the Open IoT server for device adoption
 *  3. Connects to MQTT and publishes sensor data
 *  4. Subscribes to commands from the dashboard
 * ═══════════════════════════════════════════════════════════════
 */

#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <PubSubClient.h>       // https://github.com/knolleary/pubsubclient
#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson
#include <HTTPClient.h>

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

// ── Configuration (set these from QR code or manually) ────────
// These will be populated from the QR scan / captive portal
String OPENIOT_SERVER = "http://YOUR_SERVER:8000";
String DEVICE_ID      = "YOUR_DEVICE_ID";
String ADOPT_TOKEN    = "YOUR_ADOPTION_TOKEN";
String MQTT_HOST      = "YOUR_MQTT_HOST";
int    MQTT_PORT      = 1883;
String MQTT_USER      = "";
String MQTT_PASS      = "";

// ── Globals ───────────────────────────────────────────────────
WiFiClient espClient;
PubSubClient mqttClient(espClient);
unsigned long lastPublish = 0;
const int PUBLISH_INTERVAL = 10000; // 10 seconds
bool isAdopted = false;

// ── WiFi Setup ────────────────────────────────────────────────
void setupWiFi() {
  WiFiManager wm;
  
  // Custom parameters for Open IoT config
  WiFiManagerParameter param_server("server", "Open IoT Server URL", "", 100);
  WiFiManagerParameter param_device("device_id", "Device ID", "", 40);
  WiFiManagerParameter param_token("token", "Adoption Token", "", 60);
  WiFiManagerParameter param_mqtt("mqtt_host", "MQTT Host", "", 60);
  WiFiManagerParameter param_mqtt_user("mqtt_user", "MQTT Username", "", 40);
  WiFiManagerParameter param_mqtt_pass("mqtt_pass", "MQTT Password", "", 40);

  wm.addParameter(&param_server);
  wm.addParameter(&param_device);
  wm.addParameter(&param_token);
  wm.addParameter(&param_mqtt);
  wm.addParameter(&param_mqtt_user);
  wm.addParameter(&param_mqtt_pass);

  // Start captive portal
  // If it fails to connect, it will create an AP named "OpenIoT-Setup"
  if (!wm.autoConnect("OpenIoT-Setup", "openiot123")) {
    Serial.println("Failed to connect! Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.println("✅ WiFi connected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Read custom parameters
  OPENIOT_SERVER = String(param_server.getValue());
  DEVICE_ID = String(param_device.getValue());
  ADOPT_TOKEN = String(param_token.getValue());
  MQTT_HOST = String(param_mqtt.getValue());
  MQTT_USER = String(param_mqtt_user.getValue());
  MQTT_PASS = String(param_mqtt_pass.getValue());
}

// ── Device Adoption ───────────────────────────────────────────
bool adoptDevice() {
  if (OPENIOT_SERVER.length() == 0 || DEVICE_ID.length() == 0) {
    Serial.println("⚠️ Server/Device not configured");
    return false;
  }

  HTTPClient http;
  String url = OPENIOT_SERVER + "/api/devices/adopt";
  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  // Build adoption payload
  JsonDocument doc;
  doc["token"] = ADOPT_TOKEN;
  doc["device_id"] = DEVICE_ID;
  doc["firmware_version"] = "1.0.0";
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
  #ifdef ESP32
    doc["chip_model"] = "ESP32";
  #else
    doc["chip_model"] = "ESP8266";
  #endif

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  if (httpCode == 200) {
    String response = http.getString();
    JsonDocument resDoc;
    deserializeJson(resDoc, response);

    MQTT_USER = resDoc["mqtt_username"].as<String>();
    MQTT_PASS = resDoc["mqtt_password"].as<String>();

    Serial.println("✅ Device adopted successfully!");
    Serial.println("MQTT User: " + MQTT_USER);
    http.end();
    return true;
  } else {
    Serial.printf("❌ Adoption failed: HTTP %d\n", httpCode);
    Serial.println(http.getString());
    http.end();
    return false;
  }
}

// ── MQTT Callback ─────────────────────────────────────────────
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("📨 Command received: ");
  Serial.println(message);

  // Parse command
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);
  if (error) return;

  String command = doc["command"].as<String>();

  if (command == "ping") {
    Serial.println("🏓 Pong!");
    // Respond with immediate state update
    publishState();
  }
  else if (command == "restart") {
    Serial.println("🔄 Restarting...");
    delay(1000);
    ESP.restart();
  }
  else if (command == "led_on") {
    digitalWrite(LED_BUILTIN, LOW);  // Most ESPs: LOW = ON
    Serial.println("💡 LED ON");
  }
  else if (command == "led_off") {
    digitalWrite(LED_BUILTIN, HIGH);
    Serial.println("💡 LED OFF");
  }
}

// ── MQTT Connect ──────────────────────────────────────────────
void connectMQTT() {
  mqttClient.setServer(MQTT_HOST.c_str(), MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  while (!mqttClient.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientId = "openiot-" + DEVICE_ID;

    if (mqttClient.connect(clientId.c_str(), MQTT_USER.c_str(), MQTT_PASS.c_str())) {
      Serial.println(" ✅ connected!");

      // Subscribe to commands
      String cmdTopic = "openiot/" + DEVICE_ID + "/command";
      mqttClient.subscribe(cmdTopic.c_str());

      // Publish availability
      String availTopic = "openiot/" + DEVICE_ID + "/availability";
      mqttClient.publish(availTopic.c_str(), "online", true);

      Serial.println("📡 Subscribed to: " + cmdTopic);
    } else {
      Serial.printf(" ❌ failed (rc=%d), retrying in 5s\n", mqttClient.state());
      delay(5000);
    }
  }
}

// ── Publish Sensor State ──────────────────────────────────────
void publishState() {
  if (!mqttClient.connected()) return;

  JsonDocument doc;

  // ── Read your sensors here ──
  // Example: DHT22, BMP280, light sensor, etc.
  // For demo, we use simulated values:
  doc["temperature"] = 20.0 + random(0, 100) / 10.0;  // 20-30°C
  doc["humidity"] = 40.0 + random(0, 400) / 10.0;      // 40-80%
  doc["light"] = random(100, 1000);                      // 100-1000 lux
  doc["uptime"] = millis() / 1000;

  // If you have a real sensor:
  // doc["temperature"] = dht.readTemperature();
  // doc["humidity"] = dht.readHumidity();

  String payload;
  serializeJson(doc, payload);

  String topic = "openiot/" + DEVICE_ID + "/state";
  mqttClient.publish(topic.c_str(), payload.c_str());

  Serial.println("📤 Published: " + payload);
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("\n\n");
  Serial.println("════════════════════════════════");
  Serial.println("  Open IoT ESP Firmware v1.0.0");
  Serial.println("════════════════════════════════");
  Serial.println();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);  // LED off

  // 1. Connect to WiFi (captive portal if first time)
  setupWiFi();

  // 2. Adopt device with the server
  isAdopted = adoptDevice();

  // 3. Connect to MQTT
  if (MQTT_HOST.length() > 0) {
    connectMQTT();
  }
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  // Maintain MQTT connection
  if (MQTT_HOST.length() > 0) {
    if (!mqttClient.connected()) {
      connectMQTT();
    }
    mqttClient.loop();
  }

  // Publish sensor data periodically
  if (millis() - lastPublish >= PUBLISH_INTERVAL) {
    publishState();
    lastPublish = millis();
  }
}
