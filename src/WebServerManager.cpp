#include "WebServerManager.h"
#include "config.h"
#include "types.h"
#include <ArduinoJson.h>
#include "DeviceManager.h"

WebServerManager::WebServerManager(DeviceManager& dm) : server(80), ws("/ws"), _deviceManager(dm) {}

void WebServerManager::begin() {
    ws.onEvent(std::bind(&WebServerManager::onWsEvent, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4, std::placeholders::_5, std::placeholders::_6));
    server.addHandler(&ws);
    setupWebServer();
    server.begin();
}

void WebServerManager::loop() {
    ws.cleanupClients();
}

void WebServerManager::notifyClients(const JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    ws.textAll(json);
}

void WebServerManager::setupWebServer() {
    server.on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
        request->send(200, "text/html", _getHtml());
    });
}

void WebServerManager::onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if (type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
            JsonDocument doc;
            if (deserializeJson(doc, (const char*)data, len) == DeserializationError::Ok) {
                String msgType = doc["type"];
                if (msgType == "FORCE_PUMP") {
                    _deviceManager.sendPumpCommand(doc["wellId"], doc["state"]);
                } else if (msgType == "SAVE_ASSIGNMENTS") {
                    // This logic should be in DeviceManager
                    // For now, let's just save
                    _deviceManager.saveAssignments();
                }
            }
        }
    }
}

String WebServerManager::_getHtml() {
    // This should be more dynamic, but for now...
    return "<html><head><title>HydroControl</title></head><body><h1>HydroControl</h1></body></html>";
}
