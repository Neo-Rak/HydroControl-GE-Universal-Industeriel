#pragma once

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

class WebManager {
public:
    void begin();
    void notifyClients(const JsonDocument& doc);

private:
    void setupWebServer();
    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);

    AsyncWebServer server;
    AsyncWebSocket ws;
};
