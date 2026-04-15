#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>
#include "../model.h"

/* ===== NODE CONFIG ===== */
const char* nodeID = "Block 1";

// Safe angle calibration (where the node is mounted/resting)
#define SAFE_PITCH 0
#define SAFE_ROLL 0
#define TILT_THRESHOLD 30.0

// Update this to match your receiver's MAC address!
uint8_t gatewayMAC[] = {0x88, 0x13, 0xBF, 0x6C, 0x77, 0xF0};

/* ===== PIN CONFIG ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ2_AO 34

// SPI Pins for SSD1309
#define OLED_SCK 18
#define OLED_MOSI 23
#define OLED_RES 17
#define OLED_DC 16
#define OLED_CS 15

/* ===== OBJECTS ===== */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI u8g2(U8G2_R0, OLED_SCK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RES);

/* ===== DATA STRUCT ===== */
typedef struct __attribute__((packed)) struct_message
{
  char nodeID[32];
  char macAddress[18];
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int edgeAIClass; // 0=NORMAL, 1=WARNING, 2=HAZARD
} struct_message;

struct_message msg;

/* ===== STATUS ===== */
bool sendSuccess = false;
int packetCount = 0;

/* ===== ESP-NOW CALLBACKS ===== */
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);
  if (sendSuccess) packetCount++;
}

/* ===== OLED DISPLAY ===== */
void drawOLED()
{
  u8g2.clearBuffer();

  if (msg.edgeAIClass == 2) // HAZARD
  {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "!!! HAZARD !!!");

    u8g2.setFont(u8g2_font_ncenB12_tr);
    float pitchDev = abs(msg.pitch);
    float rollDev = abs(msg.roll);

    if (pitchDev > TILT_THRESHOLD || rollDev > TILT_THRESHOLD)
      u8g2.drawStr(0, 40, "COLLAPSE!");
    else if (msg.smokeAnalog > 2500 || msg.temp > 60)
      u8g2.drawStr(0, 40, "FIRE/SMOKE");
    else
      u8g2.drawStr(0, 40, "DANGER!");

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 60);
    u8g2.print("ID: ");
    u8g2.print(nodeID);
  }
  else if (msg.edgeAIClass == 1) // WARNING
  {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "--- WARNING ---");
    
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 35);
    u8g2.print("Status: Caution");
    u8g2.setCursor(0, 50);
    u8g2.print("ID: ");
    u8g2.print(nodeID);
  }
  else // NORMAL
  {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 10);
    u8g2.print("Node:"); u8g2.print(nodeID);
    u8g2.setCursor(0, 22);
    u8g2.print("T:"); u8g2.print(msg.temp, 1);
    u8g2.setCursor(60, 22);
    u8g2.print("H:"); u8g2.print(msg.hum, 0);
    u8g2.setCursor(0, 34);
    u8g2.print("Pitch:"); u8g2.print(msg.pitch, 0);
    u8g2.setCursor(60, 34);
    u8g2.print("Roll:"); u8g2.print(msg.roll, 0);
    u8g2.setCursor(0, 46);
    u8g2.print("Smoke:"); u8g2.print(msg.smokeDigital ? "YES" : "NO");
    u8g2.setCursor(0, 58);
    u8g2.print("Pkt:"); u8g2.print(packetCount);
  }

  u8g2.sendBuffer();
}

/* ===== SETUP ===== */
void setup()
{
  Serial.begin(115200);

  Serial.println("MQ2 Sensor Warming Up (5 seconds)...");
  delay(5000); 

  Wire.begin(21, 22);
  
  Serial.println("Initializing OLED...");
  if (u8g2.begin()) {
      Serial.println("OLED Init Success");
      u8g2.setContrast(255);
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_ncenB08_tr);
      u8g2.drawStr(0, 20, "HazardNode Init...");
      u8g2.sendBuffer();
  } else {
      Serial.println("OLED Init Failed");
  }

  dht.begin();

  if (!mpu.begin())
  {
    Serial.println("MPU FAIL");
    while (1);
  }

  WiFi.mode(WIFI_STA);
  String mac = WiFi.macAddress();
  strncpy(msg.macAddress, mac.c_str(), 17);
  msg.macAddress[17] = '\0';
  
  Serial.print("Sender MAC: ");
  Serial.println(msg.macAddress);

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAIL");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer Add Fail");
  }

  Serial.println("HazardNode Sender (Provisioning Enabled) Ready");
  Serial.print("Node ID: ");
  Serial.println(nodeID);
}

/* ===== LOOP ===== */
void loop()
{
  /* ===== READ DHT ===== */
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();
  if (!isnan(newTemp)) msg.temp = newTemp;
  if (!isnan(newHum)) msg.hum = newHum;

  /* ===== READ MPU ===== */
  sensors_event_t a, g, t;
  float sumX = 0, sumY = 0, sumZ = 0;
  const int mpuSamples = 10;

  for (int i = 0; i < mpuSamples; i++) {
    mpu.getEvent(&a, &g, &t);
    sumX += a.acceleration.x;
    sumY += a.acceleration.y;
    sumZ += a.acceleration.z;
    delay(5);
  }

  float avgX = sumX / mpuSamples;
  float avgY = sumY / mpuSamples;
  float avgZ = sumZ / mpuSamples;

  msg.pitch = (atan2(avgY, sqrt(avgX * avgX + avgZ * avgZ)) * 57.3) - SAFE_PITCH;
  msg.roll = (atan2(-avgX, avgZ) * 57.3) - SAFE_ROLL;

  /* ===== READ MQ2 ===== */
  long total = 0;
  for (int i = 0; i < 20; i++)
  {
    total += analogRead(MQ2_AO);
    delay(5);
  }
  msg.smokeAnalog = total / 20;
  msg.smokeDigital = (msg.smokeAnalog > 2500); 

  /* ===== EDGE AI INFERENCE ===== */
  msg.edgeAIClass = predict(msg.temp, msg.hum, msg.pitch, msg.roll, (float)msg.smokeAnalog);
  msg.danger = (msg.edgeAIClass == 2);

  strncpy(msg.nodeID, nodeID, sizeof(msg.nodeID) - 1);
  msg.nodeID[sizeof(msg.nodeID) - 1] = '\0';

  /* ===== SEND ===== */
  esp_err_t result = esp_now_send(gatewayMAC, (uint8_t *)&msg, sizeof(msg));

  /* ===== SERIAL DEBUG ===== */
  Serial.println("====================");
  Serial.printf("Node ID: %s | MAC: %s | AI Class: %d\n", 
                msg.nodeID, msg.macAddress, msg.edgeAIClass);
  Serial.printf("Temp: %.1f C | Hum: %.1f %%\n", msg.temp, msg.hum);
  Serial.printf("Pitch: %.1f | Roll: %.1f\n", msg.pitch, msg.roll);
  Serial.printf("Smoke: %d (%s)\n", msg.smokeAnalog, msg.smokeDigital ? "DETECTED" : "CLEAR");

  drawOLED();
  delay(1000);
}
