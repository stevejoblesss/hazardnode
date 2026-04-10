#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/* WIFI */
char currentSSID[32] = "steve";
char currentPass[32] = "123456789";

/* GATEWAY CONFIG */
char gatewayID[32] = "Gateway 1";
const char *backupGatewayID = "Gateway 1";

/* BACKUP WIFI */
const char *backupSSID = "steve";
const char *backupPass = "123456789";

Preferences preferences;

/* API ENDPOINT */
const char *serverURL =
    "https://hazardnode-dashboard.vercel.app/api/node";

/* STRUCT */
typedef struct struct_message
{
  char nodeID[32];
  char type[16];   // "sender" or "receiver"
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int rssi;
  int edgeAIClass; // 0=NORMAL, 1=WARNING, 2=HAZARD
} struct_message;

// Command struct for relaying updates to Senders
typedef struct struct_command
{
  char commandType[16]; // "UPDATE_ID"
  char targetID[32];    // The current ID of the sender
  char newValue[32];    // The new ID or value
} struct_command;

struct_message data;
struct_command cmd;
uint8_t rxBuffer[78]; // New size: 32 + 16 + 4*4 + 4 + 1 + 1 + 4 + 4 = 78 bytes
const int PACKET_SIZE = 78;
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
    Serial.print("Invalid packet length: received ");
    Serial.print(len);
    Serial.print(", expected ");
    Serial.println(PACKET_SIZE);
    return;
  }

  memcpy(rxBuffer, incomingData, PACKET_SIZE);
  newDataAvailable = true;
}

bool connectToWiFi(const char *ssid, const char *pass, int timeout_s = 10)
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
    return;
  }

  int currentWiFiRSSI = WiFi.RSSI();

  JsonDocument doc;
  doc["nodeID"] = id;
  doc["type"] = type;
  doc["rssi"] = rssiVal;

  // Only add sensor data if it's a sender node
  if (type != "receiver")
  {
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

  Serial.println("Uploading to Vercel...");
  if (type == "receiver")
  {
    Serial.print("[GATEWAY] ID: ");
    Serial.print(id);
    Serial.print(" | WiFi Signal: ");
    Serial.print(rssiVal);
    Serial.println(" dBm");
  }
  else
  {
    Serial.print("[NODE] ID: ");
    Serial.print(id);
    Serial.print(" | Signal: ");
    Serial.print(rssiVal);
    Serial.println(" dBm");
  }
  Serial.println(json);

  WiFiClientSecure client;
  client.setInsecure(); // Required for Vercel HTTPS without certificate management

  HTTPClient http;
  http.setTimeout(5000);

  if (http.begin(client, serverURL))
  {
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Host", "hazardnode-dashboard.vercel.app");

    int httpResponseCode = http.POST(json);

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      
      String response = http.getString();
      Serial.println("Response: " + response);

      if (httpResponseCode == 200 || httpResponseCode == 201)
      {
        // Check for new WiFi configuration or Node ID updates
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (!error) {
            // 1. Check for WiFi config update
            if (responseDoc["newSSID"].is<const char*>() && responseDoc["newPass"].is<const char*>()) {
                const char* newSSID = responseDoc["newSSID"];
                const char* newPass = responseDoc["newPass"];
                
                if (strcmp(newSSID, currentSSID) != 0) {
                    Serial.printf("Received new WiFi config: %s\n", newSSID);
                    if (connectToWiFi(newSSID, newPass, 15)) {
                        preferences.begin("gateway-config", false);
                        preferences.putString("ssid", newSSID);
                        preferences.putString("pass", newPass);
                        preferences.end();
                        strncpy(currentSSID, newSSID, 31);
                        strncpy(currentPass, newPass, 31);
                        Serial.println("New WiFi config saved.");
                    } else {
                        Serial.println("Failed to connect to new WiFi. Reverting.");
                        connectToWiFi(currentSSID, currentPass);
                    }
                }
            }
            
            // 2. Check for Node ID update
            if (responseDoc["targetNode"].is<const char*>() && responseDoc["newNodeID"].is<const char*>()) {
                const char* targetNode = responseDoc["targetNode"];
                const char* newNodeID = responseDoc["newNodeID"];
                
                if (strcmp(targetNode, gatewayID) == 0) {
                    // Update the Gateway itself
                    Serial.printf("Updating Gateway ID to: %s\n", newNodeID);
                    preferences.begin("gateway-config", false);
                    preferences.putString("nodeID", newNodeID);
                    preferences.end();
                    strncpy(gatewayID, newNodeID, 31);
                } else {
                    // Relay to Sender nodes via ESP-NOW broadcast
                    Serial.printf("Relaying ID update to Sender '%s' -> '%s'\n", targetNode, newNodeID);
                    strncpy(cmd.commandType, "UPDATE_ID", 15);
                    strncpy(cmd.targetID, targetNode, 31);
                    strncpy(cmd.newValue, newNodeID, 31);
                    
                    uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                    esp_now_send(broadcastAddress, (uint8_t *)&cmd, sizeof(cmd));
                }
            }
        }
      }
    }
    else
    {
      Serial.print("HTTP Error code: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}

void uploadData()
{
  // Manually unpack the packed buffer into our aligned struct.
  // This prevents 'LoadProhibited' crashes on the ESP32.
  memcpy(data.nodeID, rxBuffer + 0, 32);
  memcpy(data.type, rxBuffer + 32, 16);
  memcpy(&data.temp, rxBuffer + 48, 4);
  memcpy(&data.hum, rxBuffer + 52, 4);
  memcpy(&data.pitch, rxBuffer + 56, 4);
  memcpy(&data.roll, rxBuffer + 60, 4);
  memcpy(&data.smokeAnalog, rxBuffer + 64, 4);
  data.smokeDigital = (rxBuffer[68] != 0);
  data.danger = (rxBuffer[69] != 0);
  memcpy(&data.rssi, rxBuffer + 70, 4);
  memcpy(&data.edgeAIClass, rxBuffer + 74, 4);

  uploadToServer(String(data.nodeID), String(data.type), data.temp, data.hum, data.pitch, data.roll, data.smokeAnalog, data.smokeDigital, data.danger, data.rssi, data.edgeAIClass);
}

void uploadGatewayStatus()
{
  // Gateway identifies itself as type "receiver" and nodeID gatewayID
  uploadToServer(gatewayID, "receiver", 0.0, 0.0, 0.0, 0.0, 0, false, false, WiFi.RSSI(), 0);
}

/* SETUP */
void setup()
{
  Serial.begin(115200);

  // Load stored WiFi credentials and Gateway ID
  preferences.begin("gateway-config", true);
  String storedSSID = preferences.getString("ssid", backupSSID);
  String storedPass = preferences.getString("pass", backupPass);
  String storedID = preferences.getString("nodeID", backupGatewayID);
  preferences.end();

  strncpy(currentSSID, storedSSID.c_str(), 31);
  strncpy(currentPass, storedPass.c_str(), 31);
  strncpy(gatewayID, storedID.c_str(), 31);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  // Try stored WiFi, then fallback to hardcoded backup if it fails
  if (!connectToWiFi(currentSSID, currentPass))
  {
    Serial.println("Stored WiFi failed. Trying backup...");
    if (connectToWiFi(backupSSID, backupPass))
    {
      strncpy(currentSSID, backupSSID, 31);
      strncpy(currentPass, backupPass, 31);
    }
  }

  WiFi.setSleep(false); // Disable WiFi power save for ESP-NOW

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  // Register broadcast peer for command relay
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 0; // Use current channel
  peerInfo.encrypt = false;
  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Failed to add broadcast peer");
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Gateway Ready");
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
    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.print("WiFi RSSI: ");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm");
    }
    else
    {
      Serial.println("Reconnecting WiFi...");
      WiFi.disconnect();
      connectToWiFi(currentSSID, currentPass);
    }
  }
}