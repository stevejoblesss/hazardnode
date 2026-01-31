#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

/* ================= STRUCT ================= */
typedef struct
{
  float temp;
  float hum;
  float pitch;
  float roll;
  bool danger;
} SensorPacket;

SensorPacket incoming;

/* ================= CALLBACK ================= */
// ✅ Arduino‑ESP32 (IDF v4 signature)
void onReceive(const uint8_t *mac, const uint8_t *data, int len)
{
  memcpy(&incoming, data, sizeof(incoming));

  Serial.println("📥 Packet received:");
  Serial.printf("Temp   : %.1f C\n", incoming.temp);
  Serial.printf("Hum    : %.1f %%\n", incoming.hum);
  Serial.printf("Pitch  : %.1f deg\n", incoming.pitch);
  Serial.printf("Roll   : %.1f deg\n", incoming.roll);
  Serial.printf("Danger : %s\n", incoming.danger ? "YES" : "NO");
  Serial.println("------------------------");
}

/* ================= SETUP ================= */
void setup()
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("❌ ESP-NOW INIT FAILED");
    while (true)
      ;
  }

  esp_now_register_recv_cb(onReceive);

  Serial.println("📡 Gateway ready");
}

/* ================= LOOP ================= */
void loop()
{
  // Nothing needed here
}
