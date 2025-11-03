#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "ThingsBoardManager.h"

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastReconnectAttempt = 0;

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
    if (client.connected()) client.disconnect();
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
    prefs.begin("thingsboard", true); // Read-only
    enabled = prefs.getBool("enabled", false);
    tb_server = prefs.getString("server", "");
    tb_port = prefs.getInt("port", 1883);
    tb_token = prefs.getString("token", "");
    prefs.end();
}

void ThingsBoardManager::saveToNVS() {
    Preferences prefs;
    prefs.begin("thingsboard", false); // Read-write
    prefs.putBool("enabled", enabled);
    prefs.putString("server", tb_server);
    prefs.putInt("port", tb_port);
    prefs.putString("token", tb_token);
    prefs.end();
}

void ThingsBoardManager::reconnect() {
    if (!enabled || tb_server.isEmpty() || tb_token.isEmpty() || !WiFi.isConnected()) {
        return;
    }
    Serial.println("Attempting ThingsBoard MQTT connection...");
    client.setServer(tb_server.c_str(), tb_port);
    if (client.connect("HydroControl-Gateway", tb_token.c_str(), NULL)) {
        Serial.println("ThingsBoard MQTT connected");
    } else {
        Serial.print("failed, rc=");
        Serial.print(client.state());
    }
}

void ThingsBoardManager::loop() {
    if (enabled && WiFi.isConnected()) {
        if (!client.connected()) {
            long now = millis();
            if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
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
    JsonObject deviceData = doc.createNestedObject(deviceName);
    JsonArray values = deviceData.createNestedArray("values");
    values.add(data);

    String telemetry;
    serializeJson(doc, telemetry);

    if (client.publish("v1/gateway/telemetry", telemetry.c_str())) {
        Serial.println("Telemetry sent to ThingsBoard for " + deviceName);
    } else {
        Serial.println("Failed to send telemetry to ThingsBoard");
    }
}
