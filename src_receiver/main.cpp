#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>

/* GATEWAY CONFIG */
char gatewayID[32] = "Gateway 1";

/* API ENDPOINT */
const char *serverURL = "https://hazardnode-dashboard.vercel.app/api/node";
const char *provisionURL = "https://hazardnode-dashboard.vercel.app/api/provisioning";

/* STRUCT */
typedef struct __attribute__((packed)) struct_message
{
  char nodeID[32];
  char macAddress[18];
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
// Size: 32 + 18 + 4*4 + 4 + 1 + 1 + 4 = 76 bytes
const int PACKET_SIZE = 76;
uint8_t rxBuffer[PACKET_SIZE];
volatile bool newDataAvailable = false;

/* RECEIVE CALLBACK */
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
{
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
#endif
  if (len != PACKET_SIZE)
  {
    Serial.printf("Invalid packet length: received %d, expected %d\n", len, PACKET_SIZE);
    return;
  }
  memcpy(rxBuffer, incomingData, PACKET_SIZE);
  newDataAvailable = true;
}

void provisionDevice() {
  if (WiFi.status() != WL_CONNECTED) return;

  Serial.println("Provisioning device...");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  
  http.begin(client, provisionURL);
  http.addHeader("Content-Type", "application/json");

  String payload = "{\"mac_address\": \"" + WiFi.macAddress() + "\", \"type\": \"receiver\"}";
  int httpCode = http.POST(payload);

  if (httpCode == 200) {
    String response = http.getString();
    Serial.println("Synced config: " + response);
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (!error && doc["name"].is<const char*>()) {
      strncpy(gatewayID, doc["name"], 31);
      Serial.printf("Gateway renamed to: %s\n", gatewayID);
    }
  } else {
    Serial.printf("Provisioning failed, code: %d\n", httpCode);
  }
  http.end();
}

void uploadToServer(String id, String type, float t, float h, float p, float r, int sa, bool sd, bool d, int rssiVal, int aiClass, String mac)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Disconnected, attempting to reconnect...");
    return;
  }

  JsonDocument doc;
  doc["nodeID"] = id;
  doc["type"] = type;
  doc["mac_address"] = mac;

  if (type == "receiver") {
    doc["rssi"] = rssiVal;
  } else {
    doc["temp"] = round(t * 100) / 100.0;
    doc["hum"] = round(h * 100) / 100.0;
    doc["pitch"] = round(p * 100) / 100.0;
    doc["roll"] = round(r * 100) / 100.0;
    doc["smokeAnalog"] = sa;
    doc["smokeDigital"] = sd;
    doc["danger"] = d;
    doc["edgeAIClass"] = aiClass;
  }

  String json;
  serializeJson(doc, json);

  if (type == "receiver")
    Serial.printf("[GATEWAY] ID: %s | WiFi RSSI: %d dBm\n", id.c_str(), rssiVal);
  else
    Serial.printf("[NODE] ID: %s | (No RSSI sent)\n", id.c_str());

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
  char rxNodeID[33];
  memcpy(rxNodeID, rxBuffer + 0, 32);
  rxNodeID[32] = '\0';

  char rxMac[19];
  memcpy(rxMac, rxBuffer + 32, 18);
  rxMac[18] = '\0';
  
  memcpy(&data.temp, rxBuffer + 50, 4);
  memcpy(&data.hum, rxBuffer + 54, 4);
  memcpy(&data.pitch, rxBuffer + 58, 4);
  memcpy(&data.roll, rxBuffer + 62, 4);
  memcpy(&data.smokeAnalog, rxBuffer + 66, 4);
  data.smokeDigital = (rxBuffer[70] != 0);
  data.danger = (rxBuffer[71] != 0);
  memcpy(&data.edgeAIClass, rxBuffer + 72, 4);

  uploadToServer(String(rxNodeID), "sender", data.temp, data.hum, data.pitch, data.roll, data.smokeAnalog, data.smokeDigital, data.danger, 0, data.edgeAIClass, String(rxMac));
}

void uploadGatewayStatus()
{
  uploadToServer(gatewayID, "receiver", 0.0, 0.0, 0.0, 0.0, 0, false, false, WiFi.RSSI(), 0, WiFi.macAddress());
}

/* SETUP */
void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  WiFiManager wm;
  // wm.resetSettings(); // Uncomment to wipe settings for testing
  
  Serial.println("Connecting to WiFi via WiFiManager...");
  if (!wm.autoConnect("HazardNode-Setup")) {
    Serial.println("Failed to connect or hit timeout");
    ESP.restart();
  }
  
  Serial.println("WiFi Connected!");
  
  // Provision the gateway to get its custom name
  provisionDevice();

  WiFi.setSleep(false);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("HazardNode Gateway (Provisioning Enabled) Ready");
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

  static unsigned long lastGatewayUpdate = 0;
  if (millis() - lastGatewayUpdate > 60000)
  {
    lastGatewayUpdate = millis();
    uploadGatewayStatus();
  }

  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000)
  {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("Reconnecting WiFi...");
      WiFi.begin(); // WiFiManager handles the rest
    }
  }
}
