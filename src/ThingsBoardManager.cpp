#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "ThingsBoardManager.h"

static WiFiClient espClient;
static PubSubClient client(espClient);
static unsigned long lastReconnectAttempt = 0;

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
    prefs.begin("thingsboard", true); // Read-only
    enabled = prefs.getBool("tb_enabled", false);
    tb_server = prefs.getString("tb_server", "");
    tb_port = prefs.getInt("tb_port", 1883);
    tb_token = prefs.getString("tb_token", "");
    prefs.end();
}

void ThingsBoardManager::saveToNVS() {
    Preferences prefs;
    prefs.begin("thingsboard", false); // Read-write
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
    Serial.println("Attempting ThingsBoard MQTT connection...");
    client.setServer(tb_server.c_str(), tb_port);
    if (client.connect("HydroControl-Gateway", tb_token.c_str(), NULL)) {
        Serial.println("ThingsBoard MQTT connected");
    } else {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(client.state());
    }
}

void ThingsBoardManager::loop() {
    if (enabled && WiFi.isConnected()) {
        if (!client.connected()) {
            unsigned long now = millis();
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
    JsonArray ts_values = deviceData.createNestedArray("ts");

    JsonObject ts_obj = ts_values.createNestedObject();
    ts_obj["ts"] = millis(); // Add timestamp
    JsonObject values_obj = ts_obj.createNestedObject("values");

    // Copy data to the 'values' object
    for (JsonPairConst kvp : data.as<JsonObjectConst>()) {
        values_obj[kvp.key()] = kvp.value();
    }

    String telemetry;
    serializeJson(doc, telemetry);

    if (client.publish("v1/gateway/telemetry", telemetry.c_str())) {
        Serial.println("Telemetry sent to ThingsBoard for " + deviceName);
    } else {
        Serial.println("Failed to send telemetry to ThingsBoard");
    }
}

// --- Getters ---
bool ThingsBoardManager::isEnabled() const { return enabled; }
String ThingsBoardManager::getServer() const { return tb_server; }
int ThingsBoardManager::getPort() const { return tb_port; }
String ThingsBoardManager::getToken() const { return tb_token; }
