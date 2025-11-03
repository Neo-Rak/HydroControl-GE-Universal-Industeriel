#ifndef DEVICEMANAGER_H
#define DEVICEMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "types.h"
#include "LoRaManager.h"

class DeviceManager {
public:
    DeviceManager(LoRaManager& lora);
    void begin();
    void loop();

    void processIncomingLoRaMessage(JsonDocument& doc);
    void sendDiscovery();
    void sendStatusUpdate();
    void assignReservoirToWell(const String& reservoirId, const String& wellId);
    void sendPumpCommand(const String& wellId, bool turnOn);
    void saveAssignments();

private:
    void setupRole();
    void loopRole();
    void loopAquaReservPro();
    void loopWellguardPro();
    void loopCentrale();
    void loadAssignments();

    void handleDiscovery(JsonDocument& doc);
    void handleDiscoveryAck(JsonDocument& doc);
    void handleHeartbeat(JsonDocument& doc);
    void handleStatus(JsonDocument& doc);
    void handleCommand(JsonDocument& doc);
    void handleManualFillRequest(JsonDocument& doc);
    void handleCriticalFault(JsonDocument& doc);

    void checkDeviceTimeouts();
    void runSmartPumpLogic();

    LoRaManager& _lora;
    DeviceRole _currentRole;
    String _deviceName;
    String _deviceId;

    std::map<String, Device> _managedDevices;

    // Peripheral state
    bool _isFull;
    bool _pumpOn;
    bool _faultActive;

    // Network state
    bool _discovered;
    unsigned long _lastDiscoverySent;
    unsigned long _lastStatusSent;
    unsigned long _lastLogicCheck;
};

#endif // DEVICEMANAGER_H
