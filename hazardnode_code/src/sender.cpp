#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>

uint8_t gatewayMAC[] = {0xB0,0xCB,0xD8,0xC6,0xBC,0xD0}; 

typedef struct struct_message {
  float temp;
  float hum;
  float pitch;
  float roll;
  bool danger;
} struct_message;

struct_message msg;

/* ===== DHT ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

/* ===== MPU6050 ===== */
Adafruit_MPU6050 mpu;

/* ===== OLED (SH1106 I2C) ===== */
U8G2_SH1106_128X64_NONAME_F_HW_I2C 
u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

/* ===== Variables ===== */
float temp, hum;
float pitch, roll;

void onSend(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.print("Send status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(21, 22);

  // ===== WiFi / ESP-NOW =====
  WiFi.mode(WIFI_STA);
  // WiFi.setChannel(1);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW INIT FAILED");
    while (true);
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 1;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("FAILED TO ADD PEER");
    while (true);
  }

  esp_now_register_send_cb(onSend);

  Serial.println("ESP-NOW READY");


  // OLED
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 10, "Booting...");
  u8g2.sendBuffer();

  // DHT
  dht.begin();

  // MPU6050
  if (!mpu.begin()) {
    Serial.println("MPU6050 NOT FOUND");
    while (1);
  }

  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  Serial.println("All sensors OK");
}

void loop() {
  /* ==== Read DHT ==== */
  hum = dht.readHumidity();
  temp = dht.readTemperature();

  /* ==== Read MPU ==== */
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  // Calculate orientation (simple, good enough)
  pitch = atan2(a.acceleration.y, 
                sqrt(a.acceleration.x * a.acceleration.x +
                     a.acceleration.z * a.acceleration.z)) * 57.3;

  roll  = atan2(-a.acceleration.x, 
                a.acceleration.z) * 57.3;

  // ===== Pack data =====
  msg.temp  = temp;
  msg.hum   = hum;
  msg.pitch = pitch;
  msg.roll  = roll;

  // danger logic
  static int dangerCount = 0;

  bool tempDanger = temp > 50;
  bool tiltDanger = abs(pitch) > 35;

  if (tempDanger || tiltDanger)
  {
    dangerCount++;
  }
  else
  {
    dangerCount = 0;
  }

  msg.danger = dangerCount >= 5; // must be dangerous for 5 cycles

  // ===== Send packet =====
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t *)&msg, sizeof(msg));

  if (result == ESP_OK) {
    Serial.println("Packet sent");
  } else {
    Serial.println("Send failed");
  }

  /* ==== OLED Display ==== */
  u8g2.clearBuffer();

  u8g2.setCursor(0, 12);
  u8g2.print("Temp: ");
  u8g2.print(temp, 1);
  u8g2.print(" C");

  u8g2.setCursor(0, 24);
  u8g2.print("Hum : ");
  u8g2.print(hum, 1);
  u8g2.print(" %");

  u8g2.setCursor(0, 38);
  u8g2.print("Pitch: ");
  u8g2.print(pitch, 1);

  u8g2.setCursor(0, 50);
  u8g2.print("Roll : ");
  u8g2.print(roll, 1);

  u8g2.sendBuffer();

  delay(500);
}
