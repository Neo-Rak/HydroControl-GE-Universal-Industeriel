#ifndef LORAMANAGER_H
#define LORAMANAGER_H

#include <ArduinoJson.h>
#include "mbedtls/aes.h"

typedef void (*LoRaMessageCallback)(JsonDocument& doc);

class LoRaManager {
public:
    bool begin();
    void send(JsonDocument& doc);
    void setOnReceive(LoRaMessageCallback callback);
    void loop();

private:
    String encrypt(const String& plaintext);
    String decrypt(const String& ciphertext);

    mbedtls_aes_context aes_ctx;
};

#endif // LORAMANAGER_H
