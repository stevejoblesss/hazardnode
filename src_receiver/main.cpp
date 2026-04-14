#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

/* WIFI CONFIG */
const char* currentSSID = "Unicorn2012";
const char* currentPass = "Finance@5408";

/* GATEWAY CONFIG */
const char* gatewayID = "Gateway 1";

/* API ENDPOINT */
const char *serverURL = "https://hazardnode-dashboard.vercel.app/api/node";

/* STRUCT */
typedef struct __attribute__((packed)) struct_message
{
  char nodeID[32];
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int edgeAIClass; // 0=NORMAL, 1=WARNING, 2=HAZARD
} struct_message;

struct_message data;
// Size: 32 + 4*4 + 4 + 1 + 1 + 4 = 58 bytes
const int PACKET_SIZE = 58;
uint8_t rxBuffer[PACKET_SIZE];
int lastRssi = -60; // Default RSSI
volatile bool newDataAvailable = false;

/* RECEIVE CALLBACK */
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
{
  if (info->rx_ctrl) {
      lastRssi = info->rx_ctrl->rssi;
  }
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  lastRssi = -55; 
#endif
  if (len != PACKET_SIZE)
  {
    Serial.printf("Invalid packet length: received %d, expected %d\n", len, PACKET_SIZE);
    return;
  }
  memcpy(rxBuffer, incomingData, PACKET_SIZE);
  newDataAvailable = true;
}

bool connectToWiFi(const char *ssid, const char *pass, int timeout_s = 15)
{
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, pass);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < (timeout_s * 1000))
  {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi Connected!");
    return true;
  }
  Serial.println("\nWiFi Connection Failed.");
  return false;
}

void uploadToServer(String id, String type, float t, float h, float p, float r, int sa, bool sd, bool d, int rssiVal, int aiClass)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Disconnected, attempting to reconnect...");
    connectToWiFi(currentSSID, currentPass);
    if (WiFi.status() != WL_CONNECTED) return;
  }

  JsonDocument doc;
  doc["nodeID"] = id;
  doc["type"] = type;
  doc["rssi"] = rssiVal;
  doc["temp"] = round(t * 100) / 100.0;
  doc["hum"] = round(h * 100) / 100.0;
  doc["pitch"] = round(p * 100) / 100.0;
  doc["roll"] = round(r * 100) / 100.0;
  doc["smokeAnalog"] = sa;
  doc["smokeDigital"] = sd;
  doc["danger"] = d;
  doc["edgeAIClass"] = aiClass;

  String json;
  serializeJson(doc, json);

  if (type == "receiver")
    Serial.printf("[GATEWAY] ID: %s | WiFi RSSI: %d dBm\n", id.c_str(), rssiVal);
  else
    Serial.printf("[NODE] ID: %s | Node RSSI: %d dBm\n", id.c_str(), rssiVal);

  Serial.println(json);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(5000);

  if (http.begin(client, serverURL))
  {
    http.addHeader("Content-Type", "application/json");
    int httpResponseCode = http.POST(json);

    if (httpResponseCode > 0)
    {
      Serial.printf("HTTP Response code: %d\n", httpResponseCode);
      String response = http.getString();
      Serial.println("Server Response: " + response);
    }
    else
    {
      Serial.printf("HTTP Error code: %d\n", httpResponseCode);
    }
    http.end();
  }
}

void uploadData()
{
  // Unpack buffer into struct
  char rxNodeID[33];
  memcpy(rxNodeID, rxBuffer + 0, 32);
  rxNodeID[32] = '\0'; // Ensure null termination
  
  memcpy(&data.temp, rxBuffer + 32, 4);
  memcpy(&data.hum, rxBuffer + 36, 4);
  memcpy(&data.pitch, rxBuffer + 40, 4);
  memcpy(&data.roll, rxBuffer + 44, 4);
  memcpy(&data.smokeAnalog, rxBuffer + 48, 4);
  data.smokeDigital = (rxBuffer[52] != 0);
  data.danger = (rxBuffer[53] != 0);
  memcpy(&data.edgeAIClass, rxBuffer + 54, 4);

  // Original arg order: id, type, t, h, p, r, sa, sd, d, rssiVal, aiClass
  uploadToServer(String(rxNodeID), "sender", data.temp, data.hum, data.pitch, data.roll, data.smokeAnalog, data.smokeDigital, data.danger, lastRssi, data.edgeAIClass);
}

void uploadGatewayStatus()
{
  // Original arg order: id, type, t, h, p, r, sa, sd, d, rssiVal, aiClass
  uploadToServer(gatewayID, "receiver", 0.0, 0.0, 0.0, 0.0, 0, false, false, WiFi.RSSI(), 0);
}

/* SETUP */
void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  if (!connectToWiFi(currentSSID, currentPass)) {
      Serial.println("Failed to connect to primary WiFi.");
  }

  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("HazardNode Gateway (Simplified) Ready");
  Serial.print("Gateway ID: ");
  Serial.println(gatewayID);
}

void loop()
{
  if (newDataAvailable)
  {
    uploadData();
    newDataAvailable = false;
  }

  // Periodic Gateway Status Update every 60 seconds
  static unsigned long lastGatewayUpdate = 0;
  if (millis() - lastGatewayUpdate > 60000)
  {
    lastGatewayUpdate = millis();
    uploadGatewayStatus();
  }

  // Periodic WiFi check every 30 seconds
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000)
  {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Reconnecting WiFi...");
      connectToWiFi(currentSSID, currentPass);
    }
  }
}
