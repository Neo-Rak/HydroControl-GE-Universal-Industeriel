#include "DeviceManager.h"
#include "config.h"
#include "types.h"
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "ThingsBoardManager.h"

extern DeviceRole currentRole;
extern String deviceId;
extern String deviceName;
extern std::map<String, Device> managedDevices;
extern std::map<String, std::vector<String>> wellAssignments;
extern bool discovered;
extern ThingsBoardManager tbManager;
extern bool pumpOn;
extern bool isFull;
extern bool faultActive;

extern void loraSend(JsonDocument& doc);
extern void sendPumpCommand(const String& wellId, bool turnOn);

static unsigned long lastHeartbeatSent = 0;
static unsigned long lastDiscoverySent = 0;
static unsigned long lastButtonPress = 0;
static bool lastIsFullState = false;
static bool lastFaultState = false;
static bool lastPumpOnState = false;
static unsigned long lastLogicCheck = 0;

void sendPeripheralStatusUpdate();
void checkFillingLogic();

void DeviceManager::begin() {
    setupRole();
}

void DeviceManager::loop() {
    loopRole();
}

void DeviceManager::setupRole() {
    switch (currentRole) {
        case AQUA_RESERV_PRO:
            pinMode(ROLE_PIN_1, INPUT);
            pinMode(ROLE_PIN_2, INPUT_PULLUP);
            lastIsFullState = (digitalRead(ROLE_PIN_1) == LOW);
            break;
        case WELLGUARD_PRO:
            pinMode(ROLE_PIN_1, OUTPUT);
            digitalWrite(ROLE_PIN_1, LOW);
            pinMode(ROLE_PIN_2, INPUT_PULLUP);
            pinMode(ROLE_PIN_3, INPUT);
            lastFaultState = (digitalRead(ROLE_PIN_3) == LOW);
            break;
        case HYDRO_CONTROL_GE:
            break;
    }
}

void DeviceManager::loopRole() {
    switch (currentRole) {
        case AQUA_RESERV_PRO: loopAquaReservPro(); break;
        case WELLGUARD_PRO: loopWellguardPro(); break;
        case HYDRO_CONTROL_GE: loopCentrale(); break;
    }
}

void DeviceManager::handleLoraMessage(JsonDocument& doc) {
    String senderId = doc["id"];
    String msgType = doc["type"];

    if (currentRole == HYDRO_CONTROL_GE) {
        if (managedDevices.find(senderId) == managedDevices.end() && msgType == "DISCOVERY") {
            Device newDevice;
            newDevice.id = senderId;
            newDevice.name = doc["payload"]["name"].as<String>();
            String roleStr = doc["payload"]["role"];
            if (roleStr == "AquaReservPro") newDevice.role = AQUA_RESERV_PRO;
            else if (roleStr == "WellguardPro") newDevice.role = WELLGUARD_PRO;
            managedDevices[senderId] = newDevice;
        }

        if (managedDevices.count(senderId)) {
            managedDevices[senderId].lastSeen = millis();
            if (msgType == "STATUS") {
                if (managedDevices[senderId].role == AQUA_RESERV_PRO) {
                    managedDevices[senderId].isFull = (doc["payload"]["level"] == "FULL");
                } else if (managedDevices[senderId].role == WELLGUARD_PRO) {
                    managedDevices[senderId].pumpOn = (doc["payload"]["pump"] == "ON");
                    managedDevices[senderId].faultActive = doc["payload"]["fault"];
                }

                if (tbManager.isEnabled()) {
                    JsonDocument telemetry;
                    JsonObject payload = doc["payload"].as<JsonObject>();
                    for (JsonPair kv : payload) {
                        telemetry[kv.key().c_str()] = kv.value();
                    }
                    tbManager.sendTelemetry(managedDevices[senderId].name, telemetry);
                }
            } else if (msgType == "CRITICAL_FAULT") {
                managedDevices[senderId].faultActive = true;
            }
        }
    } else { // Peripherals
        if (msgType == "COMMAND" && currentRole == WELLGUARD_PRO) {
            bool turnOn = (doc["payload"]["action"] == "PUMP_ON");
            pumpOn = turnOn;
            digitalWrite(ROLE_PIN_1, pumpOn ? HIGH : LOW);
        } else if (msgType == "DISCOVERY_ACK" && doc["payload"]["id"] == deviceId) {
            discovered = true;
        }
    }
}

void DeviceManager::loopAquaReservPro() {
    unsigned long now = millis();
    if (!discovered && (now - lastDiscoverySent > 5000)) {
        JsonDocument doc;
        doc["type"] = "DISCOVERY";
        doc["id"] = deviceId;
        doc["payload"]["name"] = deviceName;
        doc["payload"]["role"] = "AquaReservPro";
        loraSend(doc);
        lastDiscoverySent = now;
    }

    bool currentFullState = (digitalRead(ROLE_PIN_1) == LOW);
    if (currentFullState != lastIsFullState) {
        lastIsFullState = currentFullState;
        isFull = currentFullState;
        sendPeripheralStatusUpdate();
    }

    if (digitalRead(ROLE_PIN_2) == LOW && (now - lastButtonPress > 1000)) {
        lastButtonPress = now;
        JsonDocument doc;
        doc["type"] = "MANUAL_FILL_REQUEST";
        doc["id"] = deviceId;
        loraSend(doc);
    }

    if (discovered && (now - lastHeartbeatSent > 300000)) {
        JsonDocument doc;
        doc["type"] = "HEARTBEAT";
        doc["id"] = deviceId;
        loraSend(doc);
        lastHeartbeatSent = now;
    }
}

void DeviceManager::loopWellguardPro() {
    unsigned long now = millis();
    if (!discovered && (now - lastDiscoverySent > 5000)) {
        JsonDocument doc;
        doc["type"] = "DISCOVERY";
        doc["id"] = deviceId;
        doc["payload"]["name"] = deviceName;
        doc["payload"]["role"] = "WellguardPro";
        loraSend(doc);
        lastDiscoverySent = now;
    }

    bool currentFaultState = (digitalRead(ROLE_PIN_3) == LOW);
    if (currentFaultState && !lastFaultState) {
        lastFaultState = true;
        faultActive = true;
        pumpOn = false;
        digitalWrite(ROLE_PIN_1, LOW);
        JsonDocument doc;
        doc["type"] = "CRITICAL_FAULT";
        doc["id"] = deviceId;
        loraSend(doc);
    } else if (!currentFaultState && lastFaultState) {
        lastFaultState = false;
        faultActive = false;
    }

    if (pumpOn != lastPumpOnState || lastFaultState != currentFaultState) {
        sendPeripheralStatusUpdate();
    }
    lastPumpOnState = pumpOn;
    lastFaultState = currentFaultState;

    if (discovered && (now - lastHeartbeatSent > 300000)) {
        JsonDocument doc;
        doc["type"] = "HEARTBEAT";
        doc["id"] = deviceId;
        loraSend(doc);
        lastHeartbeatSent = now;
    }
}

void DeviceManager::loopCentrale() {
    unsigned long now = millis();
    if (now - lastLogicCheck > 5000) {
        checkFillingLogic();
        lastLogicCheck = now;
    }
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
}

void checkFillingLogic() {
    for (auto const& [wellId, reservoirIds] : wellAssignments) {
        bool shouldPumpBeOn = false;
        for (const String& reservoirId : reservoirIds) {
            if (managedDevices.count(reservoirId) && !managedDevices.at(reservoirId).isFull) {
                shouldPumpBeOn = true;
                break;
            }
        }

        if (managedDevices.count(wellId)) {
            if (managedDevices.at(wellId).faultActive) {
                shouldPumpBeOn = false;
            }
            if (shouldPumpBeOn != managedDevices.at(wellId).pumpOn) {
                sendPumpCommand(wellId, shouldPumpBeOn);
            }
        }
    }
}

void sendPumpCommand(const String& wellId, bool turnOn) {
    JsonDocument doc;
    doc["type"] = "COMMAND";
    doc["id"] = deviceId;
    doc["payload"]["action"] = turnOn ? "PUMP_ON" : "PUMP_OFF";
    doc["payload"]["target"] = wellId;
    loraSend(doc);
}
