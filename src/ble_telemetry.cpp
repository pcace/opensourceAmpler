/*
  BLE (Bluetooth Low Energy) Interface für ESP32 E-Bike Controller
  
  Dieses Modul läuft als separater Task auf Core 1 zusammen mit dem VESC und WiFi Task.
  Es stellt BLE Services zur Verfügung mit:
  - Live E-Bike Telemetrie-Daten über Characteristics
  - Mode Control über BLE Write Characteristics
  - Device Information Service für App-Kompatibilität
  
  WICHTIG: 
  - Läuft auf Core 1 mit NIEDRIGER Priorität
  - Verwendet thread-safe Zugriff auf shared data
  - BLE ist kompatibel mit Mobile Apps und Bike Computern
  - Geringerer Stromverbrauch als WiFi
*/

#include "ble_telemetry.h"
#include "ebike_controller.h"
#include "wifi_telemetry.h"  // For addLogMessage function

// External variables (defined in config.cpp)
extern int current_mode;

// BLE Task handle
TaskHandle_t bleTaskHandle = NULL;

// BLE Server and connection status
BLEServer* pBLEServer = NULL;
bool bleDeviceConnected = false;
bool bleOldDeviceConnected = false;

// BLE Services
BLEService* pTelemetryService = NULL;
BLEService* pControlService = NULL;
BLEService* pDeviceInfoService = NULL;

// BLE Characteristics - Telemetry
BLECharacteristic* pCharSpeed = NULL;
BLECharacteristic* pCharCadence = NULL;
BLECharacteristic* pCharTorque = NULL;
BLECharacteristic* pCharBattery = NULL;
BLECharacteristic* pCharCurrent = NULL;
BLECharacteristic* pCharVescData = NULL;
BLECharacteristic* pCharSystemStatus = NULL;

// BLE Characteristics - Control
BLECharacteristic* pCharModeControl = NULL;
BLECharacteristic* pCharModeList = NULL;
BLECharacteristic* pCharCommand = NULL;

// BLE Characteristics - Device Info
BLECharacteristic* pCharManufacturer = NULL;
BLECharacteristic* pCharModelNumber = NULL;
BLECharacteristic* pCharFirmwareRev = NULL;

// Server callbacks implementation
void EBikeServerCallbacks::onConnect(BLEServer* pServer) {
  bleDeviceConnected = true;
  Serial.println("BLE: Client connected");
  addLogMessage("BLE client connected");
}

void EBikeServerCallbacks::onDisconnect(BLEServer* pServer) {
  bleDeviceConnected = false;
  Serial.println("BLE: Client disconnected");
  addLogMessage("BLE client disconnected");
  
  // Restart advertising
  delay(500);
  pServer->startAdvertising();
  Serial.println("BLE: Started advertising again");
}

// Mode Control callback implementation
void EBikeModeControlCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
  
  if (value.length() > 0) {
    uint8_t new_mode = value[0];
    
    if (new_mode < NUM_ACTIVE_PROFILES) {
      Serial.printf("BLE: Mode change request to %d\n", new_mode);
      changeAssistMode(new_mode);
      addLogMessage("BLE Mode changed to: " + String(AVAILABLE_PROFILES[new_mode].name));
    } else {
      Serial.printf("BLE: Invalid mode %d requested\n", new_mode);
      addLogMessage("BLE Invalid mode requested: " + String(new_mode));
    }
  }
}

// Command callback implementation
void EBikeCommandCallbacks::onWrite(BLECharacteristic* pCharacteristic) {
  std::string value = pCharacteristic->getValue();
  
  if (value.length() > 0) {
    String command = String(value.c_str());
    Serial.println("BLE: Command received: " + command);
    
    if (command == "GET_STATUS") {
      // Send system status update
      updateBLETelemetryData();
      addLogMessage("BLE Status requested");
    } else if (command == "GET_MODES") {
      // Send mode list
      sendBLEModeList();
      addLogMessage("BLE Mode list requested");
    } else if (command == "EMERGENCY_STOP") {
      // Emergency stop - set mode to no assist
      for (int i = 0; i < NUM_ACTIVE_PROFILES; i++) {
        if (String(AVAILABLE_PROFILES[i].name) == "No Assist") {
          changeAssistMode(i);
          addLogMessage("BLE Emergency stop activated");
          break;
        }
      }
    } else {
      addLogMessage("BLE Unknown command: " + command);
    }
  }
}

// Update BLE telemetry data
void updateBLETelemetryData() {
  if (xSemaphoreTake(dataUpdateSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    // Debug output to verify data
    if (bleDeviceConnected) {
      Serial.printf("BLE: Updating data - Speed: %.1f km/h, Battery: %.1fV (%.0f%%), Torque: %.1f Nm (shared: %.1f)\n", 
                    sharedVescData.speed_kmh, sharedVescData.battery_voltage, 
                    sharedVescData.battery_percentage, sharedSensorData.filtered_torque, sharedSensorData.torque_nm);
    }
    
    // Speed characteristic (2 bytes uint16 - speed in 0.1 km/h units)
    uint16_t speed = (uint16_t)(sharedVescData.speed_kmh * 10.0f); // 0.1 km/h precision
    pCharSpeed->setValue((uint8_t*)&speed, 2);
    if (bleDeviceConnected) {
      Serial.printf("BLE: Speed set to %.1f km/h (uint16: %d)\n", sharedVescData.speed_kmh, speed);
      pCharSpeed->notify();
    }
    
    // Cadence characteristic (1 byte uint8 - RPM directly)
    uint8_t cadence = (uint8_t)constrain(sharedSensorData.cadence_rpm, 0, 255);
    pCharCadence->setValue(&cadence, 1);
    if (bleDeviceConnected) {
      Serial.printf("BLE: Cadence set to %.1f RPM (uint8: %d)\n", sharedSensorData.cadence_rpm, cadence);
      pCharCadence->notify();
    }
    
    // Torque characteristic (2 bytes uint16 - torque in 0.01 Nm units)
    uint16_t torque = (uint16_t)(sharedSensorData.filtered_torque * 100.0f); // 0.01 Nm precision
    pCharTorque->setValue((uint8_t*)&torque, 2);
    if (bleDeviceConnected) {
      Serial.printf("BLE: Torque set to %.2f Nm (uint16: %d)\n", sharedSensorData.filtered_torque, torque);
      pCharTorque->notify();
    }
    
    // Battery characteristic (1 byte uint8)
    uint8_t battery = (uint8_t)sharedVescData.battery_percentage;
    pCharBattery->setValue(&battery, 1);
    if (bleDeviceConnected) {
      Serial.printf("BLE: Battery set to %d%% (byte value: %d)\n", (int)sharedVescData.battery_percentage, battery);
      pCharBattery->notify();
    }
    
    // Current characteristic (2 bytes uint16 - current in 0.01 A units)
    uint16_t current = (uint16_t)(abs(sharedVescData.actual_current) * 100.0f); // 0.01 A precision, absolute value
    pCharCurrent->setValue((uint8_t*)&current, 2);
    if (bleDeviceConnected) {
      Serial.printf("BLE: Current set to %.2f A (uint16: %d)\n", sharedVescData.actual_current, current);
      pCharCurrent->notify();
    }
    
    // System Status (JSON string)
    JsonDocument statusDoc;
    statusDoc["mode"] = sharedSensorData.current_mode;
    statusDoc["mode_name"] = AVAILABLE_PROFILES[sharedSensorData.current_mode].name;
    statusDoc["motor_enabled"] = sharedSensorData.motor_enabled;
    statusDoc["timestamp"] = millis();
    
    String statusString;
    serializeJson(statusDoc, statusString);
    pCharSystemStatus->setValue(statusString.c_str());
    if (bleDeviceConnected) pCharSystemStatus->notify();
    
    xSemaphoreGive(dataUpdateSemaphore);
  }
}

// Update BLE VESC data
void updateBLEVescData() {
  if (xSemaphoreTake(dataUpdateSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    // VESC Data (JSON string für kompakte Übertragung)
    JsonDocument vescDoc;
    vescDoc["motor_rpm"] = sharedVescData.rpm;
    vescDoc["duty_cycle"] = sharedVescData.duty_cycle;
    vescDoc["temp_mosfet"] = sharedVescData.temp_mosfet;
    vescDoc["temp_motor"] = sharedVescData.temp_motor;
    vescDoc["battery_voltage"] = sharedVescData.battery_voltage;
    vescDoc["amp_hours"] = sharedVescData.amp_hours;
    vescDoc["watt_hours"] = sharedVescData.watt_hours;
    
    String vescString;
    serializeJson(vescDoc, vescString);
    pCharVescData->setValue(vescString.c_str());
    if (bleDeviceConnected) pCharVescData->notify();
    
    xSemaphoreGive(dataUpdateSemaphore);
  }
}

// Send available modes list
void sendBLEModeList() {
  JsonDocument modesDoc;
  JsonArray modesArray = modesDoc["modes"].to<JsonArray>();
  
  for (int i = 0; i < NUM_ACTIVE_PROFILES; i++) {
    JsonObject mode = modesArray.add<JsonObject>();
    mode["index"] = i;
    mode["name"] = AVAILABLE_PROFILES[i].name;
    mode["description"] = AVAILABLE_PROFILES[i].description;
    mode["hasLight"] = AVAILABLE_PROFILES[i].hasLight;
  }
  
  String modesString;
  serializeJson(modesDoc, modesString);
  pCharModeList->setValue(modesString.c_str());
  if (bleDeviceConnected) pCharModeList->notify();
}

// BLE Task main function
void bleTelemetryTask(void *pvParameters) {
  Serial.println("BLE: Task started");
  addLogMessage("BLE Task started");
  
  // Initialize BLE
  BLEDevice::init(BLE_DEVICE_NAME);
  
  // Create BLE Server
  pBLEServer = BLEDevice::createServer();
  pBLEServer->setCallbacks(new EBikeServerCallbacks());
  
  // ===== Device Information Service =====
  pDeviceInfoService = pBLEServer->createService(BLE_SERVICE_UUID_DEVICE_INFO);
  
  pCharManufacturer = pDeviceInfoService->createCharacteristic(
    BLE_CHAR_UUID_MANUFACTURER,
    BLECharacteristic::PROPERTY_READ
  );
  pCharManufacturer->setValue(BLE_MANUFACTURER);
  
  pCharModelNumber = pDeviceInfoService->createCharacteristic(
    BLE_CHAR_UUID_MODEL_NUMBER,
    BLECharacteristic::PROPERTY_READ
  );
  pCharModelNumber->setValue(BLE_MODEL_NUMBER);
  
  pCharFirmwareRev = pDeviceInfoService->createCharacteristic(
    BLE_CHAR_UUID_FIRMWARE_REV,
    BLECharacteristic::PROPERTY_READ
  );
  pCharFirmwareRev->setValue(BLE_FIRMWARE_VERSION);
  
  // ===== Telemetry Service =====
  pTelemetryService = pBLEServer->createService(BLE_SERVICE_UUID_TELEMETRY);
  
  // Speed characteristic
  pCharSpeed = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_SPEED,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharSpeed->addDescriptor(new BLE2902());
  
  // Cadence characteristic
  pCharCadence = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_CADENCE,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharCadence->addDescriptor(new BLE2902());
  
  // Torque characteristic
  pCharTorque = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_TORQUE,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharTorque->addDescriptor(new BLE2902());
  
  // Battery characteristic
  pCharBattery = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_BATTERY,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharBattery->addDescriptor(new BLE2902());
  
  // Current characteristic
  pCharCurrent = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_CURRENT,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharCurrent->addDescriptor(new BLE2902());
  
  // VESC Data characteristic
  pCharVescData = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_VESC_DATA,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharVescData->addDescriptor(new BLE2902());
  
  // System Status characteristic
  pCharSystemStatus = pTelemetryService->createCharacteristic(
    BLE_CHAR_UUID_SYSTEM_STATUS,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharSystemStatus->addDescriptor(new BLE2902());
  
  // ===== Control Service =====
  pControlService = pBLEServer->createService(BLE_SERVICE_UUID_CONTROL);
  
  // Mode Control characteristic (Write)
  pCharModeControl = pControlService->createCharacteristic(
    BLE_CHAR_UUID_MODE_CONTROL,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharModeControl->setCallbacks(new EBikeModeControlCallbacks());
  
  // Mode List characteristic (Read/Notify)
  pCharModeList = pControlService->createCharacteristic(
    BLE_CHAR_UUID_MODE_LIST,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  pCharModeList->addDescriptor(new BLE2902());
  
  // Command characteristic (Write)
  pCharCommand = pControlService->createCharacteristic(
    BLE_CHAR_UUID_COMMAND,
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharCommand->setCallbacks(new EBikeCommandCallbacks());
  
  // Start services
  pDeviceInfoService->start();
  pTelemetryService->start();
  pControlService->start();
  
  // Set initial values for characteristics
  uint16_t initialUint16 = 0;
  uint8_t initialUint8 = 0;
  pCharSpeed->setValue((uint8_t*)&initialUint16, 2);
  pCharCadence->setValue(&initialUint8, 1);
  pCharTorque->setValue((uint8_t*)&initialUint16, 2);
  pCharBattery->setValue(&initialUint8, 1);
  pCharCurrent->setValue((uint8_t*)&initialUint16, 2);
  pCharVescData->setValue("{}");
  pCharSystemStatus->setValue("{}");
  
  // Set initial mode list
  sendBLEModeList();
  
  // Start advertising
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID_TELEMETRY);
  pAdvertising->addServiceUUID(BLE_SERVICE_UUID_CONTROL);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // iPhone compatibility
  pAdvertising->setMinPreferred(0x12);
  
  BLEDevice::startAdvertising();
  Serial.println("BLE: Started advertising - Device name: " + String(BLE_DEVICE_NAME));
  addLogMessage("BLE advertising started - Name: " + String(BLE_DEVICE_NAME));
  
  // Main task loop
  TickType_t xLastWakeTime = xTaskGetTickCount();
  uint32_t loopCounter = 0;
  
  while (1) {
    loopCounter++;
    
    // Debug output every 10 loops to verify task is running
    if (loopCounter % 10 == 0) {
      Serial.printf("BLE: Task alive - Loop: %lu, Connected: %s\n", 
                    loopCounter, bleDeviceConnected ? "YES" : "NO");
    }
    
    // Handle connection state changes
    if (!bleDeviceConnected && bleOldDeviceConnected) {
      // Device disconnected
      delay(500);
      pBLEServer->startAdvertising();
      Serial.println("BLE: Restarted advertising");
      bleOldDeviceConnected = bleDeviceConnected;
    }
    
    if (bleDeviceConnected && !bleOldDeviceConnected) {
      // Device connected
      bleOldDeviceConnected = bleDeviceConnected;
    }
    
    // Update telemetry data (always, not just when connected)
    updateBLETelemetryData();
    updateBLEVescData();
    
    // Wait for next update cycle
    vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(BLE_UPDATE_RATE_MS));
  }
}

// Setup function to initialize BLE task
void setupBLETelemetry() {
  Serial.println("Setting up BLE Telemetry...");
  
  // Create BLE task on Core 1 with low priority
  BaseType_t result = xTaskCreatePinnedToCore(
    bleTelemetryTask,          // Task function
    "BLE_Task",                // Task name
    BLE_TASK_STACK_SIZE,       // Stack size
    NULL,                      // Parameters
    BLE_TASK_PRIORITY,         // Priority (low)
    &bleTaskHandle,            // Task handle
    1                          // Core 1 (same as WiFi and VESC)
  );
  
  if (result == pdPASS) {
    Serial.println("BLE task created successfully on Core 1");
  } else {
    Serial.println("ERROR: Failed to create BLE task");
  }
}
