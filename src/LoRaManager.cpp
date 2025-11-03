#include "LoRaManager.h"
#include "config.h"
#include <LoRa.h>
#include <Crypto.h>
#include <AES.h>
#include <string.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// --- Static variables ---
static LoRaMessageCallback messageCallback_static = nullptr;
static std::vector<uint8_t> rxBuffer;
static SemaphoreHandle_t packetSemaphore;

// --- AES-128 CBC Encryption ---
static AES128 aes;
static byte key[16];
static byte iv[16];

void IRAM_ATTR LoRaManager_onReceive(int packetSize) {
    if (packetSize == 0) return;

    rxBuffer.clear();
    for (int i = 0; i < packetSize; i++) {
        rxBuffer.push_back(LoRa.read());
    }

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(packetSemaphore, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

bool LoRaManager::begin() {
    packetSemaphore = xSemaphoreCreateBinary();

    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(433E6)) {
        return false;
    }

    strncpy((char*)key, LORA_ENCRYPTION_KEY, 16);
    memset(iv, 0, 16);
    aes.setKey(key, 16);

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
    if (xSemaphoreTake(packetSemaphore, pdMS_TO_TICKS(10)) == pdTRUE) {
        String received = "";
        for (uint8_t byte : rxBuffer) {
            received += (char)byte;
        }

        if (messageCallback_static != nullptr) {
            String decrypted = decrypt(received);
            JsonDocument doc;
            if (deserializeJson(doc, decrypted) == DeserializationError::Ok) {
                messageCallback_static(doc);
            }
        }
    }
}

String LoRaManager::encrypt(const String& plaintext) {
    int len = plaintext.length();
    int paddedLen = len + (16 - (len % 16));
    std::vector<byte> plain(paddedLen);
    std::vector<byte> cipher(paddedLen);

    memcpy(plain.data(), plaintext.c_str(), len);
    memset(plain.data() + len, 0, paddedLen - len);

    aes.setKey(key, 16);
    aes.setIV(iv, 16);
    aes.encrypt(cipher.data(), plain.data(), paddedLen);

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
    if (len == 0 || len % 16 != 0) return "";

    std::vector<byte> cipher(len);
    std::vector<byte> plain(len);

    for (int i = 0; i < len; i++) {
        sscanf(ciphertext.substring(i*2, i*2+2).c_str(), "%02x", &cipher[i]);
    }

    aes.setKey(key, 16);
    aes.setIV(iv, 16);
    aes.decrypt(plain.data(), cipher.data(), len);

    return String((char*)plain.data());
}
