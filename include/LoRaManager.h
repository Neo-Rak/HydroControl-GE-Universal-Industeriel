#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <ArduinoJson.h>
#include "mbedtls/aes.h"

typedef void (*LoRaMessageCallback)(JsonDocument& doc);

class LoRaManager {
public:
    bool begin();
    void setEncryptionKey(const char* key);
    void send(JsonDocument& doc);
    void setOnReceive(LoRaMessageCallback callback);
    void loop();

private:
    String encrypt(const String& plaintext);
    String decrypt(const String& ciphertext);

    mbedtls_aes_context aes_ctx;
    unsigned char _key[16];
    bool _keyIsSet;
};

#endif // LORAMANAGER_H
