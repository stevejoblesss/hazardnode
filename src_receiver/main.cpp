#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>

/* WIFI */
char currentSSID[32] = "steve";
char currentPass[32] = "123456789";

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
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int rssi;
} struct_message;

struct_message data;
uint8_t rxBuffer[58]; // Size of the packed struct (32 + 4 + 4 + 4 + 4 + 4 + 1 + 1 + 4 = 58)
const int PACKET_SIZE = 58;
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

void uploadToServer(String id, float t, float h, float p, float r, int sa, bool sd, bool d, int rssiVal)
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
  doc["temp"] = round(t * 100) / 100.0;
  doc["hum"] = round(h * 100) / 100.0;
  doc["pitch"] = round(p * 100) / 100.0;
  doc["roll"] = round(r * 100) / 100.0;
  doc["smokeAnalog"] = sa;
  doc["smokeDigital"] = sd;
  doc["danger"] = d;
  doc["rssi"] = rssiVal;

  String json;
  serializeJson(doc, json);

  Serial.println("Uploading to Vercel...");
  if (id == "0" || id == "Gateway")
    Serial.print("Gateway Status Update | ");
  else
  {
    Serial.print("Node ");
    Serial.print(id);
    Serial.print(" Update | ");
  }
  Serial.print("RSSI: ");
  Serial.print(rssiVal);
  Serial.println(" dBm");
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
        // Check for new WiFi configuration in the response
        // Expected JSON: {"newSSID": "...", "newPass": "..."}
        JsonDocument responseDoc;
        DeserializationError error = deserializeJson(responseDoc, response);
        
        if (!error && responseDoc.containsKey("newSSID") && responseDoc.containsKey("newPass")) {
            const char* newSSID = responseDoc["newSSID"];
            const char* newPass = responseDoc["newPass"];
            
            if (strcmp(newSSID, currentSSID) != 0) {
                Serial.printf("Received new WiFi config: %s\n", newSSID);
                
                // Attempt to connect to the new WiFi
                if (connectToWiFi(newSSID, newPass, 15)) {
                    // Success! Save to preferences
                    preferences.begin("wifi-config", false);
                    preferences.putString("ssid", newSSID);
                    preferences.putString("pass", newPass);
                    preferences.end();
                    
                    strncpy(currentSSID, newSSID, 31);
                    strncpy(currentPass, newPass, 31);
                    Serial.println("New WiFi config saved.");
                } else {
                    Serial.println("Failed to connect to new WiFi. Reverting to old config.");
                    connectToWiFi(currentSSID, currentPass);
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
  memcpy(&data.temp, rxBuffer + 32, 4);
  memcpy(&data.hum, rxBuffer + 36, 4);
  memcpy(&data.pitch, rxBuffer + 40, 4);
  memcpy(&data.roll, rxBuffer + 44, 4);
  memcpy(&data.smokeAnalog, rxBuffer + 48, 4);
  data.smokeDigital = (rxBuffer[52] != 0);
  data.danger = (rxBuffer[53] != 0);
  memcpy(&data.rssi, rxBuffer + 54, 4);

  uploadToServer(String(data.nodeID), data.temp, data.hum, data.pitch, data.roll, data.smokeAnalog, data.smokeDigital, data.danger, data.rssi);
}

void uploadGatewayStatus()
{
  // Gateway uses Node ID "Gateway" to identify itself on the dashboard
  uploadToServer("Gateway", 0.0, 0.0, 0.0, 0.0, 0, false, false, WiFi.RSSI());
}

/* SETUP */
void setup()
{
  Serial.begin(115200);

  // Load stored WiFi credentials
  preferences.begin("wifi-config", true);
  String storedSSID = preferences.getString("ssid", backupSSID);
  String storedPass = preferences.getString("pass", backupPass);
  preferences.end();

  strncpy(currentSSID, storedSSID.c_str(), 31);
  strncpy(currentPass, storedPass.c_str(), 31);

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
      WiFi.begin(ssid, password);
    }
  }
}