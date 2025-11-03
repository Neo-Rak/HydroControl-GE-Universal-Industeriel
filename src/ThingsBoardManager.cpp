#include "ThingsBoardManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

static WiFiClient espClient;
static PubSubClient client(espClient);
static unsigned long lastReconnectAttempt = 0;

ThingsBoardManager::ThingsBoardManager() {}

void ThingsBoardManager::begin() {
    loadFromNVS();
    if (enabled && WiFi.isConnected()) {
        client.setServer(tb_server.c_str(), tb_port);
    }
}

void ThingsBoardManager::updateCredentials(const String& server, int port, const String& token) {
    tb_server = server;
    tb_port = port;
    tb_token = token;
    saveToNVS();
    if (client.connected()) {
        client.disconnect();
    }
    reconnect();
}

void ThingsBoardManager::setEnabled(bool en) {
    enabled = en;
    saveToNVS();
    if (!enabled && client.connected()) {
        client.disconnect();
    } else if (enabled && !client.connected()) {
        reconnect();
    }
}

void ThingsBoardManager::loadFromNVS() {
    Preferences prefs;
    prefs.begin("thingsboard", true);
    enabled = prefs.getBool("tb_enabled", false);
    tb_server = prefs.getString("tb_server", "");
    tb_port = prefs.getInt("tb_port", 1883);
    tb_token = prefs.getString("tb_token", "");
    prefs.end();
}

void ThingsBoardManager::saveToNVS() {
    Preferences prefs;
    prefs.begin("thingsboard", false);
    prefs.putBool("tb_enabled", enabled);
    prefs.putString("tb_server", tb_server);
    prefs.putInt("tb_port", tb_port);
    prefs.putString("tb_token", tb_token);
    prefs.end();
}

void ThingsBoardManager::reconnect() {
    if (!enabled || tb_server.isEmpty() || tb_token.isEmpty() || !WiFi.isConnected()) {
        return;
    }
    client.setServer(tb_server.c_str(), tb_port);
    if (client.connect("HydroControl-Gateway", tb_token.c_str(), NULL)) {
        // Connected
    }
}

void ThingsBoardManager::loop() {
    if (enabled && WiFi.isConnected()) {
        if (!client.connected()) {
            if (millis() - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = millis();
                reconnect();
            }
        } else {
            client.loop();
        }
    }
}

void ThingsBoardManager::sendTelemetry(const String& deviceName, const JsonDocument& data) {
    if (!enabled || !client.connected() || deviceName.isEmpty()) {
        return;
    }

    JsonDocument doc;
    JsonObject deviceData = doc.to<JsonObject>()[deviceName].to<JsonObject>();
    JsonArray ts_values = deviceData["ts"].to<JsonArray>();

    JsonObject ts_obj = ts_values.add<JsonObject>();
    ts_obj["ts"] = millis();
    JsonObject values_obj = ts_obj["values"].to<JsonObject>();

    for (JsonPairConst kvp : data.as<JsonObjectConst>()) {
        values_obj[kvp.key()] = kvp.value();
    }

    String telemetry;
    serializeJson(doc, telemetry);

    client.publish("v1/gateway/telemetry", telemetry.c_str());
}

bool ThingsBoardManager::isEnabled() const { return enabled; }
String ThingsBoardManager::getServer() const { return tb_server; }
int ThingsBoardManager::getPort() const { return tb_port; }
String ThingsBoardManager::getToken() const { return tb_token; }
