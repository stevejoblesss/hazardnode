#include <Wire.h>
#include <WiFi.h>
#include <esp_now.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>

/* ===== NODE CONFIG ===== */
#define NODE_ID 1 // CHANGE PER NODE

/* ===== DHT ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

/* ===== MPU ===== */
Adafruit_MPU6050 mpu;

/* ===== Gateway MAC ===== */
uint8_t gatewayMAC[] = {0xB0, 0xCB, 0xD8, 0xC6, 0xBC, 0xD0};

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

NodePacket packet;

void setup()
{

  Serial.begin(115200);
  Wire.begin(21, 22);

  dht.begin();

  if (!mpu.begin())
  {
    Serial.println("MPU FAIL");
    while (true)
      ;
  }

  /* ESP NOW */
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, gatewayMAC, 6);
  peer.channel = 0;
  peer.encrypt = false;

  esp_now_add_peer(&peer);
}

void loop()
{

  /* Read Sensors */
  float hum = dht.readHumidity();
  float temp = dht.readTemperature();

  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  float pitch = atan2(a.acceleration.y,
                      sqrt(a.acceleration.x * a.acceleration.x +
                           a.acceleration.z * a.acceleration.z)) *
                57.3;

  float roll = atan2(-a.acceleration.x,
                     a.acceleration.z) *
               57.3;

  /* Pack Data */
  packet.nodeID = NODE_ID;
  packet.temp = temp;
  packet.hum = hum;
  packet.pitch = pitch;
  packet.roll = roll;
  packet.danger = (temp > 60 || abs(pitch) > 45);
  packet.timestamp = millis();

  /* Send */
  esp_now_send(gatewayMAC, (uint8_t *)&packet, sizeof(packet));

  Serial.printf("Node %d Sent\n", NODE_ID);

  delay(2000);
}
