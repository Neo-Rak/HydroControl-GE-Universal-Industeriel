#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <vector>
#include <map>
#include "config.h"
#include "LoRaManager.h"
#include "WebManager.h"
#include "ThingsBoardManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// Enum to define the roles
enum DeviceRole {
  ROLE_NOT_SET,
  AQUA_RESERV_PRO,
  WELLGUARD_PRO,
  HYDRO_CONTROL_GE
};

// Data structure for managed devices
struct Device {
    String id;
    String name;
    DeviceRole role;
    unsigned long lastSeen;
    // AquaReservPro specific
    bool isFull;
    // WellguardPro specific
    bool pumpOn;
    bool faultActive;
};

// --- Global State & Configuration ---
DeviceRole currentRole = ROLE_NOT_SET;
String deviceName = "";
String deviceId = "";
Preferences preferences;

// --- Managers ---
AsyncWebServer server_config(80);
LoRaManager loraManager;
WebManager webManager;
ThingsBoardManager tbManager;

// --- Shared Resources & RTOS Handles ---
SemaphoreHandle_t sharedDataMutex;
std::map<String, Device> managedDevices;

// Peripherals state
// These variables represent the physical state of the device's hardware
bool isFull = false;
bool pumpOn = false;
bool faultActive = false;
bool discovered = false;

// State tracking for logic loops
unsigned long lastHeartbeatSent = 0;
unsigned long lastDiscoverySent = 0;
unsigned long lastButtonPress = 0;
bool lastIsFullState = false;
bool lastFaultState = false;
bool lastPumpOnState = false;

// Central-specific data
std::map<String, std::vector<String>> wellAssignments;
unsigned long lastLogicCheck = 0;


// --- Forward Declarations ---
void CommunicationsTask(void *pvParameters);
void LogicTask(void *pvParameters);
void handleReceivedLoRaMessage(JsonDocument& doc);
void checkFillingLogic();
void sendPumpCommand(const String& wellId, bool turnOn);
void updateWebUI();
void setupAquaReservPro();
void loopAquaReservPro();
void setupWellguardPro();
void loopWellguardPro();
void setupHydroControlGE();
void loopHydroControlGE();
void setupConfigurationMode();


void setup() {
    Serial.begin(115200);
    deviceId = WiFi.macAddress();

    pinMode(LED_RED_PIN, OUTPUT);
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_BLUE_PIN, OUTPUT);
    digitalWrite(LED_RED_PIN, LOW);
    digitalWrite(LED_GREEN_PIN, LOW);
    digitalWrite(LED_BLUE_PIN, LOW);

    // Create Mutex for shared data protection
    sharedDataMutex = xSemaphoreCreateMutex();
    if (sharedDataMutex == NULL) {
        Serial.println("Error creating mutex!");
        ESP.restart();
    }

    preferences.begin("hydro_config", true); // read-only mode first
    currentRole = (DeviceRole)preferences.getInt("role", ROLE_NOT_SET);

    if (currentRole == ROLE_NOT_SET) {
        preferences.end();
        setupConfigurationMode();
    } else {
        deviceName = preferences.getString("name", "");
        String ssid = preferences.getString("ssid", "");
        String pass = preferences.getString("pass", "");
        preferences.end();

        WiFi.begin(ssid.c_str(), pass.c_str());
        Serial.print("Connecting to WiFi...");
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nConnected!");

        loraManager.begin();
        loraManager.setOnReceive(handleReceivedLoRaMessage);
        webManager.begin();

        switch(currentRole) {
            case AQUA_RESERV_PRO: setupAquaReservPro(); break;
            case WELLGUARD_PRO: setupWellguardPro(); break;
            case HYDRO_CONTROL_GE: setupHydroControlGE(); break;
            default: break; // Should not happen
        }

        // --- Task Creation ---
        xTaskCreatePinnedToCore(
            CommunicationsTask,   // Task function
            "CommsTask",          // Name of the task
            10000,                // Stack size of task
            NULL,                 // Parameter of the task
            1,                    // Priority of the task
            NULL,                 // Task handle to keep track of created task
            0);                   // Pin task to core 0

        xTaskCreatePinnedToCore(
            LogicTask,            // Task function
            "LogicTask",          // Name of the task
            10000,                // Stack size of task
            NULL,                 // Parameter of the task
            1,                    // Priority of the task
            NULL,                 // Task handle to keep track of created task
            1);                   // Pin task to core 1
    }
}

void CommunicationsTask(void *pvParameters) {
    Serial.println("Communications Task started on Core 0");
    for (;;) {
        loraManager.loop(); // Checks for incoming LoRa messages
        tbManager.loop();   // Handles MQTT connection and publishing
        vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other tasks
    }
}

void LogicTask(void *pvParameters) {
    Serial.println("Business Logic Task started on Core 1");
    unsigned long lastGreenLedBlink = 0;
    for (;;) {
        if (millis() - lastGreenLedBlink > 2000) {
            blinkLed(LED_GREEN_PIN, 50);
            lastGreenLedBlink = millis();
        }

        switch(currentRole) {
            case AQUA_RESERV_PRO: loopAquaReservPro(); break;
            case WELLGUARD_PRO: loopWellguardPro(); break;
            case HYDRO_CONTROL_GE: loopHydroControlGE(); break;
            default: break;
        }

        if(currentRole == WELLGUARD_PRO) {
            if(xSemaphoreTake(sharedDataMutex, pdMS_TO_TICKS(100) == pdTRUE)) {
                digitalWrite(LED_RED_PIN, faultActive ? HIGH : LOW);
                xSemaphoreGive(sharedDataMutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10)); // Main loop delay
    }
}


void handleReceivedLoRaMessage(JsonDocument& doc) {
    String senderId = doc["id"];
    String msgType = doc["type"];

    if (xSemaphoreTake(sharedDataMutex, portMAX_DELAY) == pdTRUE) {
        if (currentRole == HYDRO_CONTROL_GE) {
            if (msgType == "DISCOVERY") {
                if (managedDevices.find(senderId) == managedDevices.end()) {
                    Device newDevice;
                    newDevice.id = senderId;
                    newDevice.name = doc["payload"]["name"].as<String>();
                    String roleStr = doc["payload"]["role"];
                    if (roleStr == "AquaReservPro") newDevice.role = AQUA_RESERV_PRO;
                    else if (roleStr == "WellguardPro") newDevice.role = WELLGUARD_PRO;
                    newDevice.lastSeen = millis();
                    managedDevices[senderId] = newDevice;

                    JsonDocument ack;
                    ack["type"] = "DISCOVERY_ACK";
                    ack["id"] = deviceId; // Central's ID
                    ack["payload"]["id"] = senderId;
                    loraManager.send(ack);
                    Serial.println("New device discovered: " + newDevice.name);
                }
            } else if (managedDevices.count(senderId)) {
                managedDevices[senderId].lastSeen = millis();
                if (msgType == "STATUS") {
                    if (managedDevices[senderId].role == AQUA_RESERV_PRO) {
                        managedDevices[senderId].isFull = (doc["payload"]["level"] == "FULL");
                    } else if (managedDevices[senderId].role == WELLGUARD_PRO) {
                        managedDevices[senderId].pumpOn = (doc["payload"]["pump"] == "ON");
                        managedDevices[senderId].faultActive = doc["payload"]["fault"];
                    }
                } else if (msgType == "MANUAL_FILL_REQUEST") {
                    checkFillingLogic();
                } else if (msgType == "CRITICAL_FAULT") {
                    managedDevices[senderId].faultActive = true;
                }

                // Forward to ThingsBoard if enabled
                if (tbManager.isEnabled() && msgType == "STATUS") {
                    JsonDocument telemetry;
                    JsonObject payload = doc["payload"].as<JsonObject>();
                    for (JsonPair kv : payload) {
                        telemetry[kv.key()] = kv.value();
                    }
                    tbManager.sendTelemetry(managedDevices[senderId].name, telemetry);
                }
            }
        } else { // Peripheral roles
            if (msgType == "DISCOVERY_ACK" && doc["payload"]["id"] == deviceId) {
                discovered = true;
                Serial.println("Discovered by Central Unit.");
            }
            if (currentRole == WELLGUARD_PRO && msgType == "COMMAND") {
                const char* command = doc["payload"]["action"];
                bool newPumpState = pumpOn;
                if (strcmp(command, "PUMP_ON") == 0) {
                    newPumpState = true;
                } else if (strcmp(command, "PUMP_OFF") == 0) {
                    newPumpState = false;
                }
                if (newPumpState != pumpOn) {
                    pumpOn = newPumpState;
                    digitalWrite(ROLE_PIN_1, pumpOn ? HIGH : LOW);
                    // Acknowledge status change
                    // loopWellguardPro will send the status update
                }
            }
        }
        xSemaphoreGive(sharedDataMutex);
    }
    updateWebUI(); // This function will need to be thread-safe
}

// --- Implementation of Setup and Role-specific Logic ---

const char CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>HydroControl Setup</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background-color: #f2f2f2; color: #333; margin: 0; padding: 20px; }
        .container { max-width: 500px; margin: 0 auto; background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }
        h1 { color: #007aff; text-align: center; }
        label { display: block; margin-bottom: 5px; font-weight: bold; }
        input[type="text"], input[type="password"], select {
            width: 100%; padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 4px; box-sizing: border-box;
        }
        button {
            width: 100%; background-color: #007aff; color: white; padding: 14px 20px; margin: 8px 0; border: none;
            border-radius: 4px; cursor: pointer; font-size: 16px; font-weight: bold;
        }
        button:hover { background-color: #0056b3; }
        .footer { text-align: center; margin-top: 20px; color: #888; }
    </style>
</head>
<body>
    <div class="container">
        <h1>HydroControl Setup</h1>
        <form action="/save" method="post">
            <label for="name">Device Name</label>
            <input type="text" id="name" name="name" required placeholder="e.g., Bache_Piscine">

            <label for="role">Device Role</label>
            <select id="role" name="role">
                <option value="1">AquaReservPro</option>
                <option value="2">WellguardPro</option>
                <option value="3">HydroControl-GE (Central)</option>
            </select>

            <label for="ssid">WiFi SSID</label>
            <input type="text" id="ssid" name="ssid" required placeholder="Your WiFi Network Name">

            <label for="pass">WiFi Password</label>
            <input type="password" id="pass" name="pass" placeholder="Your WiFi Password">

            <button type="submit">Save and Reboot</button>
        </form>
    </div>
    <div class="footer">
        <p>Device ID: %DEVICE_ID%</p>
    </div>
</body>
</html>
)rawliteral";

void setupConfigurationMode() {
    String ssid = "HydroControl-Setup-" + deviceId.substring(deviceId.length() - 5, deviceId.length() - 3) + deviceId.substring(deviceId.length() - 2);
    WiFi.softAP(ssid.c_str());
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server_config.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        String html = CONFIG_HTML;
        html.replace("%DEVICE_ID%", deviceId);
        request->send_P(200, "text/html", html.c_str());
    });

    server_config.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        preferences.begin("hydro_config", false); // R/W mode

        int role = request->getParam("role", true)->value().toInt();
        preferences.putInt("role", role);

        String name = request->getParam("name", true)->value();
        preferences.putString("name", name);

        String ssid_sta = request->getParam("ssid", true)->value();
        preferences.putString("ssid", ssid_sta);

        String pass_sta = request->getParam("pass", true)->value();
        preferences.putString("pass", pass_sta);

        preferences.end();

        String response = "<h1>Settings Saved!</h1><p>Rebooting in 3 seconds...</p>";
        request->send(200, "text/html", response);

        delay(3000);
        ESP.restart();
    });

    server_config.begin();
    Serial.println("Configuration server started.");
}

void sendPeripheralStatusUpdate();

void setupAquaReservPro() {
    pinMode(ROLE_PIN_1, INPUT); // Level sensor
    pinMode(ROLE_PIN_2, INPUT_PULLUP); // Manual fill button
    lastIsFullState = (digitalRead(ROLE_PIN_1) == LOW); // LOW = CLOSED = FULL
    Serial.println("Role configured: AquaReservPro");
}

void loopAquaReservPro() {
    unsigned long now = millis();

    // Handle discovery
    if (!discovered && (now - lastDiscoverySent > 5000)) {
        JsonDocument doc;
        doc["type"] = "DISCOVERY";
        doc["id"] = deviceId;
        doc["payload"]["name"] = deviceName;
        doc["payload"]["role"] = "AquaReservPro";
        loraManager.send(doc);
        lastDiscoverySent = now;
        Serial.println("Sending discovery packet...");
    }

    // Read level sensor
    bool currentFullState = (digitalRead(ROLE_PIN_1) == LOW);
    if (currentFullState != lastIsFullState) {
        lastIsFullState = currentFullState;
        Serial.print("Level sensor changed state: ");
        Serial.println(lastIsFullState ? "FULL" : "EMPTY");
        sendPeripheralStatusUpdate();
    }

    // Read manual fill button
    if (digitalRead(ROLE_PIN_2) == LOW && (now - lastButtonPress > 1000)) { // Debounce
        lastButtonPress = now;
        JsonDocument doc;
        doc["type"] = "MANUAL_FILL_REQUEST";
        doc["id"] = deviceId;
        loraManager.send(doc);
        Serial.println("Manual fill request sent.");
    }

    // Send heartbeat
    if (discovered && (now - lastHeartbeatSent > 300000)) { // 5 minutes
        JsonDocument doc;
        doc["type"] = "HEARTBEAT";
        doc["id"] = deviceId;
        loraManager.send(doc);
        lastHeartbeatSent = now;
    }
}

void setupWellguardPro() {
    pinMode(ROLE_PIN_1, OUTPUT); // Relay control
    digitalWrite(ROLE_PIN_1, LOW); // Default to off
    pinMode(ROLE_PIN_2, INPUT_PULLUP); // Manual test button
    pinMode(ROLE_PIN_3, INPUT); // Fault sensor
    lastFaultState = (digitalRead(ROLE_PIN_3) == LOW); // LOW = CLOSED = FAULT
    Serial.println("Role configured: WellguardPro");
}

void loopWellguardPro() {
    unsigned long now = millis();

    // Handle discovery
    if (!discovered && (now - lastDiscoverySent > 5000)) {
        JsonDocument doc;
        doc["type"] = "DISCOVERY";
        doc["id"] = deviceId;
        doc["payload"]["name"] = deviceName;
        doc["payload"]["role"] = "WellguardPro";
        loraManager.send(doc);
        lastDiscoverySent = now;
    }

    // Read fault sensor - THIS HAS PRIORITY
    bool currentFaultState = (digitalRead(ROLE_PIN_3) == LOW);
    if (currentFaultState && !lastFaultState) {
        // A new fault has occurred
        lastFaultState = true;
        pumpOn = false; // Immediately cut the pump
        digitalWrite(ROLE_PIN_1, LOW);

        JsonDocument doc;
        doc["type"] = "CRITICAL_FAULT";
        doc["id"] = deviceId;
        loraManager.send(doc);
        Serial.println("CRITICAL FAULT DETECTED! Pump stopped.");
        sendPeripheralStatusUpdate();

    } else if (!currentFaultState && lastFaultState) {
        // Fault has been cleared
        lastFaultState = false;
        Serial.println("Fault cleared.");
        sendPeripheralStatusUpdate();
    }

    // Manual test button (only if no fault is active)
    if (!lastFaultState && digitalRead(ROLE_PIN_2) == LOW && (now - lastButtonPress > 1000)) {
        lastButtonPress = now;
        pumpOn = !pumpOn; // Toggle pump state
        digitalWrite(ROLE_PIN_1, pumpOn ? HIGH : LOW);
        Serial.print("Manual pump test toggled: ");
        Serial.println(pumpOn ? "ON" : "OFF");
        sendPeripheralStatusUpdate();
    }

    // If pump state has been changed by a command, we need to report it
    if(pumpOn != lastPumpOnState) {
        lastPumpOnState = pumpOn;
        sendPeripheralStatusUpdate();
    }

    // Send heartbeat
    if (discovered && (now - lastHeartbeatSent > 300000)) { // 5 minutes
        JsonDocument doc;
        doc["type"] = "HEARTBEAT";
        doc["id"] = deviceId;
        loraManager.send(doc);
        lastHeartbeatSent = now;
    }
}

void setupHydroControlGE() {
    Serial.println("Role configured: HydroControl-GE (Central)");
    tbManager.begin();
}

void loopHydroControlGE() {
    unsigned long now = millis();
    if (now - lastLogicCheck > 5000) { // Check logic every 5 seconds
        if (xSemaphoreTake(sharedDataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

            // 1. Check for disconnected modules
            for (auto it = managedDevices.begin(); it != managedDevices.end(); ++it) {
                if (now - it->second.lastSeen > 900000) { // 15 minutes
                    Serial.println("Module " + it->second.name + " is disconnected!");
                    // Mark as disconnected in the UI
                }
            }

            // 2. Execute filling logic
            checkFillingLogic();

            xSemaphoreGive(sharedDataMutex);
        }
        lastLogicCheck = now;
    }
}

void checkFillingLogic() {
    // This function must be called within a mutex lock
    for (auto const& [wellId, reservoirIds] : wellAssignments) {
        bool shouldPumpBeOn = false;

        // DECISION TO START: Is at least one assigned reservoir not full?
        for (const String& reservoirId : reservoirIds) {
            if (managedDevices.count(reservoirId) && !managedDevices[reservoirId].isFull) {
                shouldPumpBeOn = true;
                break; // Found one, no need to check further
            }
        }

        // DECISION TO STOP: Are ALL assigned reservoirs full?
        // This logic is implicitly handled. If shouldPumpBeOn remains false, it means all reservoirs are full.

        // Check current pump status and send command if needed
        if (managedDevices.count(wellId)) {
            bool currentPumpState = managedDevices[wellId].pumpOn;
            if (shouldPumpBeOn && !currentPump-State) {
                Serial.println("Logic: Turning pump ON for well " + managedDevices[wellId].name);
                sendPumpCommand(wellId, true);
            } else if (!shouldPumpBeOn && currentPumpState) {
                Serial.println("Logic: Turning pump OFF for well " + managedDevices[wellId].name);
                sendPumpCommand(wellId, false);
            }
        }
    }
}

void blinkLed(int pin, int duration_ms) {
    digitalWrite(pin, HIGH);
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    digitalWrite(pin, LOW);
}

void loraSend(JsonDocument& doc) {
    doc["timestamp"] = millis();
    blinkLed(LED_BLUE_PIN, 50);
    loraManager.send(doc);
}

void sendPumpCommand(const String& wellId, bool turnOn) {
    JsonDocument doc;
    doc["type"] = "COMMAND";
    doc["id"] = deviceId;
    doc["payload"]["action"] = turnOn ? "PUMP_ON" : "PUMP_OFF";
    doc["payload"]["target"] = wellId;
    loraSend(doc);
}

void sendPeripheralStatusUpdate() {
    JsonDocument doc;
    doc["type"] = "STATUS";
    doc["id"] = deviceId;
    if (currentRole == AQUA_RESERV_PRO) {
        doc["payload"]["level"] = lastIsFullState ? "FULL" : "EMPTY";
    } else if (currentRole == WELLGUARD_PRO) {
        doc["payload"]["pump"] = pumpOn ? "ON" : "OFF";
        doc["payload"]["fault"] = lastFaultState;
    }
    loraSend(doc);
    Serial.println("Sent status update.");
}

void updateWebUI() {
    webManager.notifyClients();
}

// The main loop is no longer used by our application logic
void loop() {
    vTaskDelete(NULL); // Delete the loopTask to free up resources
}
