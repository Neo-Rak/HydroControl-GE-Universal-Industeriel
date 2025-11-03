#ifndef WEBSERVER_MANAGER_H
#define WEBSERVER_MANAGER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

class WebServerManager {
public:
    WebServerManager();
    void begin();
    void loop();
    void notifyClients(const JsonDocument& doc);

private:
    AsyncWebServer server;
    AsyncWebSocket ws;

    void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
    void setupWebServer();
    String _getHtml();
};

#endif // WEBSERVER_MANAGER_H
