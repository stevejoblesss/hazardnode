#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

/* ===== NODE CONFIG ===== */
#define NODE_ID 1 // CHANGE PER NODE

uint8_t gatewayMAC[] = {0xB0, 0xCB, 0xD8, 0xC6, 0xBC, 0xD0};

/* ===== PIN CONFIG ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ2_DO 5
#define MQ2_AO 34

/* ===== OBJECTS ===== */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

/* ===== DATA STRUCT ===== */
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

struct_message msg;

/* ===== VARIABLES ===== */
float temp = 0, hum = 0, pitch = 0, roll = 0;
int smokeAnalog = 0;
bool smokeDigital = false;

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
    u8g2.drawStr(0, 15, "ALERT !");
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
    u8g2.print("T:");
    u8g2.print(temp, 1);

    u8g2.setCursor(60, 22);
    u8g2.print("H:");
    u8g2.print(hum, 0);

    u8g2.setCursor(0, 34);
    u8g2.print("Tilt:");
    u8g2.print(pitch, 0);

    u8g2.setCursor(0, 46);
    u8g2.print("Smoke:");
    u8g2.print(smokeDigital ? "YES" : "NO");

    u8g2.setCursor(0, 58);
    u8g2.print("Pkt:");
    u8g2.print(packetCount);

    u8g2.setCursor(70, 58);
    u8g2.print(sendSuccess ? "OK" : "FAIL");
  }

  u8g2.sendBuffer();
}

void setup()
{
  Serial.begin(115200);

  Wire.begin(21, 22);
  u8g2.begin();
  dht.begin();
  pinMode(MQ2_DO, INPUT);

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

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer Add Fail");
  }

  Serial.println("HazardNode Sender Ready");
}

void loop()
{

  /* ===== READ DHT ===== */
  float newHum = dht.readHumidity();
  float newTemp = dht.readTemperature();

  if (!isnan(newTemp))
    temp = newTemp;
  if (!isnan(newHum))
    hum = newHum;

  /* ===== READ MPU ===== */
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);

  pitch = atan2(a.acceleration.y,
                sqrt(a.acceleration.x * a.acceleration.x +
                     a.acceleration.z * a.acceleration.z)) *
          57.3;

  roll = atan2(-a.acceleration.x,
               a.acceleration.z) *
         57.3;

  /* ===== READ MQ-2 ===== */
  smokeDigital = (digitalRead(MQ2_DO) == LOW);

  long total = 0;
  for (int i = 0; i < 8; i++)
  {
    total += analogRead(MQ2_AO);
    delay(3);
  }
  smokeAnalog = total / 8;

  /* ===== PACK DATA ===== */
  msg.nodeID = NODE_ID;
  msg.temp = temp;
  msg.hum = hum;
  msg.pitch = pitch;
  msg.roll = roll;
  msg.smokeAnalog = smokeAnalog;
  msg.smokeDigital = smokeDigital;

  /* ===== SMART DANGER LOGIC ===== */
  msg.danger = (temp > 60 ||
                abs(pitch) > 45 ||
                smokeDigital ||
                smokeAnalog > 2500 // tune after calibration
  );

  /* ===== SEND ===== */
  esp_now_send(gatewayMAC, (uint8_t *)&msg, sizeof(msg));

  Serial.println("==================");
  Serial.print("Node ID");
  Serial.println(NODE_ID);

  Serial.print("Temp: ");
  Serial.print(temp);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(hum);
  Serial.println(' %');

  Serial.print("Pitch: ");
  Serial.println(pitch);

  Serial.print("Roll: ");
  Serial.println(roll);

  Serial.print("Smoke Digital: ");
  Serial.println(smokeDigital ? "DETECTED" : "CLEAR");

  Serial.print("Smoke Analog (0-4095): ");
  Serial.println(smokeAnalog);

  Serial.print("Danger Status: ");
  Serial.println(msg.danger ? "YES" : "NO");

  if (msg.danger)
  {
    Serial.println(">>> ALERT TRIGGERED <<<");
  }

  Serial.print("Packet Count: ");
  Serial.println(packetCount);

  Serial.print("Last Send Status: ");
  Serial.println(sendSuccess ? "SUCCESS" : "FAILED");

  drawOLED();

  delay(1000);
}
