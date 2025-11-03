#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

class ThingsBoardManager {
public:
    void begin();
    void loop();
    void updateCredentials(const String& server, int port, const String& token);
    void sendTelemetry(const JsonDocument& doc);
    bool isEnabled() const { return enabled; }
    void setEnabled(bool enabled);

private:
    void reconnect();

    String tb_server;
    int tb_port;
    String tb_token;
    bool enabled = false;
};
