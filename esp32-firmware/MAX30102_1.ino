#include <Wire.h>
#include "MAX30105.h" // Works for MAX30102
#include "heartRate.h" // The built-in beat detection algorithm

MAX30105 particleSensor;

// Variables for calculating average BPM
const byte RATE_SIZE = 4; // How many readings to average (increase to smooth out data)
byte rates[RATE_SIZE]; // Array to hold the heart rates
byte rateSpot = 0;
long lastBeat = 0; // Time at which the last beat occurred

float beatsPerMinute;
int beatAvg;

void setup() {
  Serial.begin(115200);
  Serial.println("Initializing MAX30102 for BPM...");

  // Initialize sensor on default I2C pins (SDA=21, SCL=22)
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 was not found. Please check wiring/power.");
    while (1); 
  }

  Serial.println("Place your index finger on the sensor.");

  particleSensor.setup(); // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x24); // stronger signal
  particleSensor.setPulseAmplitudeGreen(0); // Turn off Green LED
}

void loop() {
  long irValue = particleSensor.getIR(); // Read the Infrared value

  // checkForBeat() is a function from heartRate.h that returns true if a beat is detected
  if (checkForBeat(irValue) == true) {
    // We sensed a beat!
    long delta = millis() - lastBeat; // Time between the last beat and this one
    lastBeat = millis();

    // Calculate BPM
    beatsPerMinute = 60 / (delta / 1000.0);

    // Filter out crazy values (normal resting heart rate is usually 60-100)
    if (beatsPerMinute < 255 && beatsPerMinute > 20) {
      rates[rateSpot++] = (byte)beatsPerMinute; // Store this reading in the array
      rateSpot %= RATE_SIZE; // Wrap the variable

      // Take the average of the stored readings to smooth the output
      beatAvg = 0;
      for (byte x = 0 ; x < RATE_SIZE ; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_SIZE;
    }
  }

  // Print the results to the Serial Monitor
  Serial.print("IR Data=");
  Serial.print(irValue);
  Serial.print(" | BPM=");
  Serial.print(beatsPerMinute);
  Serial.print(" | Avg BPM=");
  Serial.print(beatAvg);

  if (irValue < 50000) {
    Serial.print("  <- No finger detected");
  }

  Serial.println();
}