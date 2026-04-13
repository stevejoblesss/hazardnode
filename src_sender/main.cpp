#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <WiFi.h>
#include <esp_now.h>
#include <math.h>
#include <Preferences.h>

/* ===== NODE CONFIG ===== */
char nodeID[32] = "Block 3";
const char *backupNodeID = "Block 3";

Preferences preferences;

// Safe angle calibration (where the node is mounted/resting)
#define SAFE_PITCH -66.51 //-67 node1, -84 node2, -66.51 node3
#define SAFE_ROLL -103.51 //0 node1, -175 node2, - 103.51 node3
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
  char nodeID[32];
  char type[16];   // "sender" or "receiver"
  float temp;
  float hum;
  float pitch;
  float roll;
  int smokeAnalog;
  bool smokeDigital;
  bool danger;
  int rssi;
  int edgeAIClass; // 0=NORMAL, 1=WARNING, 2=HAZARD
} struct_message;

// Command struct for receiving updates from Gateway
typedef struct struct_command
{
  char commandType[16]; // "UPDATE_ID"
  char targetID[32];    // The current ID of the sender
  char newValue[32];    // The new ID or value
} struct_command;

struct_message msg;
struct_command cmd;
int32_t scannedRSSI = 0;

/* ===== STATUS ===== */
bool sendSuccess = false;
int packetCount = 0;

/* ===== ESP-NOW CALLBACKS ===== */
void OnDataSent(const uint8_t *mac, esp_now_send_status_t status)
{
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");

  sendSuccess = (status == ESP_NOW_SEND_SUCCESS);

  if (sendSuccess)
    packetCount++;
}

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len)
{
#else
void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
#endif
  if (len == sizeof(struct_command))
  {
    memcpy(&cmd, incomingData, sizeof(cmd));
    if (strcmp(cmd.commandType, "UPDATE_ID") == 0)
    {
      if (strcmp(cmd.targetID, nodeID) == 0)
      {
        Serial.printf("Updating Node ID from '%s' to '%s'\n", nodeID, cmd.newValue);
        preferences.begin("node-config", false);
        preferences.putString("nodeID", cmd.newValue);
        preferences.end();
        strncpy(nodeID, cmd.newValue, 31);
      }
    }
  }
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
    u8g2.print(nodeID);
  }
  else
  {
    u8g2.setFont(u8g2_font_6x10_tf);

    u8g2.setCursor(0, 10);
    u8g2.print("Node:");
    u8g2.print(nodeID);

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

int32_t getWiFiChannel(const char *ssid1, const char *ssid2)
{
  Serial.println("Scanning for WiFi channels...");

  int32_t n = WiFi.scanNetworks();
  if (n > 0)
  {
    // Try to find the first SSID
    for (uint8_t i = 0; i < n; i++)
    {
      if (!strcmp(ssid1, WiFi.SSID(i).c_str()))
      {
        int32_t ch = WiFi.channel(i);
        scannedRSSI = WiFi.RSSI(i);
        Serial.printf("Found Primary SSID '%s' on channel: %d\n", ssid1, ch);
        return ch;
      }
    }
    // If not found, try to find the second SSID
    for (uint8_t i = 0; i < n; i++)
    {
      if (!strcmp(ssid2, WiFi.SSID(i).c_str()))
      {
        int32_t ch = WiFi.channel(i);
        scannedRSSI = WiFi.RSSI(i);
        Serial.printf("Found Backup SSID '%s' on channel: %d\n", ssid2, ch);
        return ch;
      }
    }
  }

  Serial.println("No known SSID found. Defaulting to channel 1.");
  return 1;
}

/* ===== SETUP ===== */
void setup()
{

  Serial.begin(115200);

  // Load stored Node ID
  preferences.begin("node-config", true);
  String storedID = preferences.getString("nodeID", backupNodeID);
  preferences.end();
  strncpy(nodeID, storedID.c_str(), 31);

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

  // Scan for the Gateway on either Home or Hotspot
  int32_t channel = getWiFiChannel("Unicorn2012", "steve"); 
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());

  if (esp_now_init() != ESP_OK)
  {
    Serial.println("ESP-NOW INIT FAIL");
    return;
  }

  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataRecv); // Register receive callback for updates

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
  Serial.print("Current Node ID: ");
  Serial.println(nodeID);
  
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
      (msg.smokeAnalog > 3000);

  // Edge AI Classification: 0=NORMAL, 1=WARNING, 2=HAZARD
  if (msg.danger) {
      msg.edgeAIClass = 2; // Hazard
  } else if (msg.temp > 45 || msg.smokeAnalog > 1500 || pitchDev > (TILT_THRESHOLD/2) || rollDev > (TILT_THRESHOLD/2)) {
      msg.edgeAIClass = 1; // Warning
  } else {
      msg.edgeAIClass = 0; // Normal
  }

  strncpy(msg.nodeID, nodeID, sizeof(msg.nodeID) - 1);
  msg.nodeID[sizeof(msg.nodeID) - 1] = '\0';
  
  strncpy(msg.type, "sender", sizeof(msg.type) - 1);
  msg.type[sizeof(msg.type) - 1] = '\0';

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