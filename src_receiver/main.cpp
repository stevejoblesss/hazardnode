#include <WiFi.h>
#include <esp_now.h>

/* ===== DATA STRUCT (MUST MATCH SENDER) ===== */
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

int totalPackets = 0;

/* ===== RECEIVE CALLBACK ===== */
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingDataBytes, int len)
{

  memcpy(&incomingData, incomingDataBytes, sizeof(incomingData));
  totalPackets++;

  Serial.println("=================================");
  Serial.print("Packet #: ");
  Serial.println(totalPackets);

  Serial.print("From Node: ");
  Serial.println(incomingData.nodeID);

  Serial.print("Temp: ");
  Serial.println(incomingData.temp);

  Serial.print("Humidity: ");
  Serial.println(incomingData.hum);

  Serial.print("Pitch: ");
  Serial.println(incomingData.pitch);

  Serial.print("Roll: ");
  Serial.println(incomingData.roll);

  Serial.print("Smoke Analog: ");
  Serial.println(incomingData.smokeAnalog);

  Serial.print("Smoke Digital: ");
  Serial.println(incomingData.smokeDigital ? "DETECTED" : "CLEAR");

  Serial.print("Danger: ");
  Serial.println(incomingData.danger ? "YES" : "NO");

  /* ===== JSON OUTPUT (FOR CLOUD / DASHBOARD) ===== */
  Serial.println("--- JSON FORMAT ---");

  Serial.print("{");
  Serial.print("\"node\":");
  Serial.print(incomingData.nodeID);
  Serial.print(",");
  Serial.print("\"temp\":");
  Serial.print(incomingData.temp);
  Serial.print(",");
  Serial.print("\"hum\":");
  Serial.print(incomingData.hum);
  Serial.print(",");
  Serial.print("\"pitch\":");
  Serial.print(incomingData.pitch);
  Serial.print(",");
  Serial.print("\"roll\":");
  Serial.print(incomingData.roll);
  Serial.print(",");
  Serial.print("\"smokeAnalog\":");
  Serial.print(incomingData.smokeAnalog);
  Serial.print(",");
  Serial.print("\"smokeDigital\":");
  Serial.print(incomingData.smokeDigital ? "true" : "false");
  Serial.print(",");
  Serial.print("\"danger\":");
  Serial.print(incomingData.danger ? "true" : "false");
  Serial.println("}");
}

/* ===== SETUP ===== */
void setup()
{
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  Serial.print("Gateway MAC Address: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAILED");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  Serial.println("Gateway Ready. Waiting for nodes...");
}

/* ===== LOOP ===== */
void loop()
{
  // Nothing needed here.
}
