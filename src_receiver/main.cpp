#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/* WIFI */
const char *ssid = "Unicorn2012";
const char *password = "Finance@5408";

/* API ENDPOINT */
const char *serverURL =
    "https://hazardnode-dashboard.vercel.app/api/node";

/* STRUCT */
typedef struct struct_message
{
  int nodeID;
  uint32_t seq;
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
volatile bool newDataAvailable = false;
uint32_t lastSeq = 0;
int lastRSSI = 0;

/* RECEIVE CALLBACK */
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
{
  int currentRSSI = info->rx_ctrl->rssi;
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
  int currentRSSI = 0; // Not easily available in older versions
#endif

  if (len != sizeof(data))
  {
    Serial.println("Invalid packet length");
    return;
  }

  struct_message tempMsg;
  memcpy(&tempMsg, incomingData, sizeof(tempMsg));

  if (tempMsg.seq == lastSeq)
  {
    Serial.println("Duplicate packet ignored");
    return;
  }

  data = tempMsg;
  lastSeq = data.seq;
  lastRSSI = currentRSSI;
  newDataAvailable = true;

  Serial.print("Packet received from Node: ");
  Serial.print(data.nodeID);
  Serial.print(" | Seq: ");
  Serial.print(data.seq);
  Serial.print(" | RSSI: ");
  Serial.print(currentRSSI);
  Serial.println(" dBm");
}

void uploadData()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("WiFi Disconnected, attempting to reconnect...");
    WiFi.begin(ssid, password);
    return;
  }

  String json = "{";
  json += "\"nodeID\":" + String(data.nodeID) + ",";
  json += "\"temp\":" + String(data.temp, 2) + ",";
  json += "\"hum\":" + String(data.hum, 2) + ",";
  json += "\"pitch\":" + String(data.pitch, 2) + ",";
  json += "\"roll\":" + String(data.roll, 2) + ",";
  json += "\"smokeAnalog\":" + String(data.smokeAnalog) + ",";
  json += "\"smokeDigital\":" + String(data.smokeDigital ? "true" : "false") + ",";
  json += "\"danger\":" + String(data.danger ? "true" : "false") + ",";
  json += "\"rssi\":" + String(lastRSSI);
  json += "}";

  Serial.println("Uploading to Vercel...");
  Serial.print("WiFi Signal Strength (RSSI): ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  Serial.println(json);

  WiFiClientSecure client;
  client.setInsecure(); // Required for Vercel HTTPS without certificate management

  HTTPClient http;

  // Set timeout to 5 seconds
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
      if (httpResponseCode == 200 || httpResponseCode == 201)
      {
        Serial.println("Upload Success!");
      }
      else
      {
        String response = http.getString();
        Serial.println("Response body: " + response);
      }
    }
    else
    {
      Serial.print("HTTP Error code: ");
      Serial.println(httpResponseCode);
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
      
      if (httpResponseCode == -1) {
          Serial.println("TIP: 'Connection Refused' usually means the ESP32 can't reach the server.");
          Serial.println("Try pinging 'hazardnode-dashboard.vercel.app' from your PC to ensure the site is up.");
      }
    }
    http.end();
  }
  else
  {
    Serial.println("[HTTP] Unable to connect to server");
  }
}

/* SETUP */
void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC: ");
  Serial.println(WiFi.macAddress());

  WiFi.begin(ssid, password);

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("\nWiFi Connection Failed (Timeout)");
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