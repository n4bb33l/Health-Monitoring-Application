/*
 * PHASE 2: Hardware Stabilization - Sensor Data Acquisition
 * 
 * Purpose: Stable, filtered sensor readings with NO BLE transmission
 * Focus: Clean data acquisition, filtering, and validation
 * 
 * Hardware:
 * - Arduino Nano 33 BLE
 * - MAX30102 Heart Rate/SpO2 Sensor (I2C)
 * - Temperature Sensor (analog or I2C)
 * 
 * Output: Serial Monitor with clean, stable readings
 */

#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

// ============================================
// CONFIGURATION
// ============================================

// Sampling rates (milliseconds)
#define HR_SAMPLE_INTERVAL    20    // 50Hz for heart rate
#define TEMP_SAMPLE_INTERVAL  1000  // 1Hz for temperature
#define PRINT_INTERVAL        2000  // Print every 2 seconds

// Filter settings
#define HR_BUFFER_SIZE        10    // Moving average window
#define TEMP_BUFFER_SIZE      5     // Temperature smoothing

// Validation ranges (child-safe)
#define MIN_VALID_BPM         40
#define MAX_VALID_BPM         200
#define MIN_VALID_SPO2        70
#define MAX_VALID_SPO2        100
#define MIN_VALID_TEMP        30.0
#define MAX_VALID_TEMP        42.0

// Temperature sensor pin (if using analog sensor like TMP36)
#define TEMP_SENSOR_PIN       A0

// ============================================
// GLOBAL OBJECTS
// ============================================

MAX30105 particleSensor;

// ============================================
// DATA STRUCTURES
// ============================================

// Circular buffer for heart rate samples
struct HeartRateBuffer {
  long irValues[HR_BUFFER_SIZE];
  int index;
  int count;
} hrBuffer = {0};

// Circular buffer for temperature samples
struct TempBuffer {
  float values[TEMP_BUFFER_SIZE];
  int index;
  int count;
} tempBuffer = {0};

// Current readings
struct SensorData {
  int heartRate;
  int spO2;
  float temperature;
  bool hrValid;
  bool tempValid;
  unsigned long lastHrSample;
  unsigned long lastTempSample;
  unsigned long lastPrint;
} sensorData = {0, 0, 0.0, false, false, 0, 0, 0};

// Peak detection for heart rate
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("=================================");
  Serial.println("PHASE 2: Hardware Stabilization");
  Serial.println("=================================");
  Serial.println();
  
  // Initialize I2C
  Wire.begin();
  
  // Initialize sensors
  if (!initSensors()) {
    Serial.println("ERROR: Sensor initialization failed!");
    Serial.println("Check wiring and restart.");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("Sensors initialized successfully");
  Serial.println("Starting data acquisition...");
  Serial.println();
  Serial.println("Time(ms)\tHR(BPM)\tSpO2(%)\tTemp(°C)\tStatus");
  Serial.println("---------------------------------------------------------------");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  unsigned long currentTime = millis();
  
  // Read heart rate sensor at high frequency
  if (currentTime - sensorData.lastHrSample >= HR_SAMPLE_INTERVAL) {
    sensorData.lastHrSample = currentTime;
    readHeartRateSensor();
  }
  
  // Read temperature sensor at lower frequency
  if (currentTime - sensorData.lastTempSample >= TEMP_SAMPLE_INTERVAL) {
    sensorData.lastTempSample = currentTime;
    readTemperatureSensor();
  }
  
  // Print filtered data periodically
  if (currentTime - sensorData.lastPrint >= PRINT_INTERVAL) {
    sensorData.lastPrint = currentTime;
    printSensorData(currentTime);
  }
}

// ============================================
// SENSOR INITIALIZATION
// ============================================

bool initSensors() {
  // Initialize MAX30102
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println("MAX30102 not found!");
    return false;
  }
  
  // Configure MAX30102
  byte ledBrightness = 0x1F;  // Low power: 0x1F, High power: 0xFF
  byte sampleAverage = 4;     // Average 4 samples
  byte ledMode = 2;           // Red + IR for HR and SpO2
  int sampleRate = 100;       // 100 samples/second
  int pulseWidth = 411;       // 411us pulse width
  int adcRange = 4096;        // ADC range 4096
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange);
  particleSensor.setPulseAmplitudeRed(0x0A);  // Low power for Red LED
  particleSensor.setPulseAmplitudeGreen(0);   // Turn off Green LED
  
  // Initialize temperature sensor (analog)
  pinMode(TEMP_SENSOR_PIN, INPUT);
  
  return true;
}

// ============================================
// HEART RATE SENSOR READING
// ============================================

void readHeartRateSensor() {
  // Read IR value (used for heart rate detection)
  long irValue = particleSensor.getIR();
  
  // Check if finger is detected (IR > threshold)
  if (irValue < 50000) {
    sensorData.hrValid = false;
    sensorData.heartRate = 0;
    sensorData.spO2 = 0;
    return;
  }
  
  // Add to buffer for filtering
  addToHrBuffer(irValue);
  
  // Get filtered IR value
  long filteredIR = getFilteredIR();
  
  // Detect heartbeat using filtered signal
  if (checkForBeat(filteredIR)) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    // Calculate BPM from time between beats
    int beatsPerMinute = 60000 / delta;
    
    // Validate BPM range
    if (beatsPerMinute >= MIN_VALID_BPM && beatsPerMinute <= MAX_VALID_BPM) {
      // Add to rate buffer for averaging
      rates[rateSpot++] = (byte)beatsPerMinute;
      rateSpot %= RATE_SIZE;
      
      // Calculate average BPM
      int beatAvg = 0;
      for (byte x = 0; x < RATE_SIZE; x++) {
        beatAvg += rates[x];
      }
      beatAvg /= RATE_SIZE;
      
      sensorData.heartRate = beatAvg;
      sensorData.hrValid = true;
    }
  }
  
  // Read SpO2 (simplified - real calculation is more complex)
  // For production, use SparkFun's spo2_algorithm library
  long redValue = particleSensor.getRed();
  if (irValue > 0 && redValue > 0) {
    float ratio = (float)redValue / (float)irValue;
    int spO2 = (int)(110 - 25 * ratio);  // Simplified formula
    
    if (spO2 >= MIN_VALID_SPO2 && spO2 <= MAX_VALID_SPO2) {
      sensorData.spO2 = spO2;
    }
  }
}

// ============================================
// TEMPERATURE SENSOR READING
// ============================================

void readTemperatureSensor() {
  // Read analog temperature sensor (TMP36)
  int reading = analogRead(TEMP_SENSOR_PIN);
  
  // Convert to voltage (Arduino Nano 33 BLE uses 3.3V)
  float voltage = reading * (3.3 / 1023.0);
  
  // Convert voltage to temperature (TMP36: 10mV/°C, 500mV offset)
  float tempC = (voltage - 0.5) * 100.0;
  
  // Validate range
  if (tempC >= MIN_VALID_TEMP && tempC <= MAX_VALID_TEMP) {
    addToTempBuffer(tempC);
    sensorData.temperature = getFilteredTemp();
    sensorData.tempValid = true;
  } else {
    sensorData.tempValid = false;
  }
}

// ============================================
// FILTERING FUNCTIONS
// ============================================

void addToHrBuffer(long value) {
  hrBuffer.irValues[hrBuffer.index] = value;
  hrBuffer.index = (hrBuffer.index + 1) % HR_BUFFER_SIZE;
  if (hrBuffer.count < HR_BUFFER_SIZE) {
    hrBuffer.count++;
  }
}

long getFilteredIR() {
  if (hrBuffer.count == 0) return 0;
  
  long sum = 0;
  for (int i = 0; i < hrBuffer.count; i++) {
    sum += hrBuffer.irValues[i];
  }
  return sum / hrBuffer.count;
}

void addToTempBuffer(float value) {
  tempBuffer.values[tempBuffer.index] = value;
  tempBuffer.index = (tempBuffer.index + 1) % TEMP_BUFFER_SIZE;
  if (tempBuffer.count < TEMP_BUFFER_SIZE) {
    tempBuffer.count++;
  }
}

float getFilteredTemp() {
  if (tempBuffer.count == 0) return 0.0;
  
  float sum = 0.0;
  for (int i = 0; i < tempBuffer.count; i++) {
    sum += tempBuffer.values[i];
  }
  return sum / tempBuffer.count;
}

// ============================================
// OUTPUT FUNCTIONS
// ============================================

void printSensorData(unsigned long timestamp) {
  Serial.print(timestamp);
  Serial.print("\t\t");
  
  if (sensorData.hrValid) {
    Serial.print(sensorData.heartRate);
  } else {
    Serial.print("--");
  }
  Serial.print("\t");
  
  if (sensorData.hrValid) {
    Serial.print(sensorData.spO2);
  } else {
    Serial.print("--");
  }
  Serial.print("\t");
  
  if (sensorData.tempValid) {
    Serial.print(sensorData.temperature, 1);
  } else {
    Serial.print("--");
  }
  Serial.print("\t\t");
  
  // Status indicator
  if (!sensorData.hrValid) {
    Serial.print("No finger detected");
  } else if (!sensorData.tempValid) {
    Serial.print("Temp sensor error");
  } else {
    Serial.print("OK");
  }
  
  Serial.println();
}

// ============================================
// NOTES FOR PHASE 3 (BLE Integration)
// ============================================
/*
 * Once this code produces stable readings:
 * 
 * 1. Verify output in Serial Monitor for 5+ minutes
 * 2. Check that BPM stays stable (±5 BPM variation)
 * 3. Verify temperature readings are consistent
 * 4. Test finger on/off detection
 * 
 * Then proceed to Phase 3:
 * - Add ArduinoBLE library
 * - Create BLE service and characteristic
 * - Transmit sensorData struct over BLE
 * - Keep all filtering logic intact
 * 
 * DO NOT add BLE until readings are stable!
 */
