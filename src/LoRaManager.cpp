#include <LoRa.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include "LoRaManager.h"
#include "config.h"

LoRaManager* LoRaManager::instance = nullptr;
std::vector<String> LoRaManager::receivedPackets;

// AES-128 CBC Encryption
static AES128 aes;
static byte key[16];
static byte iv[16];

bool LoRaManager::begin() {
    instance = this;

    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(433E6)) {
        Serial.println("Starting LoRa failed!");
        return false;
    }

    strncpy((char*)key, LORA_ENCRYPTION_KEY, 16);
    memset(iv, 0, 16);
    aes.setKey(key, 16);

    LoRa.onReceive(onReceive);
    LoRa.receive();

    return true;
}

void LoRaManager::send(const JsonDocument& doc) {
    String jsonString;
    serializeJson(doc, jsonString);
    String encryptedString = encrypt(jsonString);
    LoRa.beginPacket();
    LoRa.print(encryptedString);
    LoRa.endPacket();
}

void LoRaManager::setOnReceive(LoRaMessageCallback callback) {
    messageCallback = callback;
}

void LoRaManager::loop() {
    // Process received packets from the queue
    if (!receivedPackets.empty()) {
        String packet = receivedPackets.front();
        receivedPackets.erase(receivedPackets.begin());

        String decrypted = decrypt(packet);

        if (messageCallback != nullptr) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, decrypted);
            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.c_str());
                return;
            }
            messageCallback(doc);
        }
    }
}

void LoRaManager::onReceive(int packetSize) {
    if (packetSize == 0) return;
    String received = "";
    while (LoRa.available()) {
        received += (char)LoRa.read();
    }
    // Add the received packet to the queue for later processing
    receivedPackets.push_back(received);
}

String LoRaManager::encrypt(const String& plaintext) {
    int len = plaintext.length();
    int paddedLen = len + (16 - (len % 16));
    byte plain[paddedLen];
    byte cipher[paddedLen];
    plaintext.getBytes(plain, paddedLen);
    memset(plain + len, 0, paddedLen - len);
    aes.setIV(iv, 16);
    aes.encrypt(cipher, plain, paddedLen);
    String encoded = "";
    for (int i = 0; i < paddedLen; i++) {
        char hex[3];
        sprintf(hex, "%02x", cipher[i]);
        encoded += hex;
    }
    return encoded;
}

String LoRaManager::decrypt(const String& ciphertext) {
    int len = ciphertext.length() / 2;
    byte cipher[len];
    byte plain[len];
    for (int i = 0; i < len; i++) {
        sscanf(ciphertext.substring(i*2, i*2+2).c_str(), "%02x", &cipher[i]);
    }
    aes.setIV(iv, 16);
    aes.decrypt(plain, cipher, len);
    return String((char*)plain);
}
