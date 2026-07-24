/*
 * Health Monitor - Arduino BLE Wearable Device
 * 
 * This code simulates a health monitoring device that sends
 * vital signs data via Bluetooth Low Energy (BLE)
 * 
 * Hardware Requirements:
 * - Arduino Nano 33 BLE (or similar BLE-capable board)
 * - Optional: MAX30102 sensor for real heart rate/SpO2
 * - Optional: DS18B20 for real temperature
 * 
 * For testing purposes, this code generates simulated data
 */

#include <ArduinoBLE.h>

// BLE Service and Characteristic UUIDs (must match Android app)
#define SERVICE_UUID        "0000180d-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID "00002a37-0000-1000-8000-00805f9b34fb"

// Create BLE Service
BLEService healthService(SERVICE_UUID);

// Create BLE Characteristic (Read + Notify)
BLECharacteristic vitalSignsCharacteristic(
  CHARACTERISTIC_UUID,
  BLERead | BLENotify,
  50  // Max data length
);

// Simulated sensor values
int heartRate = 75;
int spO2 = 98;
float temperature = 36.5;

// Timing
unsigned long lastUpdate = 0;
const unsigned long UPDATE_INTERVAL = 2000; // 2 seconds

void setup() {
  Serial.begin(9600);
  while (!Serial);
  
  Serial.println("Health Monitor - BLE Device");
  Serial.println("============================");
  
  // Initialize BLE
  if (!BLE.begin()) {
    Serial.println("Starting BLE failed!");
    while (1);
  }
  
  // Set device name (visible during scan)
  BLE.setLocalName("HealthMonitor");
  
  // Set advertised service
  BLE.setAdvertisedService(healthService);
  
  // Add characteristic to service
  healthService.addCharacteristic(vitalSignsCharacteristic);
  
  // Add service
  BLE.addService(healthService);
  
  // Set initial value
  String initialData = "HR:0,SpO2:0,Temp:0.0";
  vitalSignsCharacteristic.writeValue(initialData.c_str());
  
  // Start advertising
  BLE.advertise();
  
  Serial.println("BLE device is now advertising...");
  Serial.println("Waiting for connections...");
}

void loop() {
  // Wait for a BLE central device
  BLEDevice central = BLE.central();
  
  if (central) {
    Serial.print("Connected to central: ");
    Serial.println(central.address());
    
    // While connected
    while (central.connected()) {
      unsigned long currentTime = millis();
      
      // Update and send data every UPDATE_INTERVAL
      if (currentTime - lastUpdate >= UPDATE_INTERVAL) {
        lastUpdate = currentTime;
        
        // Simulate sensor readings
        updateVitalSigns();
        
        // Format data string
        String data = formatVitalSigns();
        
        // Send via BLE
        vitalSignsCharacteristic.writeValue(data.c_str());
        
        // Debug output
        Serial.println("Sent: " + data);
      }
    }
    
    Serial.println("Disconnected from central");
  }
}

/**
 * Simulate vital signs with realistic variations
 */
void updateVitalSigns() {
  // Simulate heart rate (60-100 BPM with small variations)
  heartRate = 75 + random(-10, 15);
  if (heartRate < 60) heartRate = 60;
  if (heartRate > 100) heartRate = 100;
  
  // Simulate SpO2 (95-100% with small variations)
  spO2 = 98 + random(-3, 2);
  if (spO2 < 95) spO2 = 95;
  if (spO2 > 100) spO2 = 100;
  
  // Simulate temperature (36.0-37.5°C with small variations)
  temperature = 36.5 + (random(-10, 10) / 10.0);
  if (temperature < 36.0) temperature = 36.0;
  if (temperature > 37.5) temperature = 37.5;
}

/**
 * Format vital signs as string
 * Format: "HR:75,SpO2:98,Temp:36.5"
 */
String formatVitalSigns() {
  String data = "HR:" + String(heartRate) + 
                ",SpO2:" + String(spO2) + 
                ",Temp:" + String(temperature, 1);
  return data;
}

/*
 * ============================================
 * TESTING SCENARIOS
 * ============================================
 * 
 * To test alert system, modify updateVitalSigns() to return:
 * 
 * 1. HIGH HEART RATE ALERT:
 *    heartRate = 150;
 * 
 * 2. LOW HEART RATE ALERT:
 *    heartRate = 50;
 * 
 * 3. LOW SPO2 ALERT:
 *    spO2 = 88;
 * 
 * 4. HIGH TEMPERATURE ALERT:
 *    temperature = 39.0;
 * 
 * 5. LOW TEMPERATURE ALERT:
 *    temperature = 35.0;
 * 
 * ============================================
 * REAL SENSOR INTEGRATION
 * ============================================
 * 
 * To use real sensors, replace updateVitalSigns() with:
 * 
 * #include <MAX30105.h>
 * #include <OneWire.h>
 * #include <DallasTemperature.h>
 * 
 * MAX30105 particleSensor;
 * OneWire oneWire(ONE_WIRE_BUS);
 * DallasTemperature sensors(&oneWire);
 * 
 * void updateVitalSigns() {
 *   // Read heart rate and SpO2 from MAX30102
 *   heartRate = particleSensor.getHeartRate();
 *   spO2 = particleSensor.getSpO2();
 *   
 *   // Read temperature from DS18B20
 *   sensors.requestTemperatures();
 *   temperature = sensors.getTempCByIndex(0);
 * }
 * 
 * ============================================
 * BINARY DATA FORMAT (Alternative)
 * ============================================
 * 
 * For more efficient transmission, use binary format:
 * 
 * void sendBinaryData() {
 *   uint8_t data[5];
 *   data[0] = (heartRate >> 8) & 0xFF;  // HR high byte
 *   data[1] = heartRate & 0xFF;         // HR low byte
 *   data[2] = spO2;                     // SpO2
 *   uint16_t temp = temperature * 10;   // Temp * 10
 *   data[3] = (temp >> 8) & 0xFF;       // Temp high byte
 *   data[4] = temp & 0xFF;              // Temp low byte
 *   
 *   vitalSignsCharacteristic.writeValue(data, 5);
 * }
 * 
 * Note: Android app would need to parse binary format
 * 
 * ============================================
 * POWER OPTIMIZATION
 * ============================================
 * 
 * For battery-powered operation:
 * 
 * 1. Increase UPDATE_INTERVAL to 5000ms (5 seconds)
 * 2. Use BLE connection interval negotiation
 * 3. Implement sleep mode between readings
 * 4. Reduce advertising power
 * 
 * Example:
 * BLE.setConnectionInterval(100, 200); // 100-200ms
 * BLE.setAdvertisingInterval(1000);    // 1 second
 * 
 */
