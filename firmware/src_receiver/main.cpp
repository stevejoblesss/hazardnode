#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>

/* ===== Packet ===== */
typedef struct
{
  int nodeID;
  float temp;
  float hum;
  float pitch;
  float roll;
  bool danger;
  unsigned long timestamp;
} NodePacket;

NodePacket incoming;

/* ===== Registry ===== */
struct NodeStatus
{
  bool active;
  unsigned long lastSeen;
};

NodeStatus registry[50]; // Supports 50 nodes

/* ===== WiFi ===== */
const char *ssid = "YOUR_WIFI";
const char *pass = "YOUR_PASS";

/* ===== Cloud URL ===== */
String serverURL = "https://your-vercel-url/api/node";

void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{

  memcpy(&incoming, data, sizeof(incoming));

  int id = incoming.nodeID;

  registry[id].active = true;
  registry[id].lastSeen = millis();

  Serial.println("----- PACKET -----");
  Serial.printf("Node: %d\n", id);
  Serial.printf("Temp: %.2f\n", incoming.temp);
  Serial.printf("Hum: %.2f\n", incoming.hum);
  Serial.printf("Pitch: %.2f\n", incoming.pitch);
  Serial.printf("Roll: %.2f\n", incoming.roll);
  Serial.printf("Danger: %d\n", incoming.danger);

  sendToCloud(incoming);
}

/* ===== Cloud Forward ===== */
void sendToCloud(NodePacket pkt)
{

  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");

  String json = "{";
  json += "\"nodeID\":" + String(pkt.nodeID) + ",";
  json += "\"temp\":" + String(pkt.temp) + ",";
  json += "\"hum\":" + String(pkt.hum) + ",";
  json += "\"pitch\":" + String(pkt.pitch) + ",";
  json += "\"roll\":" + String(pkt.roll) + ",";
  json += "\"danger\":" + String(pkt.danger);
  json += "}";

  http.POST(json);
  http.end();
}

void setup()
{

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("WiFi Connected");

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  esp_now_register_recv_cb(onReceive);
}

void loop()
{

  /* Check node timeout */
  for (int i = 0; i < 50; i++)
  {
    if (registry[i].active &&
        millis() - registry[i].lastSeen > 15000)
    {

      registry[i].active = false;
      Serial.printf("Node %d LOST\n", i);
    }
  }

  delay(3000);
}
