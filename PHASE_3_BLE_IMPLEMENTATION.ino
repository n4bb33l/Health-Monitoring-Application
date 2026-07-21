/*
 * PHASE 3: BLE Communication Layer
 * 
 * Purpose: Extend Phase 2 with Bluetooth Low Energy transmission
 * Focus: Real-time sensor data streaming to Android app
 * 
 * Hardware:
 * - Arduino Nano 33 BLE
 * - MAX30102 Heart Rate/SpO2 Sensor (I2C)
 * - Temperature Sensor (analog or I2C)
 * 
 * Output: 
 * - Serial Monitor (debug)
 * - BLE notifications to connected Android device
 */

#include <Wire.h>
#include <ArduinoBLE.h>
#include "MAX30105.h"
#include "heartRate.h"

// ============================================
// CONFIGURATION
// ============================================

// Sampling rates (milliseconds)
#define HR_SAMPLE_INTERVAL    20    // 50Hz for heart rate
#define TEMP_SAMPLE_INTERVAL  1000  // 1Hz for temperature
#define PRINT_INTERVAL        2000  // Print every 2 seconds
#define BLE_UPDATE_INTERVAL   1000  // Send BLE notification every 1 second

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

// BLE Configuration
#define BLE_DEVICE_NAME       "ChildHealthWearable"
#define BLE_LOCAL_NAME        "Child Health Monitor"

// ============================================
// BLE SERVICE & CHARACTERISTIC UUIDs
// ============================================

// Custom service UUID for health monitoring
BLEService healthService("19B10000-E8F2-537E-4F6C-D104768A1214");

// Characteristic for sensor data (Notify + Read)
// Data format: HR(2 bytes) | SpO2(1 byte) | Temp(2 bytes) | Flags(1 byte)
BLECharacteristic sensorDataChar("19B10001-E8F2-537E-4F6C-D104768A1214",
                                  BLERead | BLENotify, 6);

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
  unsigned long lastBleUpdate;
} sensorData = {0, 0, 0.0, false, false, 0, 0, 0, 0};

// Peak detection for heart rate
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;

// BLE connection state
bool bleConnected = false;

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  
  Serial.println("=================================");
  Serial.println("PHASE 3: BLE Communication");
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
  
  Serial.println("✓ Sensors initialized successfully");
  
  // Initialize BLE
  if (!initBLE()) {
    Serial.println("ERROR: BLE initialization failed!");
    Serial.println("Check Arduino Nano 33 BLE board.");
    while (1) {
      delay(1000);
    }
  }
  
  Serial.println("✓ BLE initialized successfully");
  Serial.println();
  Serial.println("Starting data acquisition and BLE advertising...");
  Serial.println();
  Serial.println("Time(ms)\tHR(BPM)\tSpO2(%)\tTemp(°C)\tBLE\tStatus");
  Serial.println("-----------------------------------------------------------------------");
}

// ============================================
// MAIN LOOP
// ============================================

void loop() {
  unsigned long currentTime = millis();
  
  // Poll BLE events (connection/disconnection)
  BLE.poll();
  
  // Check connection status
  BLEDevice central = BLE.central();
  if (central) {
    if (!bleConnected) {
      bleConnected = true;
      Serial.println();
      Serial.println(">>> BLE Device Connected: " + String(central.address()));
      Serial.println();
    }
  } else {
    if (bleConnected) {
      bleConnected = false;
      Serial.println();
      Serial.println("<<< BLE Device Disconnected");
      Serial.println();
    }
  }
  
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
  
  // Send BLE notification if connected and data is ready
  if (bleConnected && (currentTime - sensorData.lastBleUpdate >= BLE_UPDATE_INTERVAL)) {
    sensorData.lastBleUpdate = currentTime;
    sendBLENotification();
  }
  
  // Print filtered data periodically
  if (currentTime - sensorData.lastPrint >= PRINT_INTERVAL) {
    sensorData.lastPrint = currentTime;
    printSensorData(currentTime);
  }
}

// ============================================
// BLE INITIALIZATION
// ============================================

bool initBLE() {
  // Initialize BLE
  if (!BLE.begin()) {
    return false;
  }
  
  // Set device name and local name
  BLE.setDeviceName(BLE_DEVICE_NAME);
  BLE.setLocalName(BLE_LOCAL_NAME);
  
  // Set advertised service
  BLE.setAdvertisedService(healthService);
  
  // Add characteristic to service
  healthService.addCharacteristic(sensorDataChar);
  
  // Add service
  BLE.addService(healthService);
  
  // Initialize characteristic with zeros
  byte initialData[6] = {0, 0, 0, 0, 0, 0};
  sensorDataChar.writeValue(initialData, 6);
  
  // Start advertising
  BLE.advertise();
  
  Serial.println("BLE Advertising started");
  Serial.println("Device Name: " + String(BLE_DEVICE_NAME));
  Serial.println("Waiting for Android connection...");
  
  return true;
}

// ============================================
// BLE DATA TRANSMISSION
// ============================================

void sendBLENotification() {
  // Only send if we have valid data
  if (!sensorData.hrValid && !sensorData.tempValid) {
    return;  // No valid data to send
  }
  
  // Prepare data packet (6 bytes)
  // Byte 0-1: Heart Rate (uint16, little-endian)
  // Byte 2:   SpO2 (uint8)
  // Byte 3-4: Temperature * 10 (uint16, little-endian, to preserve 1 decimal)
  // Byte 5:   Flags (bit 0: HR valid, bit 1: Temp valid)
  
  byte data[6];
  
  // Heart Rate (2 bytes)
  uint16_t hr = sensorData.hrValid ? sensorData.heartRate : 0;
  data[0] = hr & 0xFF;         // Low byte
  data[1] = (hr >> 8) & 0xFF;  // High byte
  
  // SpO2 (1 byte)
  data[2] = sensorData.hrValid ? sensorData.spO2 : 0;
  
  // Temperature (2 bytes, multiplied by 10)
  uint16_t temp = sensorData.tempValid ? (uint16_t)(sensorData.temperature * 10) : 0;
  data[3] = temp & 0xFF;         // Low byte
  data[4] = (temp >> 8) & 0xFF;  // High byte
  
  // Flags (1 byte)
  data[5] = 0;
  if (sensorData.hrValid) data[5] |= 0x01;   // Bit 0: HR valid
  if (sensorData.tempValid) data[5] |= 0x02; // Bit 1: Temp valid
  
  // Write to characteristic (triggers notification)
  sensorDataChar.writeValue(data, 6);
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
  
  // BLE status
  if (bleConnected) {
    Serial.print("CONN");
  } else {
    Serial.print("ADV");
  }
  Serial.print("\t");
  
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
// PHASE 3 IMPLEMENTATION NOTES
// ============================================
/*
 * BLE Data Format (6 bytes):
 * 
 * Byte 0-1: Heart Rate (uint16, little-endian)
 *           Range: 0-200 BPM
 * 
 * Byte 2:   SpO2 (uint8)
 *           Range: 0-100%
 * 
 * Byte 3-4: Temperature * 10 (uint16, little-endian)
 *           Example: 36.5°C = 365
 *           Range: 300-420 (30.0-42.0°C)
 * 
 * Byte 5:   Flags (uint8)
 *           Bit 0: Heart Rate valid (1 = valid, 0 = invalid)
 *           Bit 1: Temperature valid (1 = valid, 0 = invalid)
 *           Bits 2-7: Reserved for future use
 * 
 * Android Parsing Example:
 * 
 * int heartRate = (data[1] << 8) | data[0];
 * int spO2 = data[2];
 * float temperature = ((data[4] << 8) | data[3]) / 10.0f;
 * boolean hrValid = (data[5] & 0x01) != 0;
 * boolean tempValid = (data[5] & 0x02) != 0;
 * 
 * Testing:
 * 1. Upload to Arduino Nano 33 BLE
 * 2. Open Serial Monitor (115200 baud)
 * 3. Verify "BLE Advertising started" message
 * 4. Use nRF Connect app to scan and connect
 * 5. Subscribe to notifications on characteristic 19B10001...
 * 6. Verify data updates every 1 second
 * 7. Test finger on/off detection
 * 
 * Next Steps (Phase 4 - Android App):
 * - Scan for "ChildHealthWearable"
 * - Connect to device
 * - Subscribe to characteristic notifications
 * - Parse 6-byte data format
 * - Display real-time readings
 */
