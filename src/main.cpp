#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "config.h"
#include "DeviceManager.h"
#include "LoRaManager.h"
#include "WebServerManager.h"
#include "ThingsBoardManager.h"
#include "types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <map>
#include <vector>

// --- Global Objects ---
DeviceManager deviceManager;
LoRaManager loraManager;
WebServerManager webServerManager;
ThingsBoardManager tbManager;
Preferences preferences;
AsyncWebServer server_config(80);

// --- Global State ---
DeviceRole currentRole = ROLE_NOT_SET;
String deviceId = "";
String deviceName = "";
bool discovered = false;
std::map<String, Device> managedDevices;
std::map<String, std::vector<String>> wellAssignments;
SemaphoreHandle_t sharedDataMutex;
bool pumpOn = false;
bool isFull = false;
bool faultActive = false;


// --- Task Handles ---
TaskHandle_t loraTaskHandle = NULL;
TaskHandle_t deviceLogicTaskHandle = NULL;

// --- Function Prototypes ---
void loraTask(void *pvParameters);
void deviceLogicTask(void *pvParameters);
void setupConfigurationMode();
void loraManager_onReceive(JsonDocument& doc);

void loraSend(JsonDocument& doc) {
    loraManager.send(doc);
}

void setup() {
    Serial.begin(115200);

    deviceId = WiFi.macAddress();
    sharedDataMutex = xSemaphoreCreateMutex();

    preferences.begin("hydro_config", true);
    if (!preferences.isKey("role")) {
        preferences.end();
        setupConfigurationMode();
    } else {
        currentRole = (DeviceRole)preferences.getInt("role");
        deviceName = preferences.getString("name");
        String ssid = preferences.getString("ssid");
        String pass = preferences.getString("pass");
        preferences.end();

        WiFi.begin(ssid.c_str(), pass.c_str());
        while (WiFi.status() != WL_CONNECTED) {
            delay(500);
            Serial.print(".");
        }
        Serial.println("\nWiFi Connected.");

        loraManager.begin();
        loraManager.setOnReceive(loraManager_onReceive);
        webServerManager.begin();
        deviceManager.begin();
        if (currentRole == HYDRO_CONTROL_GE) {
            tbManager.begin();
        }

        xTaskCreatePinnedToCore(loraTask, "LoraTask", 4096, NULL, 1, &loraTaskHandle, 0);
        xTaskCreatePinnedToCore(deviceLogicTask, "DeviceLogicTask", 4096, NULL, 1, &deviceLogicTaskHandle, 1);
    }
}

void loraTask(void *pvParameters) {
    for (;;) {
        loraManager.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void deviceLogicTask(void *pvParameters) {
    for (;;) {
        xSemaphoreTake(sharedDataMutex, portMAX_DELAY);
        deviceManager.loop();
        xSemaphoreGive(sharedDataMutex);

        if (currentRole == HYDRO_CONTROL_GE) {
            tbManager.loop();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void loraManager_onReceive(JsonDocument& doc) {
    xSemaphoreTake(sharedDataMutex, portMAX_DELAY);
    deviceManager.handleLoraMessage(doc);
    xSemaphoreGive(sharedDataMutex);
    webServerManager.notifyClients();
}

void setupConfigurationMode() {
    String ssid = "HydroControl-Setup-" + deviceId.substring(deviceId.length() - 5, deviceId.length() - 3) + deviceId.substring(deviceId.length() - 2);
    WiFi.softAP(ssid.c_str());
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP);

    server_config.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
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
        String html = CONFIG_HTML;
        html.replace("%DEVICE_ID%", deviceId);
        request->send(200, "text/html", html);
    });

    server_config.on("/save", HTTP_POST, [](AsyncWebServerRequest *request){
        preferences.begin("hydro_config", false);

        int role = request->getParam("role", true)->value().toInt();
        preferences.putInt("role", role);

        String name = request->getParam("name", true)->value();
        preferences.putString("name", name);

        String ssid_sta = request->getParam("ssid", true)->value();
        preferences.putString("ssid", ssid_sta);

        String pass_sta = request->getParam("pass", true)->value();
        preferences.putString("pass", pass_sta);

        preferences.end();

        request->send(200, "text/html", "<h1>Settings Saved!</h1><p>Rebooting...</p>");
        delay(3000);
        ESP.restart();
    });

    server_config.begin();
}

void loop() {
    // Empty
}
