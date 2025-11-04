#include "DeviceManager.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>

DeviceManager::DeviceManager(LoRaManager& lora) : _lora(lora) {
    _currentRole = ROLE_NOT_SET;
    _discovered = false;
    _lastStatusSent = 0;
    _lastLogicCheck = 0;
    _mutex = xSemaphoreCreateMutex();
    _lastButtonPress = 0;
    _buttonPressed = false;
}

void DeviceManager::begin() {
    Preferences prefs;
    prefs.begin("hydro_config", true);
    _currentRole = (DeviceRole)prefs.getInt("role", ROLE_NOT_SET);
    _deviceName = prefs.getString("name", "");
    String loraKey = prefs.getString("loraKey", "");
    prefs.end();

    if (loraKey.length() == 16) {
        _lora.setEncryptionKey(loraKey.c_str());
    }

    _deviceId = WiFi.macAddress();

    if (_currentRole == HYDRO_CONTROL_GE) {
        loadAssignments();
    }
    setupRole();
}

DeviceRole DeviceManager::getRole() const {
    return _currentRole;
}

void DeviceManager::loop() {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    loopRole();
    xSemaphoreGive(_mutex);
}

void DeviceManager::processIncomingLoRaMessage(JsonDocument& doc) {
    xSemaphoreTake(_mutex, portMAX_DELAY);
    String type = doc["type"];
    if (type == "DISCOVERY") handleDiscovery(doc);
    else if (type == "DISCOVERY_ACK") handleDiscoveryAck(doc);
    else if (type == "HEARTBEAT") handleHeartbeat(doc);
    else if (type == "STATUS") handleStatus(doc);
    else if (type == "COMMAND") handleCommand(doc);
    else if (type == "MANUAL_FILL_REQUEST") handleManualFillRequest(doc);
    else if (type == "CRITICAL_FAULT") handleCriticalFault(doc);
    xSemaphoreGive(_mutex);
}

void DeviceManager::setupRole() {
    switch (_currentRole) {
        case AQUA_RESERV_PRO:
            pinMode(ROLE_PIN_1, INPUT);
            pinMode(ROLE_PIN_2, INPUT_PULLUP);
            break;
        case WELLGUARD_PRO:
            pinMode(ROLE_PIN_1, OUTPUT);
            digitalWrite(ROLE_PIN_1, LOW);
            pinMode(ROLE_PIN_2, INPUT_PULLUP);
            pinMode(ROLE_PIN_3, INPUT);
            break;
        case HYDRO_CONTROL_GE:
            break;
        default:
            break;
    }
}

void DeviceManager::loopRole() {
    unsigned long now = millis();
    if (!_discovered && _currentRole != HYDRO_CONTROL_GE) {
        if (now - _lastDiscoverySent > 10000) {
            sendDiscovery();
            _lastDiscoverySent = now;
        }
    }

    switch (_currentRole) {
        case AQUA_RESERV_PRO:
            loopAquaReservPro();
            break;
        case WELLGUARD_PRO:
            loopWellguardPro();
            break;
        case HYDRO_CONTROL_GE:
            loopCentrale();
            break;
        default:
            break;
    }
}

void DeviceManager::loopAquaReservPro() {
    bool isFull = (digitalRead(ROLE_PIN_1) == LOW);
    if (isFull != _isFull || millis() - _lastStatusSent > 300000) {
        _isFull = isFull;
        sendStatusUpdate();
        _lastStatusSent = millis();
    }

    if (digitalRead(ROLE_PIN_2) == LOW && !_buttonPressed && (millis() - _lastButtonPress > 50)) {
        _buttonPressed = true;
        _lastButtonPress = millis();
        JsonDocument doc;
        doc["from"] = _deviceId;
        doc["type"] = "MANUAL_FILL_REQUEST";
        _lora.send(doc);
    } else if (digitalRead(ROLE_PIN_2) == HIGH) {
        _buttonPressed = false;
    }
}

void DeviceManager::loopWellguardPro() {
    bool fault = (digitalRead(ROLE_PIN_3) == LOW);
    if (fault && !_faultActive) {
        _faultActive = true;
        digitalWrite(ROLE_PIN_1, LOW); // Turn off pump
        _pumpOn = false;
        sendStatusUpdate();
    } else if (!fault && _faultActive) {
        _faultActive = false;
        sendStatusUpdate();
    }

    if (millis() - _lastStatusSent > 300000) {
        sendStatusUpdate();
        _lastStatusSent = millis();
    }

    if (digitalRead(ROLE_PIN_2) == LOW && !_buttonPressed && (millis() - _lastButtonPress > 50)) {
        _buttonPressed = true;
        _lastButtonPress = millis();
        _pumpOn = !_pumpOn;
        digitalWrite(ROLE_PIN_1, _pumpOn);
        sendStatusUpdate();
    } else if (digitalRead(ROLE_PIN_2) == HIGH) {
        _buttonPressed = false;
    }
}

void DeviceManager::loopCentrale() {
    if (millis() - _lastLogicCheck > 5000) {
        checkDeviceTimeouts();
        runSmartPumpLogic();
        _lastLogicCheck = millis();
    }
}

void DeviceManager::checkDeviceTimeouts() {
    for (auto it = _managedDevices.begin(); it != _managedDevices.end(); ++it) {
        if (millis() - it->second.lastSeen > 900000) { // 15 minutes
            it->second.status = STATUS_DISCONNECTED;
            if (it->second.role == AQUA_RESERV_PRO) {
                it->second.level = LEVEL_UNKNOWN;
            }
        }
    }
}

void DeviceManager::runSmartPumpLogic() {
    for (auto const& [wellId, wellDevice] : _managedDevices) {
        if (wellDevice.role == WELLGUARD_PRO) {
            if (wellDevice.faultActive || wellDevice.status == STATUS_DISCONNECTED) {
                if (wellDevice.pumpOn) {
                    sendPumpCommand(wellId, false);
                }
                continue;
            }

            bool shouldPump = false;
            for (auto const& reservoirId : wellDevice.assignedReservoirIds) {
                 if (_managedDevices.count(reservoirId) &&
                     _managedDevices[reservoirId].level == LEVEL_EMPTY &&
                     _managedDevices[reservoirId].status == STATUS_OK) {
                    shouldPump = true;
                    break;
                }
            }
            if (wellDevice.pumpOn != shouldPump) {
                sendPumpCommand(wellId, shouldPump);
            }
        }
    }
}

void DeviceManager::sendDiscovery() {
    JsonDocument doc;
    doc["from"] = _deviceId;
    doc["type"] = "DISCOVERY";
    doc["role"] = (int)_currentRole;
    doc["name"] = _deviceName;
    _lora.send(doc);
}

void DeviceManager::sendStatusUpdate() {
    JsonDocument doc;
    doc["from"] = _deviceId;
    doc["type"] = "STATUS";
    if (_currentRole == AQUA_RESERV_PRO) {
        doc["isFull"] = _isFull;
    } else if (_currentRole == WELLGUARD_PRO) {
        doc["pumpOn"] = _pumpOn;
        doc["fault"] = _faultActive;
    }
    _lora.send(doc);
}

void DeviceManager::sendPumpCommand(const String& wellId, bool turnOn) {
    JsonDocument doc;
    doc["from"] = _deviceId;
    doc["to"] = wellId;
    doc["type"] = "COMMAND";
    doc["command"] = turnOn ? "PUMP_ON" : "PUMP_OFF";
    _lora.send(doc);
}


void DeviceManager::handleDiscovery(JsonDocument& doc) {
    if (_currentRole != HYDRO_CONTROL_GE) return;

    String id = doc["from"];
    if (_managedDevices.find(id) == _managedDevices.end()) {
        Device newDevice;
        newDevice.id = id;
        newDevice.name = doc["name"].as<String>();
        newDevice.role = (DeviceRole)doc["role"].as<int>();
        newDevice.lastSeen = millis();
        newDevice.status = STATUS_OK; // Initialize status
        _managedDevices[id] = newDevice;

        JsonDocument ack;
        ack["from"] = _deviceId;
        ack["to"] = id;
        ack["type"] = "DISCOVERY_ACK";
        _lora.send(ack);
    }
}

void DeviceManager::handleDiscoveryAck(JsonDocument& doc) {
    if (doc["to"].as<String>() == _deviceId) {
        _discovered = true;
    }
}

void DeviceManager::handleHeartbeat(JsonDocument& doc) {
    String id = doc["from"];
    if (_managedDevices.count(id)) {
        _managedDevices[id].lastSeen = millis();
    }
}

void DeviceManager::handleStatus(JsonDocument& doc) {
    String id = doc["from"];
    if (_managedDevices.count(id)) {
        Device& dev = _managedDevices[id];
        dev.lastSeen = millis();
        if (dev.role == AQUA_RESERV_PRO) {
            dev.level = doc["isFull"] ? LEVEL_FULL : LEVEL_EMPTY;
        } else if (dev.role == WELLGUARD_PRO) {
            dev.pumpOn = doc["pumpOn"];
            dev.faultActive = doc["fault"];
        }
    }
}

void DeviceManager::handleCommand(JsonDocument& doc) {
    if (_currentRole != WELLGUARD_PRO || doc["to"].as<String>() != _deviceId) return;
    String command = doc["command"];
    if (command == "PUMP_ON") {
        digitalWrite(ROLE_PIN_1, HIGH);
    } else if (command == "PUMP_OFF") {
        digitalWrite(ROLE_PIN_1, LOW);
    }
}

void DeviceManager::handleManualFillRequest(JsonDocument& doc) {
    if (_currentRole != HYDRO_CONTROL_GE) return;
    String reservoirId = doc["from"];
    // The logic will trigger the pump via runSmartPumpLogic
}

void DeviceManager::handleCriticalFault(JsonDocument& doc) {
    if (_currentRole != HYDRO_CONTROL_GE) return;
    String wellId = doc["from"];
    if (_managedDevices.count(wellId)) {
        _managedDevices[wellId].faultActive = true;
        _managedDevices[wellId].pumpOn = false;
        // Also need to send PUMP_OFF command for safety
        sendPumpCommand(wellId, false);
    }
}

void DeviceManager::loadAssignments() {
    Preferences prefs;
    prefs.begin("assignments", true);
    String assignmentsStr = prefs.getString("json", "{}");
    prefs.end();

    JsonDocument doc;
    deserializeJson(doc, assignmentsStr);
    for (JsonPairConst kvp : doc.as<JsonObjectConst>()) {
        String wellId = kvp.key().c_str();
        for (JsonVariantConst reservoirId : kvp.value().as<JsonArrayConst>()) {
            assignReservoirToWell(reservoirId.as<String>(), wellId);
        }
    }
}

void DeviceManager::saveAssignments() {
    JsonDocument doc;
    for (auto const& [wellId, wellDevice] : _managedDevices) {
        if (wellDevice.role == WELLGUARD_PRO) {
            JsonArray arr = doc.to<JsonObject>()[wellId].to<JsonArray>();
            for (auto const& reservoirId : wellDevice.assignedReservoirIds) {
                arr.add(reservoirId);
            }
        }
    }
    String assignmentsStr;
    serializeJson(doc, assignmentsStr);

    Preferences prefs;
    prefs.begin("assignments", false);
    prefs.putString("json", assignmentsStr);
    prefs.end();
}

void DeviceManager::assignReservoirToWell(const String& reservoirId, const String& wellId) {
    if (_managedDevices.count(wellId) && _managedDevices.count(reservoirId)) {
        _managedDevices[wellId].assignedReservoirIds.push_back(reservoirId);
        _managedDevices[reservoirId].assignedWellId = wellId;
    }
}
