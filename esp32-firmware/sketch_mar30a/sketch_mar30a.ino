#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* serverUrl = "http://YOUR_COMPUTER_IP:3001/api/heartrate"; 

// OLED config
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MAX30102 sensor
MAX30105 particleSensor;

// Heart rate variables
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
int systolicBP = 0;
int diastolicBP = 0;

// Buffer for averaging
#define RATE_SIZE 4
byte rates[RATE_SIZE];
byte rateSpot = 0;
byte validRates = 0;

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);  // ESP32 SDA=21, SCL=22

  // OLED init
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed");
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.println("Initializing...");
  display.display();

  // add this in setup(), before sensor init
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
  // Sensor init
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Sensor not found!");
    display.display();
    while (1);
  }

  // Recommended sensor settings
  byte ledBrightness = 60;   // 0-255
  byte sampleAverage = 4;
  byte ledMode = 2;          // Red + IR
  int sampleRate = 100;      // 100 samples/sec
  int pulseWidth = 411;      // 69, 118, 215, 411
  int adcRange = 4096;       // 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);

  // Important: turn on IR LED
  particleSensor.setPulseAmplitudeIR(0x1F);
  particleSensor.setPulseAmplitudeRed(0x1F);
  particleSensor.setPulseAmplitudeGreen(0);

  // Clear rate buffer
  memset(rates, 0, sizeof(rates));

  Serial.println("Setup complete");
}

void estimateBP(long irValue, int bpm) {
  if (irValue < 20000 || bpm == 0) {
    systolicBP = 0;
    diastolicBP = 0;
    return;
  }

  float irNorm = constrain((irValue - 20000.0) / 80000.0, 0.0, 1.0);

  systolicBP  = 110 + (bpm - 70) * 0.4 + irNorm * 10;
  diastolicBP = 70  + (bpm - 70) * 0.2 + irNorm * 5;
}

void sendToServer(int bpm, int avgBpm, long irValue, int systolic, int diastolic, bool fingerDetected) {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(500);

  String payload = "{";
  payload += "\"bpm\":" + String(bpm) + ",";
  payload += "\"avgBpm\":" + String(avgBpm) + ",";
  payload += "\"ir\":" + String(irValue) + ",";
  payload += "\"systolic\":" + String(systolic) + ",";   // add this
  payload += "\"diastolic\":" + String(diastolic) + ",";
  payload += "\"fingerDetected\":" + String(fingerDetected ? "true" : "false") + ",";
  payload += "\"timestamp\":" + String(millis());
  payload += "}";
  
  Serial.println(payload);
  http.POST(payload);
  http.end();
}


void loop() {
  static unsigned long lastSend = 0;
  long irValue = particleSensor.getIR();

  // Finger detection
  if (irValue < 20000) {
    beatsPerMinute = 0;
    beatAvg = 0;
    validRates = 0;
    rateSpot = 0;
    lastBeat = 0;
    memset(rates, 0, sizeof(rates));

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Place finger");
    display.println("on sensor");
    display.setCursor(0, 40);
    display.print("IR: ");
    display.println(irValue);
    display.display();

    Serial.print("No finger detected. IR=");
    Serial.println(irValue);
    if (millis() - lastSend >= 2000) {  // add this throttle
    lastSend = millis();
    sendToServer(0, 0, irValue, 0, 0, false);
  }
    return;
  }

  // Detect heartbeat
  if (checkForBeat(irValue)) {
    if (lastBeat == 0) {
      lastBeat = millis();   // ignore first beat
      return;
    }

    long delta = millis() - lastBeat;
    lastBeat = millis();

    beatsPerMinute = 60.0 / (delta / 1000.0);

    if (beatsPerMinute > 20 && beatsPerMinute < 255) {
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;

      if (validRates < RATE_SIZE) validRates++;

      int sum = 0;
      for (byte i = 0; i < validRates; i++) {
        sum += rates[i];
      }
      beatAvg = sum / validRates;
    }
  }

  // Serial monitor
  Serial.print("IR=");
  Serial.print(irValue);
  Serial.print(", BPM=");
  Serial.print(beatsPerMinute);
  Serial.print(", AVG BPM=");
  Serial.println(beatAvg);

  // OLED display
  // OLED display
  display.clearDisplay();

  // BPM — large, top left
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("HR: ");
  display.setTextSize(2);
  display.setCursor(24, 0);
  if (beatAvg > 0) {
    display.print(beatAvg);
    display.print(" bpm");
  } else {
    display.print("-- bpm");
  }

  display.drawLine(0, 20, 127, 20, WHITE);

  // BP section
  display.setTextSize(1);
  display.setCursor(0, 26);
  display.print("Blood Pressure:");

  display.setTextSize(2);
  display.setCursor(0, 38);
  if (systolicBP > 0 && diastolicBP > 0) {
    display.print(systolicBP);
    display.print("/");
    display.print(diastolicBP);
  } else {
    display.print("--/--");
  }

  // mmHg label
  display.setTextSize(1);
  display.setCursor(100, 48);
  display.print("mmHg");

  display.drawLine(0, 57, 127, 57, WHITE);

  // IR and sample rate — bottom strip
  display.setTextSize(1);
  display.setCursor(0, 58);
  display.print("IR:");
  display.print(irValue / 1000);  // shortened to save space e.g. "82k"
  display.print("k");

  display.setCursor(72, 58);
  display.print("S:");
  display.print(validRates);
  display.print("/");
  display.print(RATE_SIZE);

  display.display();

  estimateBP(irValue, beatAvg);

  if (millis() - lastSend >= 2000) {
  lastSend = millis();
  sendToServer((int)beatsPerMinute, beatAvg, irValue, systolicBP, diastolicBP, true);
  }
}
