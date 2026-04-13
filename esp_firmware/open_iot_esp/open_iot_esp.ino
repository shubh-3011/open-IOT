/*
 * ══════════════════════════════════════════════════════════════
 *  Open IoT – ESP32 / ESP8266 Firmware  v2.1.0  (Fixed)
 *
 *  Fixes vs v2.0.0:
 *  ✅ Removed duplicate handleQRStatus() & handleQRGenerate() definitions
 *     (originals caused compile error)
 *  ✅ Fixed /setup/qr route (was pointing to wrong function)
 *  ✅ Added EEPROM persistence for ALL device config (server, device_id,
 *     token, MQTT creds) — previously lost  reboot
 *  ✅ Added isAdopted flag saved to EEPROM (skips re-adoption on reboot)
 *  ✅ Added WiFi SSID + password fields to captive portal form
 *  ✅ Fixed handleSaveManual: now connects WiFi, saves full config, restarts
 *  ✅ Fixed connectMQTT: no longer blocks forever (max 5 retries)
 *  ✅ Fixed loop: only publish state when adopted + MQTT connected
 *  ✅ Added deferred restart (lets HTTP response reach browser first)
 * ══════════════════════════════════════════════════════════════
 */

#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson  (v6)
#include <EEPROM.h>
#include <PubSubClient.h> // https://github.com/knolleary/pubsubclient

#ifdef ESP32
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <WiFi.h>
#define EEPROM_SIZE 1024
#define WIFI_OPEN WIFI_AUTH_OPEN
#else
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#define EEPROM_SIZE 512
#define WIFI_OPEN ENC_TYPE_NONE
// Alias so the rest of the code compiles unchanged
using WebServer = ESP8266WebServer;
#endif

// ── Runtime config (loaded from EEPROM or captive portal) ──────
String OPENIOT_SERVER = "";
String DEVICE_ID = "";
String ADOPT_TOKEN = "";
String MQTT_HOST = "";
int MQTT_PORT = 1883;
String MQTT_USER = "";
String MQTT_PASS = "";

// ── Globals ────────────────────────────────────────────────────
WiFiClient espClient;
PubSubClient mqttClient(espClient);
WebServer server(80);
DNSServer dnsServer;               // <-- captive portal DNS redirect
bool apMode = false;               // true when running as AP hotspot

unsigned long lastPublish = 0;
const int PUBLISH_INTERVAL = 10000; // ms between sensor publishes
bool isAdopted = false;
bool pendingRestart = false;
unsigned long restartAt = 0;

// ── Factory Reset ──────────────────────────────────────────────
// GPIO0 = FLASH/BOOT button on ESP8266 (D3) and ESP32 boards.
// Hold it for 3 seconds at any time to wipe EEPROM and reboot
// into captive-portal mode.
#ifdef ESP32
  #define RESET_PIN 0
  #define RELAY_PIN 23 
#else
  #define RESET_PIN 0  // GPIO0 = FLASH button on NodeMCU/Wemos
  #define RELAY_PIN 5  // D1 on NodeMCU/Wemos
#endif

bool relayState = false;


// ── EEPROM Layout ──────────────────────────────────────────────
//  Offset  Size  Content
//  0       1     WiFi SSID length
//  1       32    WiFi SSID  (null-padded)
//  33      64    WiFi Password (null-padded)
//  97      1     Device config marker  (0xAB = valid data present)
//  98      1     isAdopted flag
//  99      ~     Packed strings: [len][bytes] for each of:
//                OPENIOT_SERVER, DEVICE_ID, ADOPT_TOKEN, MQTT_HOST,
//                [port_hi][port_lo], MQTT_USER, MQTT_PASS

#define EEPROM_WIFI_SSID_LEN 0
#define EEPROM_WIFI_SSID 1
#define EEPROM_WIFI_PASS 33
#define EEPROM_CFG_MARKER 97
#define EEPROM_CFG_ADOPTED 98
#define EEPROM_CFG_START 99
#define CFG_MARKER_VALUE 0xAB
#define MAX_STR 100

// ── Firmware format version ───────────────────────────────────
// ⚠️  BUMP THIS NUMBER each time you flash new firmware.
//     On boot, if EEPROM doesn't hold this exact value the device
//     wipes all saved WiFi + device config automatically — so it
//     always starts fresh in captive-portal mode after a reflash.
#define FORMAT_VERSION    0x08
#define EEPROM_FORMAT_VER 95    // address 95: firmware-format marker

// ══════════════════════════════════════════════════════════════
//  EEPROM HELPERS
// ══════════════════════════════════════════════════════════════

bool loadWiFiCredentials(String &ssid, String &pass) {
  EEPROM.begin(EEPROM_SIZE);
  int ssidLen = EEPROM.read(EEPROM_WIFI_SSID_LEN);
  if (ssidLen <= 0 || ssidLen >= 32) {
    EEPROM.end();
    return false;
  }

  char buf[128];
  for (int i = 0; i < ssidLen; i++)
    buf[i] = EEPROM.read(EEPROM_WIFI_SSID + i);
  buf[ssidLen] = '\0';
  ssid = String(buf);

  int passLen = 0;
  while (passLen < 63 && EEPROM.read(EEPROM_WIFI_PASS + passLen) != 0)
    passLen++;
  for (int i = 0; i < passLen; i++)
    buf[i] = EEPROM.read(EEPROM_WIFI_PASS + i);
  buf[passLen] = '\0';
  pass = String(buf);

  EEPROM.end();
  return ssid.length() > 0;
}

void saveWiFiCredentials(const String &ssid, const String &pass) {
  EEPROM.begin(EEPROM_SIZE);
  int ssidLen = min((int)ssid.length(), 31);
  EEPROM.write(EEPROM_WIFI_SSID_LEN, ssidLen);
  for (int i = 0; i < ssidLen; i++)
    EEPROM.write(EEPROM_WIFI_SSID + i, ssid[i]);
  EEPROM.write(EEPROM_WIFI_SSID + ssidLen, '\0');

  int passLen = min((int)pass.length(), 63);
  for (int i = 0; i < passLen; i++)
    EEPROM.write(EEPROM_WIFI_PASS + i, pass[i]);
  EEPROM.write(EEPROM_WIFI_PASS + passLen, '\0');

  EEPROM.commit();
  EEPROM.end();
}

// Read a length-prefixed string from EEPROM at addr (advances addr)
static String eepromReadStr(int &addr) {
  int len = min((int)EEPROM.read(addr++), MAX_STR);
  char buf[MAX_STR + 1];
  for (int i = 0; i < len; i++)
    buf[i] = EEPROM.read(addr++);
  buf[len] = '\0';
  return String(buf);
}

// Write a length-prefixed string to EEPROM at addr (advances addr)
static void eepromWriteStr(int &addr, const String &s) {
  int len = min((int)s.length(), MAX_STR);
  EEPROM.write(addr++, len);
  for (int i = 0; i < len; i++)
    EEPROM.write(addr++, s[i]);
}

bool loadDeviceConfig() {
  EEPROM.begin(EEPROM_SIZE);
  if (EEPROM.read(EEPROM_CFG_MARKER) != CFG_MARKER_VALUE) {
    EEPROM.end();
    return false;
  }

  isAdopted = (EEPROM.read(EEPROM_CFG_ADOPTED) == 1);
  int addr = EEPROM_CFG_START;

  OPENIOT_SERVER = eepromReadStr(addr);
  DEVICE_ID = eepromReadStr(addr);
  ADOPT_TOKEN = eepromReadStr(addr);
  MQTT_HOST = eepromReadStr(addr);
  MQTT_PORT = ((int)EEPROM.read(addr) << 8) | EEPROM.read(addr + 1);
  addr += 2;
  MQTT_USER = eepromReadStr(addr);
  MQTT_PASS = eepromReadStr(addr);

  EEPROM.end();
  Serial.println("✅ Device config loaded from EEPROM");
  Serial.println("   Server:    " + OPENIOT_SERVER);
  Serial.println("   Device ID: " + DEVICE_ID);
  Serial.println("   MQTT:      " + MQTT_HOST + ":" + String(MQTT_PORT));
  Serial.println("   Adopted:   " + String(isAdopted));
  return DEVICE_ID.length() > 0;
}

void saveDeviceConfig() {
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(EEPROM_CFG_MARKER, CFG_MARKER_VALUE);
  EEPROM.write(EEPROM_CFG_ADOPTED, isAdopted ? 1 : 0);

  int addr = EEPROM_CFG_START;
  eepromWriteStr(addr, OPENIOT_SERVER);
  eepromWriteStr(addr, DEVICE_ID);
  eepromWriteStr(addr, ADOPT_TOKEN);
  eepromWriteStr(addr, MQTT_HOST);
  EEPROM.write(addr++, (MQTT_PORT >> 8) & 0xFF);
  EEPROM.write(addr++, MQTT_PORT & 0xFF);
  eepromWriteStr(addr, MQTT_USER);
  eepromWriteStr(addr, MQTT_PASS);

  EEPROM.commit();
  EEPROM.end();
  Serial.println("✅ Device config saved to EEPROM");
}

void clearAllConfig() {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++)
    EEPROM.write(i, 0);
  EEPROM.commit();
  EEPROM.end();
  Serial.println("🗑️  EEPROM cleared");
}

// ── Check for factory reset on boot ───────────────────────────
// If RESET_PIN (FLASH button) is held LOW for 3 seconds → wipe all
// EEPROM and restart into captive-portal mode.
void checkFactoryReset() {
  pinMode(RESET_PIN, INPUT_PULLUP);

  if (digitalRead(RESET_PIN) != LOW) return; // button not held — skip

  Serial.println("\n⚠️  FLASH button held — factory reset in 3 s...");
  Serial.println("   Release now to cancel.");

  unsigned long start = millis();

  while (digitalRead(RESET_PIN) == LOW) {
    int held = (millis() - start) / 1000;
    Serial.printf("   %d...\n", 3 - held);
    // Blink LED rapidly as countdown feedback
    digitalWrite(LED_BUILTIN, LOW);  delay(120);
    digitalWrite(LED_BUILTIN, HIGH); delay(120);
    if (millis() - start >= 3000) break;
  }

  if (millis() - start < 3000) {
    Serial.println("   Cancelled — resuming normal boot.");
    return;
  }

  Serial.println("🔄 Factory reset! Clearing all config...");
  // Flash LED 5× to confirm
  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_BUILTIN, LOW);  delay(80);
    digitalWrite(LED_BUILTIN, HIGH); delay(80);
  }

  clearAllConfig();
  delay(300);
  Serial.println("🔄 Restarting into captive portal mode...");
  ESP.restart();
}

// ══════════════════════════════════════════════════════════════
//  WIFI HELPERS
// ══════════════════════════════════════════════════════════════

int scanWiFiNetworks(String &resultsJson) {
  Serial.println("Scanning WiFi networks…");
  int n = WiFi.scanNetworks();
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n; i++) {
    JsonObject net = arr.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);
    net["encryption"] = (WiFi.encryptionType(i) != WIFI_OPEN) ? "WPA2" : "Open";
    net["channel"] = WiFi.channel(i);
  }
  serializeJson(doc, resultsJson);
  WiFi.scanDelete();
  return n;
}

bool connectToWiFi(const String &ssid, const String &pass = "") {
  Serial.printf("Connecting to WiFi: %s\n", ssid.c_str());
  if (pass.length() > 0)
    WiFi.begin(ssid.c_str(), pass.c_str());
  else
    WiFi.begin(ssid.c_str());

  int timeout = 30; // 15 seconds max
  while (WiFi.status() != WL_CONNECTED && timeout-- > 0) {
    delay(500);
    Serial.print(".");
    server.handleClient(); // keep portal alive during wait
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n✅ WiFi connected! IP: %s\n",
                  WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("\n❌ WiFi connection failed");
  return false;
}

void startCaptivePortal() {
  Serial.println("Starting OpenIoT-Setup AP…");
  WiFi.mode(WIFI_AP);  // AP-only mode for reliable captive portal
  WiFi.softAP("OpenIoT-Setup", "openiot123");
  delay(500);          // give the AP interface time to initialise

  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("AP IP: %s\n", apIP.toString().c_str());
  Serial.println("Connect to: OpenIoT-Setup  |  Password: openiot123");

  // DNS server: redirect every hostname to the ESP's AP IP
  // so phones auto-detect the captive portal
  dnsServer.start(53, "*", apIP);
  apMode = true;
}

void setupWiFi() {
  String savedSsid, savedPass;
  if (loadWiFiCredentials(savedSsid, savedPass)) {
    Serial.println("Trying saved WiFi credentials…");
    WiFi.mode(WIFI_STA);
    if (connectToWiFi(savedSsid, savedPass))
      return;
    Serial.println("Saved network unavailable — falling back to setup portal");
  }
  startCaptivePortal();
}

// ══════════════════════════════════════════════════════════════
//  CAPTIVE PORTAL HTML  (defined in portal_html.h)
//  Kept in a separate header to prevent the Arduino IDE's
//  auto-prototype injector from misreading JS "function" keywords
// ══════════════════════════════════════════════════════════════
#include "portal_html.h"

// ══════════════════════════════════════════════════════════════
//  FORWARD DECLARATIONS
// ══════════════════════════════════════════════════════════════
void publishState();

// ══════════════════════════════════════════════════════════════
//  HTTP HANDLERS
// ══════════════════════════════════════════════════════════════

// GET / — serve captive portal UI
// (registered as lambda in setupWebServer)

// GET /setup/qr — return current device config as JSON (for QR display)
void handleQRStatus() {
  DynamicJsonDocument doc(256);
  if (DEVICE_ID.length() > 0 && ADOPT_TOKEN.length() > 0) {
    doc["exists"] = true;
    doc["server"] = OPENIOT_SERVER;
    doc["device_id"] = DEVICE_ID;
    doc["token"] = ADOPT_TOKEN;
    doc["mqtt_host"] = MQTT_HOST;
    doc["mqtt_port"] = MQTT_PORT;
  } else {
    doc["exists"] = false;
  }
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

// GET /scan — return nearby WiFi networks as JSON
void handleWiFiScan() {
  String json;
  scanWiFiNetworks(json);
  server.send(200, "application/json", json);
}

// POST /setup/save — receive config from portal, connect WiFi, restart
void handleSaveManual() {
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"no_data\"}");
    return;
  }

  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  String ssid = doc["ssid"].as<String>();
  String wifiPwd = doc["password"].as<String>();
  OPENIOT_SERVER = doc["server"].as<String>();
  DEVICE_ID = doc["device_id"].as<String>();
  ADOPT_TOKEN = doc["token"].as<String>();
  MQTT_HOST = doc["mqtt_host"].as<String>();
  MQTT_PORT = doc["mqtt_port"] | 1883;
  MQTT_USER = doc["mqtt_user"].as<String>();
  MQTT_PASS = doc["mqtt_pass"].as<String>();

  Serial.println("📋 Config received via portal");
  Serial.println("   Server: " + OPENIOT_SERVER);
  Serial.println("   Device: " + DEVICE_ID);
  Serial.println("   MQTT:   " + MQTT_HOST + ":" + String(MQTT_PORT));

  // Connect to home WiFi while keeping AP alive (WIFI_AP_STA mode)
  if (ssid.length() > 0) {
    WiFi.mode(WIFI_AP_STA);  // Keep captive-portal AP up during STA connect
    if (!connectToWiFi(ssid, wifiPwd)) {
      server.send(400, "application/json",
                  "{\"error\":\"wifi_failed\",\"message\":\"Could not connect "
                  "to WiFi. Check the network name and password.\"}");
      return;
    }
    apMode = false;           // Stop DNS redirect — STA connection is live
    saveWiFiCredentials(ssid, wifiPwd);
  }

  // Save full device config to EEPROM
  isAdopted = false; // will be set true after adoption on next boot
  saveDeviceConfig();

  String ip = WiFi.localIP().toString();
  server.send(200, "application/json",
              "{\"status\":\"ok\",\"device_id\":\"" + DEVICE_ID +
                  "\",\"ip\":\"" + ip + "\"}");

  // Schedule restart in 2 s (gives the HTTP response time to reach browser)
  pendingRestart = true;
  restartAt = millis() + 2000;
}

// POST /wifi/connect — optional: connect to WiFi from a separate request
void handleWiFiConnect() {
  String ssid = server.arg("ssid");
  String pass = server.arg("password");
  if (ssid.length() == 0) {
    server.send(400, "application/json", "{\"error\":\"no_ssid\"}");
    return;
  }
  if (connectToWiFi(ssid, pass)) {
    saveWiFiCredentials(ssid, pass);
    server.send(200, "application/json",
                "{\"status\":\"connected\",\"ip\":\"" +
                    WiFi.localIP().toString() + "\"}");
  } else {
    server.send(400, "application/json", "{\"error\":\"connection_failed\"}");
  }
}

// ══════════════════════════════════════════════════════════════
//  DEVICE ADOPTION
// ══════════════════════════════════════════════════════════════

bool adoptDevice() {
  if (OPENIOT_SERVER.length() == 0 || DEVICE_ID.length() == 0 ||
      ADOPT_TOKEN.length() == 0) {
    Serial.println("⚠️  Missing server/device/token — skipping adoption");
    return false;
  }

  WiFiClient httpClient;
  HTTPClient http;
  http.begin(httpClient, OPENIOT_SERVER + "/api/devices/adopt");
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(512);  // v6 API (JsonDocument is v7 only)
  doc["token"] = ADOPT_TOKEN;
  doc["device_id"] = DEVICE_ID;
  doc["firmware_version"] = "2.1.0";
  doc["ip_address"] = WiFi.localIP().toString();
  doc["mac_address"] = WiFi.macAddress();
#ifdef ESP32
  doc["chip_model"] = "ESP32";
#else
  doc["chip_model"] = "ESP8266";
#endif

  String payload;
  serializeJson(doc, payload);
  Serial.println("📤 Sending adoption request…");

  int code = http.POST(payload);
  if (code == 200) {
    DynamicJsonDocument res(512);  // v6 API
    deserializeJson(res, http.getString());
    MQTT_USER = res["mqtt_username"].as<String>();
    MQTT_PASS = res["mqtt_password"].as<String>();
    isAdopted = true;
    saveDeviceConfig(); // persist updated MQTT creds + adopted flag
    Serial.println("✅ Adopted! MQTT user: " + MQTT_USER);
    http.end();
    return true;
  }

  Serial.printf("❌ Adoption failed: HTTP %d  %s\n", code,
                http.getString().c_str());
  http.end();
  return false;
}

// ══════════════════════════════════════════════════════════════
//  MQTT
// ══════════════════════════════════════════════════════════════

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++)
    message += (char)payload[i];
  Serial.println("📨 Command: " + message);

  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, message))
    return;

  String cmd = doc["command"].as<String>();

  if (cmd == "ping") {
    Serial.println("🏓 Pong!");
    publishState();
  } else if (cmd == "restart") {
    Serial.println("🔄 Restarting…");
    delay(500);
    ESP.restart();
  } else if (cmd == "led_on" || cmd == "turn_on" || cmd == "on") {
    relayState = true;
    digitalWrite(LED_BUILTIN, LOW); // Usually inverted on ESPs
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("💡 RELAY ON");
    publishState();
  } else if (cmd == "led_off" || cmd == "turn_off" || cmd == "off") {
    relayState = false;
    digitalWrite(LED_BUILTIN, HIGH); 
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("💡 RELAY OFF");
    publishState();
  } else if (cmd == "toggle") {
    relayState = !relayState;
    digitalWrite(LED_BUILTIN, relayState ? LOW : HIGH);
    digitalWrite(RELAY_PIN, relayState ? HIGH : LOW);
    Serial.println(relayState ? "💡 RELAY TOGGLED ON" : "💡 RELAY TOGGLED OFF");
    publishState();
  }
}

// Connect MQTT with limited retries (does NOT block forever)
void connectMQTT() {
  mqttClient.setServer(MQTT_HOST.c_str(), MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  int retries = 5;
  while (!mqttClient.connected() && retries-- > 0) {
    Serial.printf("Connecting to MQTT (%d left)… ", retries + 1);
    String clientId = "openiot-" + DEVICE_ID;
    if (mqttClient.connect(clientId.c_str(), MQTT_USER.c_str(),
                           MQTT_PASS.c_str())) {
      Serial.println("✅ connected!");
      mqttClient.subscribe(("openiot/" + DEVICE_ID + "/command").c_str());
      mqttClient.publish(("openiot/" + DEVICE_ID + "/availability").c_str(),
                         "online", true);
    } else {
      Serial.printf("❌ rc=%d, waiting 5 s\n", mqttClient.state());
      delay(5000);
    }
  }
  if (!mqttClient.connected())
    Serial.println("⚠️  MQTT unavailable — will retry in loop");
}

// ── Publish sensor readings ────────────────────────────────────
// Replace the PLACEHOLDER lines with your actual sensor reads.
void publishState() {
  if (!mqttClient.connected() || DEVICE_ID.length() == 0)
    return;

  DynamicJsonDocument doc(256);
  // Send the actual physical state of the relay/switch to the dashboard
  doc["relay_status"] = relayState ? "ON" : "OFF";
  doc["is_on"] = relayState;
  doc["uptime"] = millis() / 1000;                     // seconds

  String payload;
  serializeJson(doc, payload);
  mqttClient.publish(("openiot/" + DEVICE_ID + "/state").c_str(),
                     payload.c_str());
  Serial.println("📤 Published: " + payload);
}

// ══════════════════════════════════════════════════════════════
//  WEB SERVER SETUP
// ══════════════════════════════════════════════════════════════

// Catch-all: redirect any unknown URL to the portal root.
// This is what triggers the "Sign in to network" popup on phones/laptops.
void handleCaptivePortalRedirect() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
  server.send(302, "text/plain", "");
}

void setupWebServer() {
  server.on("/", HTTP_GET,
            []() { server.send_P(200, "text/html", ROOT_HTML); });
  server.on("/scan", HTTP_GET, handleWiFiScan);
  server.on("/setup/qr", HTTP_GET, handleQRStatus);
  server.on("/setup/save", HTTP_POST, handleSaveManual);
  server.on("/wifi/connect", HTTP_POST, handleWiFiConnect);

  // ── Factory reset via browser ──────────────────────────────────
  // While connected to the device's LAN IP, visit http://<device-ip>/reset
  // to wipe all config and restart into captive-portal mode.
  server.on("/reset", HTTP_GET, []() {
    server.send(200, "text/html",
      "<html><body style='font-family:sans-serif;background:#0d1117;color:#e6edf3;padding:30px'>"
      "<h2>\u2705 Factory Reset</h2>"
      "<p>EEPROM cleared. Device restarting into setup mode in 2 seconds...</p>"
      "<p><em>Connect to <strong>OpenIoT-Setup</strong> WiFi (pw: openiot123)</em></p>"
      "</body></html>");
    pendingRestart = true;
    restartAt = millis() + 2000;
    // Schedule EEPROM clear before restart
    clearAllConfig();
    Serial.println("\U0001f504 Remote factory reset triggered via /reset");
  });

  // Common captive-portal probe URLs used by iOS, Android, Windows
  auto portalRedirect = []() { handleCaptivePortalRedirect(); };
  server.on("/generate_204", HTTP_GET, portalRedirect);          // Android
  server.on("/redirect", HTTP_GET, portalRedirect);              // Android
  server.on("/hotspot-detect.html", HTTP_GET, portalRedirect);   // iOS
  server.on("/library/test/success.html", HTTP_GET, portalRedirect); // iOS older
  server.on("/ncsi.txt", HTTP_GET, portalRedirect);              // Windows
  server.on("/connecttest.txt", HTTP_GET, portalRedirect);       // Windows
  server.on("/fwlink", HTTP_GET, portalRedirect);                // Windows

  // Catch all other unknown paths and redirect to portal
  server.onNotFound(handleCaptivePortalRedirect);

  server.begin();
  Serial.println("HTTP server started on port 80");
}

// ══════════════════════════════════════════════════════════════
//  SETUP & LOOP
// ══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n════════════════════════════════════");
  Serial.println("  Open IoT ESP Firmware  v2.1.0    ");
  Serial.println("════════════════════════════════════\n");

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // LED off (active-low on most boards)

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relay off automatically on boot

  // ── Factory reset check — MUST run before WiFi/EEPROM init ────
  // Hold the FLASH/BOOT button (GPIO0) for 3 s on power-on to
  // wipe all saved WiFi + device config and restart in portal mode.
  checkFactoryReset();

  // ── Post-flash EEPROM clear ────────────────────────────────────
  // EEPROM survives firmware flashing, so old WiFi/device config
  // would still be there after a reflash. This check detects a new
  // firmware version (FORMAT_VERSION mismatch) and wipes EEPROM so
  // the device always boots into the captive portal after a reflash.
  // Bump FORMAT_VERSION in the #define above before each flash.
  {
    EEPROM.begin(EEPROM_SIZE);
    if (EEPROM.read(EEPROM_FORMAT_VER) != FORMAT_VERSION) {
      Serial.println("\n🆕 New firmware detected — clearing EEPROM for clean boot...");
      for (int i = 0; i < EEPROM_SIZE; i++) EEPROM.write(i, 0);
      EEPROM.write(EEPROM_FORMAT_VER, FORMAT_VERSION);
      EEPROM.commit();
      Serial.println("✅ EEPROM cleared. Starting captive portal setup.");
    }
    EEPROM.end();
  }

  // ── IMPORTANT: WiFi must be set up BEFORE the web server starts ──
  // This ensures the server binds to the AP interface when in portal mode.
  setupWiFi();

  // Start web server (routes + DNS already running if in AP mode)
  setupWebServer();

  // Load device config from EEPROM
  bool hasConfig = loadDeviceConfig();

  if (hasConfig && DEVICE_ID.length() > 0) {
    if (isAdopted) {
      // Already adopted — just reconnect MQTT with saved credentials
      Serial.println("📡 Already adopted — connecting MQTT…");
      if (MQTT_HOST.length() > 0)
        connectMQTT();
    } else if (ADOPT_TOKEN.length() > 0 && WiFi.status() == WL_CONNECTED) {
      // Have config but not yet adopted — try adoption
      Serial.println("📡 Attempting device adoption…");
      isAdopted = adoptDevice();
      if (isAdopted && MQTT_HOST.length() > 0)
        connectMQTT();
    }
  } else {
    Serial.println("⚠️  No device config — waiting for captive portal setup");
    Serial.printf("   Connect to : OpenIoT-Setup  |  password: openiot123\n");
    Serial.printf("   Then open  : http://%s\n",
                  WiFi.softAPIP().toString().c_str());
  }
}

void loop() {
  // Process DNS queries (captive portal redirect) — must be first
  if (apMode)
    dnsServer.processNextRequest();

  server.handleClient();

  // Deferred restart — lets the HTTP response reach the browser before
  // rebooting
  if (pendingRestart && millis() >= restartAt) {
    Serial.println("🔄 Restarting with new config…");
    delay(100);
    ESP.restart();
  }

  // MQTT keep-alive (only when fully adopted)
  if (isAdopted && MQTT_HOST.length() > 0) {
    if (!mqttClient.connected())
      connectMQTT();
    mqttClient.loop();
  }

  // Periodic sensor publish (only when adopted + MQTT connected)
  if (isAdopted && mqttClient.connected() &&
      millis() - lastPublish >= PUBLISH_INTERVAL) {
    publishState();
    lastPublish = millis();
  }
}
