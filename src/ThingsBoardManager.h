#ifndef THINGSBOARDMANAGER_H
#define THINGSBOARDMANAGER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h" // Include for Device struct

class ThingsBoardManager {
public:
    ThingsBoardManager();
    void begin();
    void loop();
    void sendTelemetry(const Device& device);
    void sendTelemetry(const String& deviceName, const JsonDocument& data);
    void updateDeviceStatus(const String& deviceName, const String& key, const String& value);
    bool isEnabled() const;

    void updateCredentials(const String& server, int port, const String& token);
    void setEnabled(bool en);
    String getServer() const;
    int getPort() const;
    String getToken() const;

private:
    void reconnect();
    void loadFromNVS();
    void saveToNVS();

    String tb_server;
    int tb_port;
    String tb_token;
    bool enabled;
};

#endif // THINGSBOARDMANAGER_H
