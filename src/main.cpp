#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "DeviceManager.h"
#include "LoRaManager.h"
#include "WebServerManager.h"
#include "ThingsBoardManager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_task_wdt.h>

// --- Global Objects ---
LoRaManager loraManager;
DeviceManager deviceManager(loraManager);
WebServerManager webServerManager(deviceManager);
ThingsBoardManager tbManager;

// --- Task Handles ---
TaskHandle_t commsTaskHandle = NULL;
TaskHandle_t deviceLogicTaskHandle = NULL;

void commsTask(void *pvParameters);
void deviceLogicTask(void *pvParameters);
void setupConfigurationMode();

void loraManager_onReceive(JsonDocument& doc) {
    deviceManager.processIncomingLoRaMessage(doc);
    webServerManager.notifyClients(doc);
}

void setup() {
    Serial.begin(115200);

    esp_task_wdt_init(30, true);
    esp_task_wdt_add(NULL);

    Preferences preferences;
    preferences.begin("hydro_config", true);
    if (!preferences.isKey("role")) {
        preferences.end();
        setupConfigurationMode();
    } else {
        preferences.end();

        WiFi.mode(WIFI_STA);
        // Connect WiFi...

        loraManager.begin();
        loraManager.setOnReceive(loraManager_onReceive);
        deviceManager.begin();
        webServerManager.begin();
        if (deviceManager.getRole() == HYDRO_CONTROL_GE) {
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
        loraManager.loop();
        if (deviceManager.getRole() == HYDRO_CONTROL_GE) {
            tbManager.loop();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void deviceLogicTask(void *pvParameters) {
    esp_task_wdt_add(NULL);
    for (;;) {
        esp_task_wdt_reset();
        deviceManager.loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void setupConfigurationMode() {
    // ...
}

void loop() {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(1000));
}
