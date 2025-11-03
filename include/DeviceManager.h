#pragma once

#include "Arduino.h"
#include <ArduinoJson.h>

class DeviceManager {
public:
    void begin();
    void loop();

    // Methods to handle incoming data/events
    void handleLoraMessage(JsonDocument& doc);

private:
    void setupRole();
    void loopRole();

    // Role-specific logic loops
    void loopAquaReservPro();
    void loopWellguardPro();
    void loopCentrale();
};
