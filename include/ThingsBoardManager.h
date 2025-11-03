#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class ThingsBoardManager {
public:
    void begin();
    void loop();
    void updateCredentials(const String& server, int port, const String& token);
    void sendTelemetry(const String& deviceName, const JsonDocument& data);

    bool isEnabled() const;
    void setEnabled(bool en);

    String getServer() const;
    int getPort() const;
    String getToken() const;

private:
    void reconnect();
    void loadFromNVS();
    void saveToNVS();

    String tb_server;
    int tb_port = 1883;
    String tb_token;
    bool enabled = false;
};
