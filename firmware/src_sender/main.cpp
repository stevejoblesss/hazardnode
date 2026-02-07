#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>

/* ===== NODE CONFIG ===== */
#define NODE_ID 1 // <<< CHANGE FOR EACH NODE

uint8_t gatewayMAC[] = {0xB0, 0xCB, 0xD8, 0xC6, 0xBC, 0xD0};

/* ===== DHT ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

/* ===== MPU ===== */
Adafruit_MPU6050 mpu;

/* ===== OLED ===== */
U8G2_SH1106_128X64_NONAME_F_HW_I2C
u8g2(U8G2_R0, U8X8_PIN_NONE);

/* ===== DATA STRUCT ===== */
typedef struct struct_message
{
  int nodeID;
  float temp;
  float hum;
  float pitch;
  float roll;
  bool danger;
} struct_message;

struct_message msg;

/* ===== VARIABLES ===== */
float temp, hum, pitch, roll;
bool sendSuccess = false;
int packetCount = 0;

/* ===== ESP NOW CALLBACK ===== */
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);

  if (sendSuccess)
    packetCount++;
}

/* ===== OLED UI ===== */
void drawOLED()
{
  u8g2.clearBuffer();

  if (msg.danger)
  {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "!!! ALERT !!!");
    u8g2.drawStr(0, 35, "Hazard Detected");
    u8g2.setCursor(0, 55);
    u8g2.print("Node:");
    u8g2.print(NODE_ID);
  }
  else
  {
    u8g2.setFont(u8g2_font_6x10_tf);

    u8g2.setCursor(0, 10);
    u8g2.print("Node:");
    u8g2.print(NODE_ID);

    u8g2.setCursor(0, 22);
    u8g2.print("Temp:");
    u8g2.print(temp, 1);

    u8g2.setCursor(0, 34);
    u8g2.print("Hum:");
    u8g2.print(hum, 1);

    u8g2.setCursor(0, 46);
    u8g2.print("Tilt:");
    u8g2.print(pitch, 1);

    u8g2.setCursor(0, 58);
    u8g2.print("Pkt:");
    u8g2.print(packetCount);

    u8g2.setCursor(70, 58);
    if (sendSuccess)
      u8g2.print("OK");
    else
      u8g2.print("FAIL");
  }

  u8g2.sendBuffer();
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);
  u8g2.begin();

  dht.begin();

  if (!mpu.begin())
  {
    Serial.println("MPU FAIL");
    while (1)
      ;
  }

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP NOW FAIL");
    while (1)
      ;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  esp_now_add_peer(&peerInfo);
}

void loop()
{
  /* ===== READ SENSORS ===== */
  hum = dht.readHumidity();
  temp = dht.readTemperature();

  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  pitch = atan2(a.acceleration.y,
                sqrt(a.acceleration.x * a.acceleration.x +
                     a.acceleration.z * a.acceleration.z)) *
          57.3;

  roll = atan2(-a.acceleration.x,
               a.acceleration.z) *
         57.3;

  /* ===== PACK DATA ===== */
  msg.nodeID = NODE_ID;
  msg.temp = temp;
  msg.hum = hum;
  msg.pitch = pitch;
  msg.roll = roll;

  msg.danger = (temp > 60 || abs(pitch) > 45);

  /* ===== SEND ===== */
  esp_now_send(gatewayMAC, (uint8_t *)&msg, sizeof(msg));

  drawOLED();

  delay(1000);
}
