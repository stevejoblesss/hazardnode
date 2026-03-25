#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>

/* ===== NODE CONFIG ===== */
#define NODE_ID 1

// Safe angle calibration (where the node is mounted/resting)
#define SAFE_PITCH -67.0
#define SAFE_ROLL 0.0
#define TILT_THRESHOLD 30.0 // Degrees of deviation before triggering alert

// Update this to match your receiver's MAC address!
// You can find it by looking at the Serial monitor of the receiver.
uint8_t gatewayMAC[] = {0x88, 0x13, 0xBF, 0x24, 0x4F, 0x68};

// Set this to true to broadcast to ALL nearby receivers (for testing)
bool useBroadcast = false;
uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/* ===== PIN CONFIG ===== */
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ2_AO 34

// SPI Pins for SSD1309 (if used)
#define OLED_SCK 18
#define OLED_MOSI 23
#define OLED_RES 17
#define OLED_DC 16
#define OLED_CS 15

/* ===== OBJECTS ===== */
DHT dht(DHTPIN, DHTTYPE);
Adafruit_MPU6050 mpu;

// UNCOMMENT ONLY ONE:
// 1. Existing I2C SH1106
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// 2. New SPI SSD1309
U8G2_SSD1309_128X64_NONAME0_F_4W_SW_SPI u8g2(U8G2_R0, OLED_SCK, OLED_MOSI, OLED_CS, OLED_DC, OLED_RES);

/* ===== DATA STRUCT ===== */
typedef struct __attribute__((packed)) struct_message
{
  int nodeID;
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int rssi;
} struct_message;

struct_message msg;
int32_t scannedRSSI = 0;

/* ===== STATUS ===== */
bool sendSuccess = false;
int packetCount = 0;

/* ===== ESP-NOW CALLBACK ===== */
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);

  if (sendSuccess)
    packetCount++;
}

/* ===== OLED DISPLAY ===== */
void drawOLED()
{

  u8g2.clearBuffer();

  if (msg.danger)
  {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 15, "!!! ALERT !!!");

    u8g2.setFont(u8g2_font_ncenB12_tr);
    float pitchDev = abs(msg.pitch);
    float rollDev = abs(msg.roll);

    if (pitchDev > TILT_THRESHOLD || rollDev > TILT_THRESHOLD)
    {
      u8g2.drawStr(0, 40, "COLLAPSE!");
    }
    else if (msg.smokeAnalog > 2500 || msg.temp > 60)
    {
      u8g2.drawStr(0, 40, "FIRE/SMOKE");
    }
    else
    {
      u8g2.drawStr(0, 40, "HAZARD!");
    }

    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.setCursor(0, 60);
    u8g2.print("Node ID: ");
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
    u8g2.print(msg.temp, 1);

    u8g2.setCursor(60, 22);
    u8g2.print("H:");
    u8g2.print(msg.hum, 0);

    u8g2.setCursor(0, 34);
    u8g2.print("Pitch:");
    u8g2.print(msg.pitch, 0);

    u8g2.setCursor(60, 34);
    u8g2.print("Roll:");
    u8g2.print(msg.roll, 0);

    u8g2.setCursor(0, 46);
    u8g2.print("Smoke:");
    u8g2.print(msg.smokeDigital ? "YES" : "NO");

    u8g2.setCursor(0, 58);
    u8g2.print("Pkt:");
    u8g2.print(packetCount);

    u8g2.setCursor(65, 58);
    u8g2.print("R:");
    u8g2.print(scannedRSSI);
  }

  u8g2.sendBuffer();
}

/* ===== WIFI SCAN ===== */
#include <esp_wifi.h>

int32_t getWiFiChannel(const char *ssid)
{
  Serial.print("Scanning for WiFi channel of SSID: ");
  Serial.println(ssid);

  int32_t n = WiFi.scanNetworks();
  if (n > 0)
  {
    for (uint8_t i = 0; i < n; i++)
    {
      if (!strcmp(ssid, WiFi.SSID(i).c_str()))
      {
        int32_t ch = WiFi.channel(i);
        scannedRSSI = WiFi.RSSI(i);
        Serial.print("Found channel: ");
        Serial.print(ch);
        Serial.print(" | Signal (RSSI): ");
        Serial.print(scannedRSSI);
        Serial.println(" dBm");
        return ch;
      }
    }
  }

  Serial.println("SSID not found. Defaulting to channel 1.");
  return 1;
}

/* ===== SETUP ===== */
void setup()
{

  Serial.begin(115200);

  // Calibration for MQ2 Sensor (Warm-up period)
  Serial.println("MQ2 Sensor Warming Up (5 seconds)...");
  delay(5000); 

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

  int32_t channel = getWiFiChannel("Unicorn2012");
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAIL");
    return;
  }

  esp_now_register_send_cb(OnDataSent);

  esp_now_peer_info_t peerInfo = {};

  if (useBroadcast)
  {
    memcpy(peerInfo.peer_addr, broadcastMAC, 6);
  }
  else
  {
    memcpy(peerInfo.peer_addr, gatewayMAC, 6);
  }

  peerInfo.channel = channel;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK)
  {
    Serial.println("Peer Add Fail");
  }

  Serial.println("HazardNode Sender Ready");
  if (useBroadcast)
    Serial.println("BROADCAST MODE ON");
  else
  {
    Serial.print("Target MAC: ");
    for (int i = 0; i < 6; i++)
    {
      Serial.printf("%02X", gatewayMAC[i]);
      if (i < 5)
        Serial.print(":");
    }
    Serial.println();
  }
}

/* ===== LOOP ===== */
void loop()
{

  /* ===== READ DHT ===== */
  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();

  if (!isnan(newTemp))
    msg.temp = newTemp;

  if (!isnan(newHum))
    msg.hum = newHum;

  /* ===== READ MPU ===== */
  sensors_event_t a, g, t;

  mpu.getEvent(&a, &g, &t);

  msg.pitch = (atan2(
                   a.acceleration.y,
                   sqrt(a.acceleration.x * a.acceleration.x +
                        a.acceleration.z * a.acceleration.z)) *
               57.3) -
              SAFE_PITCH;

  msg.roll = (atan2(
                  -a.acceleration.x,
                  a.acceleration.z) *
              57.3) -
             SAFE_ROLL;

  /* ===== READ MQ2 ===== */

  long total = 0;

  for (int i = 0; i < 20; i++) // Increased samples for smoother reading
  {
    total += analogRead(MQ2_AO);
    delay(5); // Slightly more delay between samples
  }

  msg.smokeAnalog = total / 20;

  // Threshold check - MQ2 reading on ESP32 can be high if it's 12-bit (0-4095)
  // Adjust threshold if it's too sensitive for your environment
  msg.smokeDigital = (msg.smokeAnalog > 2500); 

  /* ===== DANGER LOGIC ===== */

  float pitchDev = abs(msg.pitch);
  float rollDev = abs(msg.roll);

  msg.danger =
      (msg.temp > 60) ||
      (pitchDev > TILT_THRESHOLD) ||
      (rollDev > TILT_THRESHOLD) ||
      (msg.smokeAnalog > 2500);

  msg.nodeID = NODE_ID;
  msg.rssi = scannedRSSI;

  /* ===== SEND ===== */

  esp_err_t result =
      esp_now_send(useBroadcast ? broadcastMAC : gatewayMAC,
                   (uint8_t *)&msg,
                   sizeof(msg));

  if (result == ESP_OK)
    Serial.println("Send queued");
  else
    Serial.println("Send error");

  // Periodic WiFi scan every 30 seconds to update RSSI
  static unsigned long lastScanTime = 0;
  if (millis() - lastScanTime > 30000)
  {
    lastScanTime = millis();
    getWiFiChannel("Unicorn2012");
  }

  /* ===== SERIAL DEBUG ===== */

  Serial.println("====================");

  Serial.print("Node ID: ");
  Serial.println(msg.nodeID);

  Serial.print("Temp: ");
  Serial.print(msg.temp);
  Serial.println(" C");

  Serial.print("Humidity: ");
  Serial.print(msg.hum);
  Serial.println(" %");

  Serial.print("Pitch (Relative): ");
  Serial.println(msg.pitch);

  Serial.print("Roll (Relative): ");
  Serial.println(msg.roll);

  Serial.print("Smoke Digital: ");
  Serial.println(msg.smokeDigital ? "DETECTED" : "CLEAR");

  Serial.print("Smoke Analog: ");
  Serial.println(msg.smokeAnalog);

  Serial.print("Danger: ");
  Serial.println(msg.danger ? "YES" : "NO");

  Serial.print("Node RSSI: ");
  Serial.print(msg.rssi);
  Serial.println(" dBm");

  Serial.print("Packet Count: ");
  Serial.println(packetCount);

  Serial.print("Last Send: ");
  Serial.println(sendSuccess ? "SUCCESS" : "FAILED");

  if (msg.danger)
    Serial.println(">>> ALERT TRIGGERED <<<");

  drawOLED();

  delay(1000);
}