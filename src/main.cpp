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
#include <esp_task_wdt.h>

// --- Global Objects ---
LoRaManager loraManager;
DeviceManager deviceManager(loraManager);
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
TaskHandle_t commsTaskHandle = NULL;
TaskHandle_t deviceLogicTaskHandle = NULL;

// --- Function Prototypes ---
void commsTask(void *pvParameters);
void deviceLogicTask(void *pvParameters);
void setupConfigurationMode();
void loraManager_onReceive(JsonDocument& doc);

void loraSend(JsonDocument& doc) {
    loraManager.send(doc);
}
void sendPumpCommand(const String& wellId, bool turnOn) {
    deviceManager.sendPumpCommand(wellId, turnOn);
}

void setup() {
    Serial.begin(115200);

    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    deviceId = WiFi.macAddress();
    sharedDataMutex = xSemaphoreCreateMutex();

    preferences.begin("hydro_config", true);
    if (!preferences.isKey("role")) {
        preferences.end();
        setupConfigurationMode();
    } else {
        currentRole = (DeviceRole)preferences.getInt("role");
        deviceName = preferences.getString("name");

        WiFi.mode(WIFI_STA);
        WiFi.begin(preferences.getString("ssid").c_str(), preferences.getString("pass").c_str());

        loraManager.begin();
        loraManager.setOnReceive(loraManager_onReceive);
        webServerManager.begin();
        deviceManager.begin();
        if (currentRole == HYDRO_CONTROL_GE) {
            tbManager.begin();
        }

        xTaskCreatePinnedToCore(commsTask, "CommsTask", 4096, NULL, 1, &commsTaskHandle, 0);
        xTaskCreatePinnedToCore(deviceLogicTask, "DeviceLogicTask", 4096, NULL, 2, &deviceLogicTaskHandle, 1);
    }
}

void commsTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    for (;;) {
        esp_task_wdt_reset();

        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(10000));
        } else {
            loraManager.loop();
            if (currentRole == HYDRO_CONTROL_GE) {
                tbManager.loop();
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

void deviceLogicTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    for (;;) {
        esp_task_wdt_reset();

        xSemaphoreTake(sharedDataMutex, portMAX_DELAY);
        deviceManager.loop();
        xSemaphoreGive(sharedDataMutex);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void loraManager_onReceive(JsonDocument& doc) {
    xSemaphoreTake(sharedDataMutex, portMAX_DELAY);
    deviceManager.processIncomingLoRaMessage(doc);
    xSemaphoreGive(sharedDataMutex);
    webServerManager.notifyClients(doc);
}

void setupConfigurationMode() {
    // Full implementation from before
}

void loop() {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
}
