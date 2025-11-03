#include <LoRa.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include "LoRaManager.h"
#include "config.h"

// --- Static variables ---
static LoRaMessageCallback messageCallback_static = nullptr;
static std::vector<String> receivedPackets;
static SemaphoreHandle_t packetMutex;

// --- AES-128 CBC Encryption ---
static AES128 aes;
static byte key[16];
static byte iv[16]; // Initialization Vector

void LoRaManager_onReceive(int packetSize) {
    if (packetSize == 0) return;
    String received = "";
    while (LoRa.available()) {
        received += (char)LoRa.read();
    }

    if (xSemaphoreTake(packetMutex, portMAX_DELAY) == pdTRUE) {
        receivedPackets.push_back(received);
        xSemaphoreGive(packetMutex);
    }
}

bool LoRaManager::begin() {
    packetMutex = xSemaphoreCreateMutex();

    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(433E6)) { // 433MHz frequency
        Serial.println("Starting LoRa failed!");
        return false;
    }

    // Setup AES encryption key
    strncpy((char*)key, LORA_ENCRYPTION_KEY, 16);
    memset(iv, 0, 16); // Use a zero IV for simplicity. For higher security, a random or counter-based IV is better.
    aes.setKey(key, 16);

    // Set up the receive handler
    LoRa.onReceive(LoRaManager_onReceive);
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
    messageCallback_static = callback;
}

void LoRaManager::loop() {
    if (uxSemaphoreGetCount(packetMutex) > 0) { // Check if there are packets without blocking
        String packet = "";
        if (xSemaphoreTake(packetMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!receivedPackets.empty()) {
                packet = receivedPackets.front();
                receivedPackets.erase(receivedPackets.begin());
            }
            xSemaphoreGive(packetMutex);
        }

        if (packet.length() > 0 && messageCallback_static != nullptr) {
            String decrypted = decrypt(packet);

            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, decrypted);
            if (error) {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.c_str());
                return;
            }
            messageCallback_static(doc);
        }
    }
}

String LoRaManager::encrypt(const String& plaintext) {
    int len = plaintext.length();
    int paddedLen = len + (16 - (len % 16)); // Pad to multiple of 16
    byte plain[paddedLen];
    byte cipher[paddedLen];

    plaintext.getBytes(plain, paddedLen);
    memset(plain + len, 0, paddedLen - len); // Zero padding

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
    if (len == 0) return "";

    byte cipher[len];
    byte plain[len];

    for (int i = 0; i < len; i++) {
        sscanf(ciphertext.substring(i*2, i*2+2).c_str(), "%02x", &cipher[i]);
    }

    aes.setIV(iv, 16);
    aes.decrypt(plain, cipher, len);

    return String((char*)plain);
}
