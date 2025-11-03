#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// Define a callback function type for received messages
typedef void (*LoRaMessageCallback)(JsonDocument& doc);

class LoRaManager {
public:
    bool begin();
    void send(const JsonDocument& doc);
    void setOnReceive(LoRaMessageCallback callback);
    void loop(); // Method to be called repeatedly to process incoming packets

private:
    static void onReceive(int packetSize);
    String encrypt(const String& plaintext);
    String decrypt(const String& ciphertext);

    static LoRaManager* instance;
    LoRaMessageCallback messageCallback;

    // A queue to hold received messages for processing in the main loop
    static std::vector<String> receivedPackets;
};
