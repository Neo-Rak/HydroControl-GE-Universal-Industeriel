#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "ThingsBoardManager.hh"

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastReconnectAttempt = 0;

void ThingsBoardManager::begin() {
    // Initialization will be done when credentials are set
}

void ThingsBoardManager::updateCredentials(const String& server, int port, const String& token) {
    tb_server = server;
    tb_port = port;
    tb_token = token;
    if (enabled && WiFi.isConnected()) {
        client.setServer(tb_server.c_str(), tb_port);
    }
}

void ThingsBoardManager::setEnabled(bool en) {
    enabled = en;
    if (!enabled && client.connected()) {
        client.disconnect();
    }
}

void ThingsBoardManager::reconnect() {
    if (!enabled || tb_server.isEmpty() || tb_token.isEmpty()) {
        return;
    }

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
                // Attempt to reconnect
                reconnect();
            }
        } else {
            client.loop();
        }
    }
}

void ThingsBoardManager::sendTelemetry(const JsonDocument& doc) {
    if (!enabled || !client.connected()) {
        return;
    }

    String telemetry;
    serializeJson(doc, telemetry);

    if (client.publish("v1/gateway/telemetry", telemetry.c_str())) {
        Serial.println("Telemetry sent to ThingsBoard");
    } else {
        Serial.println("Failed to send telemetry to ThingsBoard");
    }
}
