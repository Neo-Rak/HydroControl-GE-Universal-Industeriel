#include "LoRaManager.h"
#include "config.h"
#include <LoRa.h>
#include <string.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "mbedtls/base64.h"
#include "esp_random.h"

#define LORA_RX_BUFFER_SIZE 256

// --- Static variables ---
static LoRaMessageCallback messageCallback_static = nullptr;
static QueueHandle_t rxQueue;
static uint8_t rxBuffer[LORA_RX_BUFFER_SIZE];

void IRAM_ATTR LoRaManager_onReceive(int packetSize) {
    if (packetSize == 0 || packetSize > LORA_RX_BUFFER_SIZE) return;

    int bytesRead = 0;
    while (LoRa.available() && bytesRead < LORA_RX_BUFFER_SIZE) {
        rxBuffer[bytesRead++] = LoRa.read();
    }

    if (bytesRead > 0) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(rxQueue, &bytesRead, &xHigherPriorityTaskWoken);
        if (xHigherPriorityTaskWoken) {
            portYIELD_FROM_ISR();
        }
    }
}

bool LoRaManager::begin() {
    _keyIsSet = false;
    rxQueue = xQueueCreate(10, sizeof(int));

    LoRa.setPins(LORA_SS_PIN, LORA_RST_PIN, LORA_DIO0_PIN);
    if (!LoRa.begin(433E6)) {
        return false;
    }

    mbedtls_aes_init(&aes_ctx);

    LoRa.onReceive(LoRaManager_onReceive);
    LoRa.receive();

    return true;
}

void LoRaManager::setEncryptionKey(const char* key) {
    memcpy(_key, key, 16);
    _keyIsSet = true;
}

void LoRaManager::send(JsonDocument& doc) {
    String jsonString;
    serializeJson(doc, jsonString);
    String encryptedString = encrypt(jsonString);

    if (encryptedString.length() > 0) {
        LoRa.beginPacket();
        LoRa.print(encryptedString);
        LoRa.endPacket();
    }
}

void LoRaManager::setOnReceive(LoRaMessageCallback callback) {
    messageCallback_static = callback;
}

void LoRaManager::loop() {
    int packetSize;
    if (xQueueReceive(rxQueue, &packetSize, pdMS_TO_TICKS(10)) == pdTRUE) {
        String received = "";
        for(int i = 0; i < packetSize; i++) {
            received += (char)rxBuffer[i];
        }

        if (messageCallback_static != nullptr) {
            String decrypted = decrypt(received);
            if (decrypted.length() > 0) {
                JsonDocument doc;
                if (deserializeJson(doc, decrypted) == DeserializationError::Ok) {
                    messageCallback_static(doc);
                }
            }
        }
    }
}

String LoRaManager::encrypt(const String& plaintext) {
    if (!_keyIsSet) return "";

    mbedtls_aes_setkey_enc(&aes_ctx, _key, 128);

    unsigned char iv[16];
    esp_fill_random(iv, 16);

    size_t input_len = plaintext.length();
    size_t padded_len = input_len + (16 - (input_len % 16));
    unsigned char padded_input[padded_len];

    memcpy(padded_input, plaintext.c_str(), input_len);

    unsigned char padding_val = 16 - (input_len % 16);
    for(size_t i = input_len; i < padded_len; i++) {
        padded_input[i] = padding_val;
    }

    unsigned char encrypted[padded_len];
    mbedtls_aes_crypt_cbc(&aes_ctx, MBEDTLS_AES_ENCRYPT, padded_len, iv, padded_input, encrypted);

    unsigned char combined[16 + padded_len];
    memcpy(combined, iv, 16);
    memcpy(combined + 16, encrypted, padded_len);

    size_t encoded_len;
    mbedtls_base64_encode(NULL, 0, &encoded_len, combined, sizeof(combined));
    unsigned char encoded[encoded_len];
    mbedtls_base64_encode(encoded, encoded_len, &encoded_len, combined, sizeof(combined));

    return String((char*)encoded);
}

String LoRaManager::decrypt(const String& ciphertext_b64) {
    if (!_keyIsSet) return "";

    size_t decoded_len;
    mbedtls_base64_decode(NULL, 0, &decoded_len, (const unsigned char*)ciphertext_b64.c_str(), ciphertext_b64.length());
    unsigned char decoded[decoded_len];
    int ret = mbedtls_base64_decode(decoded, decoded_len, &decoded_len, (const unsigned char*)ciphertext_b64.c_str(), ciphertext_b64.length());

    if (ret != 0 || decoded_len < 16) return "";

    unsigned char iv[16];
    memcpy(iv, decoded, 16);

    size_t encrypted_len = decoded_len - 16;
    if (encrypted_len % 16 != 0) return "";

    unsigned char encrypted[encrypted_len];
    memcpy(encrypted, decoded + 16, encrypted_len);

    unsigned char decrypted[encrypted_len];

    mbedtls_aes_context dec_ctx;
    mbedtls_aes_init(&dec_ctx);
    mbedtls_aes_setkey_dec(&dec_ctx, _key, 128);
    mbedtls_aes_crypt_cbc(&dec_ctx, MBEDTLS_AES_DECRYPT, encrypted_len, iv, encrypted, decrypted);
    mbedtls_aes_free(&dec_ctx);

    unsigned char padding_val = decrypted[encrypted_len - 1];
    if(padding_val > 16) return "";

    return String((char*)decrypted, encrypted_len - padding_val);
}
