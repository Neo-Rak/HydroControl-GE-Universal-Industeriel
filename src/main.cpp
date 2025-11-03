#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include "config.h"
#include "DeviceManager.h"
#include "LoraManager.h"
#include "WebServerManager.h"
#include "ThingsBoardManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Global Objects ---
DeviceManager deviceManager;
LoraManager loraManager;
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
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);

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
    webServerManager.notifyClients(); // Notify web clients of potential state change
}

void setupConfigurationMode() {
    // ... Implementation of setup mode ...
}

void loop() {
    // Empty. Everything is handled by FreeRTOS tasks.
}
