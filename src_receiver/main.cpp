#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/* WIFI CONFIG */
char currentSSID[32] = "Unicorn2012";   // Your Home Network
char currentPass[32] = "Finance@5408";

const char *backupSSID = "steve";       // Your Hotspot
const char *backupPass = "123456789";

/* GATEWAY CONFIG */
char gatewayID[32] = "Gateway 1";
const char *backupGatewayID = "Gateway 1";

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

  if (type != "receiver") {
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

    int httpResponseCode = http.POST(json);

    if (httpResponseCode > 0)
    {
      Serial.print("HTTP Response code: ");
      Serial.println(httpResponseCode);
      
      String response = http.getString();
      Serial.print("[FETCH] Server Response: ");
      Serial.println(response);

      if (httpResponseCode == 200 || httpResponseCode == 201)
      {
        if (response.length() == 0) {
            Serial.println("[FETCH] Warning: Response body is empty.");
        } else {
            JsonDocument responseDoc;
            DeserializationError error = deserializeJson(responseDoc, response);
            
            if (error) {
                Serial.print("[FETCH] JSON Parse Error: ");
                Serial.println(error.c_str());
            } else {
                // 1. Check for WiFi config update
                if (responseDoc["newSSID"].is<const char*>() && responseDoc["newPass"].is<const char*>()) {
                    const char* nSSID = responseDoc["newSSID"];
                    const char* nPass = responseDoc["newPass"];
                    Serial.printf("[FETCH] New WiFi Config detected: %s\n", nSSID);
                    
                    if (connectToWiFi(nSSID, nPass, 15)) {
                        preferences.begin("gateway-config", false);
                        preferences.putString("ssid", nSSID);
                        preferences.putString("pass", nPass);
                        preferences.end();
                        strncpy(currentSSID, nSSID, 31);
                        strncpy(currentPass, nPass, 31);
                        Serial.println("[FETCH] WiFi Update Successful and Saved.");
                    } else {
                        Serial.println("[FETCH] WiFi Update Failed (Connection failed). Reverting.");
                        connectToWiFi(currentSSID, currentPass);
                    }
                }
                
                // 2. Check for Node ID update
                if (responseDoc["targetNode"].is<const char*>() && responseDoc["newNodeID"].is<const char*>()) {
                    String targetNode = responseDoc["targetNode"].as<String>();
                    String newNodeID = responseDoc["newNodeID"].as<String>();
                    targetNode.trim();
                    newNodeID.trim();
                    
                    Serial.printf("[FETCH] ID Update found for '%s' -> '%s'\n", targetNode.c_str(), newNodeID.c_str());
                    
                    if (targetNode.equalsIgnoreCase(String(gatewayID))) {
                        Serial.println("[FETCH] Updating Gateway ID...");
                        preferences.begin("gateway-config", false);
                        preferences.putString("nodeID", newNodeID);
                        preferences.end();
                        strncpy(gatewayID, newNodeID.c_str(), 31);
                        Serial.print("[FETCH] Gateway renamed to: ");
                        Serial.println(gatewayID);
                    } else {
                        Serial.printf("[FETCH] Relaying ID update to Sender '%s'\n", targetNode.c_str());
                        strncpy(cmd.commandType, "UPDATE_ID", 15);
                        strncpy(cmd.targetID, targetNode.c_str(), 31);
                        strncpy(cmd.newValue, newNodeID.c_str(), 31);
                        
                        uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
                        esp_now_send(broadcastAddress, (uint8_t *)&cmd, sizeof(cmd));
                    }
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

  // Load stored Gateway ID from flash
  preferences.begin("gateway-config", true);
  String storedID = preferences.getString("nodeID", backupGatewayID);
  
  // Also check if we have a remotely updated SSID
  String storedSSID = preferences.getString("ssid", "");
  String storedPass = preferences.getString("pass", "");
  preferences.end();

  strncpy(gatewayID, storedID.c_str(), 31);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  bool connected = false;

  // PRIORITY 1: Try the Home SSID (The one you hardcoded in the code)
  Serial.printf("Trying Home WiFi: %s\n", currentSSID);
  if (connectToWiFi(currentSSID, currentPass)) {
      connected = true;
  }

  // PRIORITY 2: Try any remotely updated SSID (If you changed it on the website)
  if (!connected && storedSSID != "" && storedSSID != currentSSID) {
      Serial.printf("Trying Remote WiFi: %s\n", storedSSID.c_str());
      if (connectToWiFi(storedSSID.c_str(), storedPass.c_str())) {
          strncpy(currentSSID, storedSSID.c_str(), 31);
          strncpy(currentPass, storedPass.c_str(), 31);
          connected = true;
      }
  }

  // PRIORITY 3: Try the Hotspot (Backup)
  if (!connected) {
      Serial.printf("Trying Hotspot: %s\n", backupSSID);
      if (connectToWiFi(backupSSID, backupPass)) {
          strncpy(currentSSID, backupSSID, 31);
          strncpy(currentPass, backupPass, 31);
          connected = true;
      }
  }

  if (!connected) {
      Serial.println("FATAL: No WiFi available.");
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