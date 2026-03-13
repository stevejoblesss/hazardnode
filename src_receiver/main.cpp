#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>

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
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
} struct_message;

struct_message data;

/* RECEIVE CALLBACK */
void OnDataRecv(const uint8_t *mac,
                const uint8_t *incomingData,
                int len)
{

  memcpy(&data, incomingData, sizeof(data));

  Serial.println("Packet received");

  String json = "{";
  json += "\"nodeID\":" + String(data.nodeID) + ",";
  json += "\"temp\":" + String(data.temp, 2) + ",";
  json += "\"hum\":" + String(data.hum, 2) + ",";
  json += "\"pitch\":" + String(data.pitch, 2) + ",";
  json += "\"roll\":" + String(data.roll, 2) + ",";
  json += "\"smokeAnalog\":" + String(data.smokeAnalog) + ",";
  json += "\"smokeDigital\":" + String(data.smokeDigital ? "true" : "false") + ",";
  json += "\"danger\":" + String(data.danger ? "true" : "false");
  json += "}";

  Serial.println(json);

  if (WiFi.status() == WL_CONNECTED)
  {

    HTTPClient http;

    http.begin(serverURL);
    http.addHeader("Content-Type", "application/json");

    int httpResponseCode = http.POST(json);

    Serial.print("HTTP Response: ");
    Serial.println(httpResponseCode);

    http.end();
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

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Gateway Ready");
}

void loop() {}