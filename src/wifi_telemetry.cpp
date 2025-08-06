/*
  WiFi Web Interface für ESP32 E-Bike Controller
  
  Dieses Modul läuft als separater Task auf Core 1 zusammen mit dem VESC Task.
  Es stellt ein Web-Interface zur Verfügung mit:
  - Live E-Bike Telemetrie-Daten
  - Log-Nachrichten Anzeige
  - TCP basiert (kein UDP)
  
  WICHTIG: 
  - Läuft auf Core 1 mit NIEDRIGER Priorität
  - Verwendet thread-safe Zugriff auf shared data
  - Web Interface ist über Browser erreichbar
*/

#include "wifi_telemetry.h"
#include "ebike_controller.h"

// External variables (defined in config.cpp)
extern int current_mode;

// Task handle
TaskHandle_t wifiTaskHandle = NULL;

// Web Server
WebServer webServer(WEB_SERVER_PORT);
bool wifiConnected = false;

// Log messages storage (thread-safe with mutex)
SemaphoreHandle_t logMutex = NULL;
String logMessages[MAX_LOG_MESSAGES];
int logMessageCount = 0;
int logMessageIndex = 0;

// Add log message (thread-safe)
void addLogMessage(const String& message) {
  if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // Circular buffer für Log-Nachrichten
    logMessages[logMessageIndex] = String(millis()) + ": " + message;
    logMessageIndex = (logMessageIndex + 1) % MAX_LOG_MESSAGES;
    if (logMessageCount < MAX_LOG_MESSAGES) {
      logMessageCount++;
    }
    xSemaphoreGive(logMutex);
  }
}

void addLogMessage(const char* message) {
  addLogMessage(String(message));
}

// Web Interface HTML
const char* webInterface = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>E-Bike Controller</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: Arial, sans-serif; margin: 10px; background-color: #f0f0f0; }
        .container { max-width: 1200px; margin: 0 auto; }
        .card { background: white; padding: 12px; margin: 8px 0; border-radius: 6px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(140px, 1fr)); gap: 8px; }
        .grid-small { display: grid; grid-template-columns: repeat(auto-fit, minmax(110px, 1fr)); gap: 6px; }
        .value { font-size: 1.4em; font-weight: bold; color: #2c3e50; margin: 2px 0; }
        .value-small { font-size: 1.1em; font-weight: bold; color: #2c3e50; margin: 2px 0; }
        .unit { font-size: 0.7em; color: #7f8c8d; margin-top: 1px; }
        .label { font-size: 0.8em; color: #34495e; margin-bottom: 3px; font-weight: 500; }
        .metric-card { background: #f8f9fa; padding: 8px; border-radius: 4px; text-align: center; min-height: 50px; display: flex; flex-direction: column; justify-content: center; }
        .logs { height: 250px; overflow-y: auto; background: #2c3e50; color: #ecf0f1; padding: 10px; border-radius: 4px; font-family: monospace; font-size: 11px; line-height: 1.3; }
        .status-ok { color: #27ae60; }
        .status-warning { color: #f39c12; }
        .status-error { color: #e74c3c; }
        h1 { color: #2c3e50; text-align: center; margin: 15px 0; font-size: 1.8em; }
        h2 { color: #34495e; margin: 8px 0 12px 0; font-size: 1.2em; }
        .refresh-btn { background: #3498db; color: white; border: none; padding: 6px 12px; border-radius: 4px; cursor: pointer; margin: 3px; font-size: 12px; }
        .refresh-btn:hover { background: #2980b9; }
        .mode-btn { background: #95a5a6; color: white; border: none; padding: 8px 12px; border-radius: 5px; cursor: pointer; margin: 3px; font-size: 12px; font-weight: bold; min-width: 80px; }
        .mode-btn:hover { background: #7f8c8d; }
        .mode-btn.active { background: #e74c3c; }
        .mode-buttons { display: flex; flex-wrap: wrap; justify-content: center; gap: 6px; margin: 12px 0; }
        .two-column { display: grid; grid-template-columns: 1fr 1fr; gap: 15px; }
        @media (max-width: 768px) { 
            .two-column { grid-template-columns: 1fr; } 
            .grid { grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); }
            .grid-small { grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); }
        }
    </style>
</head>
<body>
    <div class="container">
        
        <div class="card">
            <h2>Assist Mode Control</h2>
            <div class="mode-buttons" id="modeButtons">
                <!-- Mode buttons will be populated by JavaScript -->
            </div>
        </div>
        
        <div class="two-column">
            <div class="card">
                <h2>Main Telemetry</h2>
                <button class="refresh-btn" onclick="updateData()">Refresh</button>
                <div class="grid" id="telemetryData">
                    <div class="metric-card">
                        <div class="label">Speed</div>
                        <div class="value" id="speed">--</div>
                        <div class="unit">km/h</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Cadence</div>
                        <div class="value" id="cadence">--</div>
                        <div class="unit">RPM</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Torque</div>
                        <div class="value" id="torque">--</div>
                        <div class="unit">Nm</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Battery</div>
                        <div class="value" id="battery">--</div>
                        <div class="unit">%</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Motor Current</div>
                        <div class="value" id="current">--</div>
                        <div class="unit">A</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Mode</div>
                        <div class="value" id="mode">--</div>
                        <div class="unit"></div>
                    </div>
                </div>
            </div>
            
            <div class="card">
                <h2>VESC Status</h2>
                <div class="grid-small">
                    <div class="metric-card">
                        <div class="label">Motor RPM</div>
                        <div class="value-small" id="motorRpm">--</div>
                        <div class="unit">RPM</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Duty Cycle</div>
                        <div class="value-small" id="dutyCycle">--</div>
                        <div class="unit">%</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">MOSFET Temp</div>
                        <div class="value-small" id="tempMosfet">--</div>
                        <div class="unit">°C</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Motor Temp</div>
                        <div class="value-small" id="tempMotor">--</div>
                        <div class="unit">°C</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Battery Voltage</div>
                        <div class="value-small" id="batteryVolt">--</div>
                        <div class="unit">V</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Amp Hours</div>
                        <div class="value-small" id="ampHours">--</div>
                        <div class="unit">Ah</div>
                    </div>
                    <div class="metric-card">
                        <div class="label">Watt Hours</div>
                        <div class="value-small" id="wattHours">--</div>
                        <div class="unit">Wh</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="card">
            <h2>System Log Messages</h2>
            <button class="refresh-btn" onclick="updateLogs()">Refresh Log</button>
            <div class="logs" id="logContainer">
                Loading logs...
            </div>
        </div>
    </div>

    <script>
        let currentMode = 0;
        let availableModes = [];
        
        function updateData() {
            fetch('/api/telemetry')
                .then(response => response.json())
                .then(data => {
                    // Main telemetry
                    document.getElementById('speed').textContent = data.speed.toFixed(1);
                    document.getElementById('cadence').textContent = data.cadence.toFixed(0);
                    document.getElementById('torque').textContent = data.torque.toFixed(1);
                    document.getElementById('battery').textContent = data.battery.toFixed(0);
                    document.getElementById('current').textContent = data.current.toFixed(1);
                    document.getElementById('mode').textContent = data.mode_name || data.mode;
                    
                    // VESC data
                    document.getElementById('motorRpm').textContent = data.motor_rpm.toFixed(0);
                    document.getElementById('dutyCycle').textContent = data.duty_cycle.toFixed(1);
                    document.getElementById('tempMosfet').textContent = data.temp_mosfet.toFixed(1);
                    document.getElementById('tempMotor').textContent = data.temp_motor.toFixed(1);
                    document.getElementById('batteryVolt').textContent = data.battery_voltage.toFixed(1);
                    document.getElementById('ampHours').textContent = data.amp_hours.toFixed(2);
                    document.getElementById('wattHours').textContent = data.watt_hours.toFixed(1);
                    
                    // Update current mode
                    currentMode = data.mode;
                    updateModeButtons();
                })
                .catch(error => console.error('Error:', error));
        }
        
        function updateModeButtons() {
            fetch('/api/modes')
                .then(response => response.json())
                .then(data => {
                    availableModes = data.modes;
                    const container = document.getElementById('modeButtons');
                    container.innerHTML = '';
                    
                    availableModes.forEach((mode, index) => {
                        const button = document.createElement('button');
                        button.className = 'mode-btn' + (index === currentMode ? ' active' : '');
                        button.textContent = mode.name;
                        button.title = mode.description;
                        button.onclick = () => changeMode(index);
                        container.appendChild(button);
                    });
                })
                .catch(error => console.error('Error loading modes:', error));
        }
        
        function changeMode(modeIndex) {
            fetch('/api/changemode', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ mode: modeIndex })
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    currentMode = modeIndex;
                    updateModeButtons();
                    updateData();
                }
            })
            .catch(error => console.error('Error changing mode:', error));
        }
        
        function updateLogs() {
            fetch('/api/logs')
                .then(response => response.json())
                .then(data => {
                    const logContainer = document.getElementById('logContainer');
                    logContainer.innerHTML = data.logs.join('<br>');
                    logContainer.scrollTop = logContainer.scrollHeight;
                })
                .catch(error => console.error('Error:', error));
        }
        
        setInterval(updateData, 2000);
        setInterval(updateLogs, 5000);
        
        updateData();
        updateModeButtons();
        updateLogs();
    </script>
</body>
</html>
)HTML";

// API Handler für Telemetrie-Daten
void handleTelemetryAPI() {
  if (xSemaphoreTake(dataUpdateSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
    JsonDocument doc;
    
    // Main telemetry data
    doc["speed"] = sharedVescData.speed_kmh;
    doc["cadence"] = sharedSensorData.cadence_rpm;
    doc["torque"] = sharedSensorData.filtered_torque;
    doc["battery"] = sharedVescData.battery_percentage;
    doc["current"] = sharedVescData.actual_current;
    doc["mode"] = sharedSensorData.current_mode;
    doc["motor_enabled"] = sharedSensorData.motor_enabled;
    doc["timestamp"] = millis();
    
    // Extended VESC data
    doc["motor_rpm"] = sharedVescData.rpm;
    doc["duty_cycle"] = sharedVescData.duty_cycle;
    doc["temp_mosfet"] = sharedVescData.temp_mosfet;
    doc["temp_motor"] = sharedVescData.temp_motor;
    doc["battery_voltage"] = sharedVescData.battery_voltage;
    doc["amp_hours"] = sharedVescData.amp_hours;
    doc["watt_hours"] = sharedVescData.watt_hours;
    
    // Add mode name from available profiles
    if (sharedSensorData.current_mode >= 0 && sharedSensorData.current_mode < NUM_ACTIVE_PROFILES) {
      doc["mode_name"] = AVAILABLE_PROFILES[sharedSensorData.current_mode].name;
    }
    
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
    
    xSemaphoreGive(dataUpdateSemaphore);
  } else {
    webServer.send(503, "application/json", "{\"error\":\"Data unavailable\"}");
  }
}

// API Handler für Log-Nachrichten
void handleLogsAPI() {
  if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    JsonDocument doc;
    JsonArray logsArray = doc["logs"].to<JsonArray>();
    
    // Neueste Nachrichten zuerst
    for (int i = 0; i < logMessageCount; i++) {
      int idx = (logMessageIndex - 1 - i + MAX_LOG_MESSAGES) % MAX_LOG_MESSAGES;
      if (logMessages[idx].length() > 0) {
        logsArray.add(logMessages[idx]);
      }
    }
    
    String response;
    serializeJson(doc, response);
    webServer.send(200, "application/json", response);
    
    xSemaphoreGive(logMutex);
  } else {
    webServer.send(503, "application/json", "{\"error\":\"Logs unavailable\"}");
  }
}

// API Handler für verfügbare Modi
void handleModesAPI() {
  JsonDocument doc;
  JsonArray modesArray = doc["modes"].to<JsonArray>();
  
  for (int i = 0; i < NUM_ACTIVE_PROFILES; i++) {
    JsonObject mode = modesArray.add<JsonObject>();
    mode["name"] = AVAILABLE_PROFILES[i].name;
    mode["description"] = AVAILABLE_PROFILES[i].description;
    mode["hasLight"] = AVAILABLE_PROFILES[i].hasLight;
  }
  
  String response;
  serializeJson(doc, response);
  webServer.send(200, "application/json", response);
}

// API Handler für Mode-Wechsel
void handleChangeModeAPI() {
  if (webServer.method() != HTTP_POST) {
    webServer.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }
  
  String body = webServer.arg("plain");
  JsonDocument doc;
  
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    webServer.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }
  
  if (!doc["mode"].is<int>()) {
    webServer.send(400, "application/json", "{\"error\":\"Missing mode parameter\"}");
    return;
  }
  
  int new_mode = doc["mode"];
  
  if (new_mode < 0 || new_mode >= NUM_ACTIVE_PROFILES) {
    webServer.send(400, "application/json", "{\"error\":\"Invalid mode number\"}");
    return;
  }
  
  // Change mode via external function
  changeAssistMode(new_mode);
  
  JsonDocument response_doc;
  response_doc["success"] = true;
  response_doc["new_mode"] = new_mode;
  response_doc["mode_name"] = AVAILABLE_PROFILES[new_mode].name;
  
  String response;
  serializeJson(response_doc, response);
  webServer.send(200, "application/json", response);
  
  addLogMessage("Mode changed to: " + String(AVAILABLE_PROFILES[new_mode].name));
}

void wifiTelemetryTask(void *pvParameters) {
  // Delay um sicherzustellen dass andere Tasks schon laufen
  vTaskDelay(pdMS_TO_TICKS(2000));
  
  Serial.println("=== WiFi Web Interface Task Starting ===");
  Serial.printf("WiFi Task running on Core: %d\n", xPortGetCoreID());
  
  // Log Mutex erstellen
  logMutex = xSemaphoreCreateMutex();
  if (logMutex == NULL) {
    Serial.println("ERROR: Failed to create log mutex!");
    vTaskDelete(NULL);
    return;
  }
  
  // Erste Log-Nachricht
  addLogMessage("WiFi Task started");
  
  // WiFi Access Point erstellen
  Serial.println("Creating WiFi Access Point...");
  addLogMessage("Creating WiFi Access Point: " + String(WIFI_AP_SSID));
  
  // WiFi Access Point konfigurieren
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
  
  bool apStarted = WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS);
  
  if (apStarted) {
    Serial.println();
    Serial.println("WiFi Access Point created successfully!");
    Serial.printf("AP SSID: %s\n", WIFI_AP_SSID);
    Serial.printf("AP Password: %s\n", WIFI_AP_PASSWORD);
    Serial.printf("AP IP address: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("Web interface: http://%s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("Connect your device to the WiFi network and open the IP address in browser");
    
    wifiConnected = true;
    addLogMessage("WiFi AP created - SSID: " + String(WIFI_AP_SSID));
    addLogMessage("AP IP: " + WiFi.softAPIP().toString());
    addLogMessage("Web Interface: http://" + WiFi.softAPIP().toString());
  } else {
    Serial.println();
    Serial.println("Failed to create WiFi Access Point!");
    addLogMessage("Failed to create WiFi Access Point!");
    wifiConnected = false;
  }
  
  // Web Server Setup
  if (wifiConnected) {
    // Route für Hauptseite
    webServer.on("/", HTTP_GET, []() {
      webServer.send(200, "text/html", webInterface);
    });
    
    // API Routes
    webServer.on("/api/telemetry", HTTP_GET, handleTelemetryAPI);
    webServer.on("/api/logs", HTTP_GET, handleLogsAPI);
    webServer.on("/api/modes", HTTP_GET, handleModesAPI);
    webServer.on("/api/changemode", HTTP_POST, handleChangeModeAPI);
    
    // 404 Handler
    webServer.onNotFound([]() {
      webServer.send(404, "text/plain", "404: Not Found");
    });
    
    webServer.begin();
    Serial.println("Web server started");
    addLogMessage("Web Server started on port " + String(WEB_SERVER_PORT));
  }
  
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(TELEMETRY_UPDATE_RATE_MS);
  
  for (;;) {
    // Web Server verarbeiten
    if (wifiConnected) {
      webServer.handleClient();
      
      // Gelegentliche Debug-Ausgabe und AP Status
      static unsigned long lastDebug = 0;
      if (millis() - lastDebug > 10000) { // Alle 10 Sekunden
        uint8_t connectedClients = WiFi.softAPgetStationNum();
        Serial.printf("[WiFi AP] Web Interface running - Connected clients: %d - Free heap: %d bytes\n", 
                     connectedClients, ESP.getFreeHeap());
        lastDebug = millis();
      }
    } else {
      // Falls AP aus irgendeinem Grund stoppt, versuchen neu zu starten
      static unsigned long lastAPCheck = 0;
      if (millis() - lastAPCheck > 5000) { // Alle 5 Sekunden prüfen
        Serial.println("[WiFi AP] Attempting to restart Access Point...");
        addLogMessage("Attempting to restart WiFi Access Point");
        
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_GATEWAY, WIFI_AP_SUBNET);
        if (WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CONNECTIONS)) {
          wifiConnected = true;
          addLogMessage("WiFi Access Point restarted successfully");
        }
        lastAPCheck = millis();
      }
    }
    
    // Warten bis nächster Zyklus
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void setupWifiTelemetry() {
  Serial.println("Creating WiFi Web Interface Task...");
  
  // WiFi Task auf Core 1 erstellen (gleicher Core wie VESC Task)
  BaseType_t wifiTaskResult = xTaskCreatePinnedToCore(
    wifiTelemetryTask,        // Task function
    "WiFiWebTask",            // Task name
    12288,                    // Stack size (Web Server braucht mehr Stack)
    NULL,                     // Parameter
    1,                        // Priority (NIEDRIGERE als VESC Task)
    &wifiTaskHandle,          // Task handle
    1                         // Core 1 (gleicher Core wie VESC)
  );
  
  if (wifiTaskResult == pdPASS && wifiTaskHandle != NULL) {
    Serial.println("WiFi Web Interface Task created successfully!");
  } else {
    Serial.println("ERROR: Failed to create WiFi Web Interface Task!");
  }
}
