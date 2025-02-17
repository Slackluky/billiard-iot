#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ESP32Time.h>

// Wi-Fi credentials
const char* ssid = "DZS_GONT2G_83F0";
const char* password = "1234567890a";

// Static IP configuration
IPAddress local_IP(192, 168, 1, 19);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

// Set DNS servers (Google DNS)
IPAddress dns1(8, 8, 8, 8);
IPAddress dns2(8, 8, 4, 4);

// Timezone configuration
const long gmtOffset_sec = 7 * 3600;
const int daylightOffset_sec = 0;

ESP32Time rtc;
WebServer server(80);
struct tm timeinfo;

bool manual_time = false;

// Relay GPIO pins
const int relayPins[] = { 32, 33, 25, 26, 27, 14, 12, 13 };
const int relayCount = sizeof(relayPins) / sizeof(relayPins[0]);

// Relay control variables
String relayOnDatetime[relayCount];
String relayOffDatetime[relayCount];
String relayMode[relayCount];  // "manual" or "auto"

// Helper function to parse datetime
bool parseDateTime(const char* datetime, struct tm& timeinfo) {
  memset(&timeinfo, 0, sizeof(struct tm));
  int parsedItems = sscanf(datetime, "%d-%d-%d %d:%d",
                           &timeinfo.tm_year, &timeinfo.tm_mon, &timeinfo.tm_mday,
                           &timeinfo.tm_hour, &timeinfo.tm_min);
  if (parsedItems == 5) {
    timeinfo.tm_year -= 1900;
    timeinfo.tm_mon -= 1;
    return true;
  }
  return false;
}

// Handle CORS preflight
void handlePreflight() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");
  server.sendHeader("Access-Control-Max-Age", "3600");
  server.send(200);
}

// Handle individual relay control
void handleRelayControl() {
  server.sendHeader("Access-Control-Allow-Origin", "*");                                              // Allow all origins
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");               // Allowed HTTP methods
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");  // Allowed request headers
  server.sendHeader("Access-Control-Max-Age", "3600");                                                // Cache preflight request for 1 hour

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Invalid request body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<200> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, body);

  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
    return;
  }

  if (!jsonDoc.containsKey("relay") || !jsonDoc.containsKey("state")) {
    server.send(400, "application/json", "{\"error\":\"Missing 'relay' or 'state' field\"}");
    return;
  }

  int relay = jsonDoc["relay"];
  bool state = jsonDoc["state"];

  if (relay < 1 || relay > relayCount) {
    server.send(400, "application/json", "{\"error\":\"Invalid relay number\"}");
    return;
  }

  digitalWrite(relayPins[relay - 1], state ? HIGH : LOW);
  relayMode[relay - 1] = "manual";

  StaticJsonDocument<200> response;
  response["status"] = "success";
  response["relay"] = relay;
  response["state"] = state ? "on" : "off";

  String responseBody;
  serializeJson(response, responseBody);
  server.send(200, "application/json", responseBody);
}


void handleRelayCheck() {
  server.sendHeader("Access-Control-Allow-Origin", "*");                                              // Allow all origins
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");               // Allowed HTTP methods
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");  // Allowed request headers
  server.sendHeader("Access-Control-Max-Age", "3600");                                                // Cache preflight request for 1 hour

  // Create a JSON response
  StaticJsonDocument<500> response;
  JsonArray relays = response.createNestedArray("relays");

  for (int i = 0; i < relayCount; i++) {
    JsonObject relayStatus = relays.createNestedObject();
    relayStatus["relay"] = i + 1;
    relayStatus["pin"] = relayPins[i];

    // Test relay by turning it ON for 1 second
    digitalWrite(relayPins[i], HIGH);
    delay(2000);  // Keep it on for 1 second
    digitalWrite(relayPins[i], LOW);

    relayStatus["tested"] = "success";
  }

  String responseBody;
  serializeJson(response, responseBody);
  server.send(200, "application/json", responseBody);
}

void handleUpdateRelays() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");
  server.sendHeader("Access-Control-Max-Age", "3600");

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Invalid request body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<2048> jsonDoc;  // Adjust size based on expected payload
  DeserializationError error = deserializeJson(jsonDoc, body);

  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
    return;
  }

  if (!jsonDoc.containsKey("data")) {
    server.send(400, "application/json", "{\"error\":\"Missing 'data' field\"}");
    return;
  }

  JsonArray relayUpdates = jsonDoc["data"].as<JsonArray>();
  StaticJsonDocument<2048> response;
  JsonArray results = response.createNestedArray("results");

  for (JsonObject relayUpdate : relayUpdates) {
    JsonObject result = results.createNestedObject();

    if (!relayUpdate.containsKey("relay") || !relayUpdate.containsKey("state") || !relayUpdate.containsKey("mode") || !relayUpdate.containsKey("startDatetime") || !relayUpdate.containsKey("endDatetime")) {
      result["error"] = "Missing 'relay', 'state', 'mode', 'startDatetime', or 'endDatetime'";
      continue;
    }

    int relay = relayUpdate["relay"];
    bool state = relayUpdate["state"];
    String mode = relayUpdate["mode"];
    String startDatetime = relayUpdate["startDatetime"];
    String endDatetime = relayUpdate["endDatetime"];

    if (relay < 1 || relay > relayCount) {
      result["relay"] = relay;
      result["error"] = "Invalid relay number";
      continue;
    }

    if (manual_time) {
      if (parseDateTime(String(startDatetime).c_str(), timeinfo)) {
        time_t manualTime = mktime(&timeinfo);
        struct timeval now = { manualTime, 0 };
        manual_time = false;
        settimeofday(&now, NULL);  // Set the system time manually
        Serial.println("Manual time set successfully.");
      } else {
        Serial.println("Failed to parse manual time.");
      }
    }

    // Update relay state
    digitalWrite(relayPins[relay - 1], state ? HIGH : LOW);
    relayMode[relay - 1] = mode;

    // Update relay timer
    relayOnDatetime[relay - 1] = startDatetime;
    relayOffDatetime[relay - 1] = endDatetime;

    result["relay"] = relay;
    result["status"] = "success";
    result["state"] = state ? "on" : "off";
    result["mode"] = mode;
    result["startDatetime"] = startDatetime;
    result["endDatetime"] = endDatetime;
  }

  String responseBody;
  serializeJson(response, responseBody);
  server.send(200, "application/json", responseBody);
}

// Handle relay timer control
void handleRelayTimer() {
  server.sendHeader("Access-Control-Allow-Origin", "*");                                              // Allow all origins
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");               // Allowed HTTP methods
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");  // Allowed request headers
  server.sendHeader("Access-Control-Max-Age", "3600");                                                // Cache preflight request for 1 hour

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"error\":\"Invalid request body\"}");
    return;
  }

  String body = server.arg("plain");
  StaticJsonDocument<300> jsonDoc;
  DeserializationError error = deserializeJson(jsonDoc, body);

  if (error) {
    server.send(400, "application/json", "{\"error\":\"Invalid JSON format\"}");
    return;
  }

  if (!jsonDoc.containsKey("relay") || !jsonDoc.containsKey("startDatetime") || !jsonDoc.containsKey("endDatetime")) {
    server.send(400, "application/json", "{\"error\":\"Missing required fields\"}");
    return;
  }



  int relay = jsonDoc["relay"];
  if (relay < 1 || relay > relayCount) {
    server.send(400, "application/json", "{\"error\":\"Invalid relay number\"}");
    return;
  }

  digitalWrite(relayPins[relay - 1], HIGH);
  relayOnDatetime[relay - 1] = String(jsonDoc["startDatetime"]);
  relayOffDatetime[relay - 1] = String(jsonDoc["endDatetime"]);
  relayMode[relay - 1] = "auto";


  if (manual_time) {
    if (parseDateTime(String(jsonDoc["startDatetime"]).c_str(), timeinfo)) {
      time_t manualTime = mktime(&timeinfo);
      struct timeval now = { manualTime, 0 };
      manual_time = false;
      settimeofday(&now, NULL);  // Set the system time manually
      Serial.println("Manual time set successfully.");
    } else {
      Serial.println("Failed to parse manual time.");
    }
  }

  StaticJsonDocument<200> response;
  response["status"] = "success";
  response["relay"] = relay;
  response["startDatetime"] = relayOnDatetime[relay - 1];
  response["endDatetime"] = relayOffDatetime[relay - 1];

  String responseBody;
  serializeJson(response, responseBody);
  server.send(200, "application/json", responseBody);
}

// API to get the status of all relays
void handleGetStatus() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type, X-Custom-Header, Authorization");
  server.sendHeader("Access-Control-Max-Age", "3600");

  StaticJsonDocument<1024> jsonDoc;
  JsonArray relays = jsonDoc.createNestedArray("relays");

  for (int i = 0; i < relayCount; i++) {
    JsonObject relay = relays.createNestedObject();
    relay["relay"] = i + 1;  // Relay number (1-based index)
    relay["state"] = digitalRead(relayPins[i]) == HIGH ? "ON" : "OFF";
    relay["startDatetime"] = relayOnDatetime[i];
    relay["endDatetime"] = relayOffDatetime[i];
    relay["mode"] = relayMode[i];  // "manual" or "auto"
  }

  String response;
  serializeJson(jsonDoc, response);
  server.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < relayCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW);
    relayMode[i] = "manual";
  }

  if (!WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
    Serial.println("Static IP configuration failed!");
  }

  WiFi.mode(WIFI_STA);                  // Set Wi-Fi to station mode
  WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Set maximum TX power for better range
  WiFi.begin(ssid, password);

  int maxRetries = 3;
  int attempt = 0;


  // Retry Wi-Fi connection up to maxRetries times
  while (WiFi.status() != WL_CONNECTED && attempt < maxRetries) {
    delay(1000);
    Serial.printf("Attempting to connect to Wi-Fi (Attempt %d/%d)...\n", attempt + 1, maxRetries);
    attempt++;
  }


  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi connected!");
    configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org");

    if (getLocalTime(&timeinfo)) {
      rtc.setTimeStruct(timeinfo);  // Sync ESP32Time with NTP time
      Serial.println("RTC updated with NTP time");
    } else {
      manual_time = true;
      Serial.println("Failed to obtain time from NTP");
    }
  } else {
    Serial.println("Failed to connect to Wi-Fi after 3 attempts. Retrying automatically...");
    WiFi.disconnect(true);  // Reset Wi-Fi stack
    delay(1000);
    ESP.restart();  // Restart the device
  }

  server.on("/api/led", HTTP_POST, handleRelayControl);
  server.on("/api/ledOffTime", HTTP_POST, handleRelayTimer);
  server.on("/api/status", HTTP_GET, handleGetStatus);
  server.on("/api/relay-check", HTTP_GET, handleRelayCheck);

  server.on("/api/relay-check", HTTP_OPTIONS, handlePreflight);
  server.on("/api/status", HTTP_OPTIONS, handlePreflight);
  server.on("/api/led", HTTP_OPTIONS, handlePreflight);
  server.on("/api/ledOffTime", HTTP_OPTIONS, handlePreflight);

  server.on("/api/update-relays", HTTP_POST, handleUpdateRelays);
  server.on("/api/update-relays", HTTP_OPTIONS, handlePreflight);


  server.begin();
}

void loop() {
  server.handleClient();
  struct tm currentTimeInfo = rtc.getTimeStruct();
  time_t currentTime = mktime(&currentTimeInfo);

  for (int i = 0; i < relayCount; i++) {
    if (relayMode[i] == "auto") {
      struct tm relayOffTimeInfo;
      parseDateTime(relayOffDatetime[i].c_str(), relayOffTimeInfo);
      time_t relayOffTime = mktime(&relayOffTimeInfo);

      time_t tenMinutesBefore = relayOffTime - (10 * 60);
      time_t threeMinutesBefore = relayOffTime - (5 * 60);

      if (currentTime == tenMinutesBefore && currentTime < threeMinutesBefore) {
          digitalWrite(relayPins[i], LOW);
          delay(500);
          digitalWrite(relayPins[i], HIGH);
      }

      if (currentTime == threeMinutesBefore && currentTime < relayOffTime) {
        for (int j = 0; j < 3; j++) {
          digitalWrite(relayPins[i], LOW);
          delay(500);
          digitalWrite(relayPins[i], HIGH);
          delay(500);
        }
      }

      if (currentTime >= relayOffTime) {
        digitalWrite(relayPins[i], LOW);
      }
    }
  }
  delay(1000);
}
