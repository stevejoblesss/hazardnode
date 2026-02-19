#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>

/* ===== WIFI CREDENTIALS ===== */
const char *ssid = "Unicorn2012";
const char *password = "Finance@5408";

/* ===== YOUR BACKEND ENDPOINT ===== */
const char *serverURL = "https://hazardnode.vercel.app/";

/* ===== DATA STRUCT (MUST MATCH SENDER EXACTLY) ===== */
typedef struct struct_message
{
  int nodeID;
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
} struct_message;

struct_message incomingData;

/* ===== RECEIVE CALLBACK ===== */
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataBytes, int len)
{

  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));

  Serial.println("Packet received from node:");
  Serial.println(incomingData.nodeID);

  // Convert to JSON
  String json = "{";
  json += "\"node\":" + String(incomingData.nodeID) + ",";
  json += "\"temp\":" + String(incomingData.temp) + ",";
  json += "\"hum\":" + String(incomingData.hum) + ",";
  json += "\"pitch\":" + String(incomingData.pitch) + ",";
  json += "\"roll\":" + String(incomingData.roll) + ",";
  json += "\"smokeAnalog\":" + String(incomingData.smokeAnalog) + ",";
  json += "\"smokeDigital\":" + String(incomingData.smokeDigital ? "true" : "false") + ",";
  json += "\"danger\":" + String(incomingData.danger ? "true" : "false");
  json += "}";

  Serial.println("Sending JSON:");
  Serial.println(json);

  // Send to server
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(json);

    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);

    http.end();
  }
  else
  {
    Serial.println("WiFi not connected!");
  }

  Serial.println("--------------------------------");
}

/* ===== SETUP ===== */
void setup()
{
  Serial.begin(115200);  

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC Address: ");
  Serial.println(WiFi.macAddress());
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW Init Failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Gateway Ready. Waiting for ESP-NOW data...");
}

/* ===== LOOP ===== */
void loop()
{
  // Nothing needed
}
