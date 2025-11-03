#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

typedef void (*LoRaMessageCallback)(JsonDocument& doc);

class LoRaManager {
public:
    bool begin();
    void loop();
    void send(const JsonDocument& doc);
    void setOnReceive(LoRaMessageCallback callback);

private:
    static void onReceive(int packetSize);
    String encrypt(const String& plaintext);
    String decrypt(const String& ciphertext);

    LoRaMessageCallback messageCallback = nullptr;
};
