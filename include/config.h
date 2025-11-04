#pragma once

// -- IDENTIFICATION ET SÉCURITÉ --
// La clé de chiffrement est maintenant stockée en NVS

// -- BROCHAGE MATÉRIEL COMMUN --
// Module LoRa (RFM95/SX127x) - SPI
#define LORA_SCK_PIN   18
#define LORA_MISO_PIN  19
#define LORA_MOSI_PIN  23
#define LORA_SS_PIN    5
#define LORA_RST_PIN   14
#define LORA_DIO0_PIN  2

// LEDs de statut
#define LED_RED_PIN    15 // ERREUR
#define LED_GREEN_PIN  16 // OK / ACTION
#define LED_BLUE_PIN   17 // COMMUNICATION

// -- BROCHAGE SPÉCIFIQUE AU RÔLE --
// Ces broches ont une fonction différente selon le rôle.
#define ROLE_PIN_1    25 // AquaReservPro: Capteur Niveau / WellguardPro: Commande Relais
#define ROLE_PIN_2    26 // AquaReservPro: Bouton Manuel / WellguardPro: Bouton Test
#define ROLE_PIN_3    27 // AquaReservPro: Non utilisé / WellguardPro: Capteur Défaut
