#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
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
const char *ssid = "Unicorn2012";
const char *pass = "Finance@5408";

/* ===== Cloud URL ===== */
String serverURL = "https://hazardnode.vercel.app/api/node";

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

/* ===== Get Mac Address ===== */
void readMacAddress()
{
  uint8_t baseMac[6];
  esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, baseMac);
  if (ret == ESP_OK)
  {
    Serial.printf("%02x:%02x:%02x:%02x:%02x:%02x\n",
                  baseMac[0], baseMac[1], baseMac[2],
                  baseMac[3], baseMac[4], baseMac[5]);
  }
  else
  {
    Serial.println("Failed to read MAC address");
  }
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

  Serial.print("[DEFAULT] ESP32 Board MAC Address: ");
  readMacAddress();
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
