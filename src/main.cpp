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
    bool isFull;
    bool pumpOn;
    bool faultActive;
};

DeviceRole currentRole = ROLE_NOT_SET;
String deviceName = "";
String deviceId = "";

AsyncWebServer server_config(80);
Preferences preferences;
LoRaManager loraManager;
WebManager webManager;
ThingsBoardManager tbManager;

std::map<String, Device> managedDevices;
unsigned long lastHeartbeat = 0;
bool discovered = false;
bool isFull = false;
bool pumpOn = false;
bool faultActive = false;

// Forward declarations
void checkFillingLogic();
void sendPumpCommand(const String& wellId, bool turnOn);
void updateWebUI();
void setupAquaReservPro();
void loopAquaReservPro();
void setupWellguardPro();
void loopWellguardPro();
void setupHydroControlGE();
void loopHydroControlGE();

void handleReceivedLoRaMessage(JsonDocument& doc) {
    String senderId = doc["id"];
    String msgType = doc["type"];

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
                ack["payload"]["id"] = senderId;
                loraManager.send(ack);
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
        }
        if (tbManager.isEnabled()) {
             // ... ThingsBoard logic ...
        }
    } else {
        if (msgType == "DISCOVERY_ACK" && doc["payload"]["id"] == deviceId) {
            discovered = true;
        }
        if (currentRole == WELLGUARD_PRO && msgType == "COMMAND") {
            const char* command = doc["payload"]["action"];
            if (strcmp(command, "PUMP_ON") == 0) {
                if (!pumpOn) {
                    pumpOn = true;
                    digitalWrite(ROLE_PIN_1, HIGH);
                }
            } else if (strcmp(command, "PUMP_OFF") == 0) {
                if (pumpOn) {
                    pumpOn = false;
                    digitalWrite(ROLE_PIN_1, LOW);
                }
            }
        }
    }
    updateWebUI();
}

void setup() {
    Serial.begin(115200);
    preferences.begin("hydro_config", true);
    currentRole = (DeviceRole)preferences.getInt("role", ROLE_NOT_SET);
    deviceName = preferences.getString("name", "");
    deviceId = WiFi.macAddress();

    if (currentRole == ROLE_NOT_SET) {
        // ... Configuration mode setup ...
    } else {
        WiFi.begin(preferences.getString("ssid", "").c_str(), preferences.getString("pass", "").c_str());
        while (WiFi.status() != WL_CONNECTED) { delay(500); }
        loraManager.begin();
        loraManager.setOnReceive(handleReceivedLoRaMessage);
        webManager.begin();
        switch(currentRole) {
            case AQUA_RESERV_PRO: setupAquaReservPro(); break;
            case WELLGUARD_PRO: setupWellguardPro(); break;
            case HYDRO_CONTROL_GE: setupHydroControlGE(); break;
        }
    }
}

void loop() {
    loraManager.loop();
    switch(currentRole) {
        case AQUA_RESERV_PRO: loopAquaReservPro(); break;
        case WELLGUARD_PRO: loopWellguardPro(); break;
        case HYDRO_CONTROL_GE: loopHydroControlGE(); break;
    }
    if (currentRole != ROLE_NOT_SET) {
        tbManager.loop();
    }
}

void setupAquaReservPro() { /* ... */ }
void loopAquaReservPro() { /* ... */ }
void setupWellguardPro() { /* ... */ }
void loopWellguardPro() { /* ... */ }
void setupHydroControlGE() { /* ... */ }
void loopHydroControlGE() { /* ... */ }
void checkFillingLogic() { /* ... */ }
void sendPumpCommand(const String& wellId, bool turnOn) { /* ... */ }
void updateWebUI() { /* ... */ }
