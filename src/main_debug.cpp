// ============================================================
//  ESP32-C3 Debug-Monitor via ESP-NOW
//  src_debug/main.cpp
//
//  Firmware fuer den DRITTEN ESP32-C3, der per USB an einen PC
//  angeschlossen wird und Debug-Daten von den beiden Bridge-ESPs
//  empfaengt und auf der USB-Serial-Konsole ausgibt.
//
//  Datenfluss (nur Empfang, kein Senden):
//    ESP-A ──(ESP-NOW PKT_DEBUG)──►  Debug-Monitor ──(USB)──► PC
//    ESP-B ──(ESP-NOW PKT_DEBUG)──►  Debug-Monitor ──(USB)──► PC
//
//  Die normale Bridge-Kommunikation (PKT_DATA) wird ignoriert.
//  Der Debug-Monitor sendet KEINE Daten an die Bridge-ESPs.
//
//  Hardware: Seeed Studio XIAO ESP32-C3
//  Anschluss: USB-C an PC
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "config.h"

// ============================================================
//  Globale Variablen
// ============================================================
uint8_t g_myMac[6] = {0};
uint32_t g_pktCount = 0;

// ============================================================
//  Hilfsfunktionen
// ============================================================

/** MAC-Array  ->  String "AA:BB:CC:DD:EE:FF" */
String macToStr(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// ============================================================
//  ESP-NOW Empfangs-Callback
//
//  Nur PKT_DEBUG Pakete werden verarbeitet. Alle anderen
//  Pakettypen (DATA, HEARTBEAT, DISCOVERY etc.) werden ignoriert.
//  Die Debug-Daten werden mit der Quell-MAC auf USB-Serial
//  ausgegeben.
// ============================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *inData, int len) {
    const uint8_t *mac = recv_info->src_addr;
#else
void onDataRecv(const uint8_t *mac, const uint8_t *inData, int len) {
#endif
    if (len < PKT_HEADER_SIZE) return;
 
    const uint8_t *pktData = inData;
    uint8_t  pktType    = pktData[0];
    // uint16_t pktSeq  = pktData[1] | (pktData[2] << 8);  // nicht benoetigt
    uint16_t pktDataLen = pktData[3] | (pktData[4] << 8);

    // Nur Debug-Pakete verarbeiten
    if (pktType != PKT_DEBUG) return;
    if (pktDataLen == 0 || pktDataLen > ESPNOW_MAX_PAYLOAD) return;

    g_pktCount++;

    // Quell-MAC als Prefix ausgeben
    Serial.print('[');
    Serial.print(macToStr(mac));
    Serial.print(F("] "));

    // Debug-Daten ausgeben
    Serial.write(pktData + PKT_HEADER_SIZE, pktDataLen);
    Serial.println();
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(USB_SERIAL_BAUD);
    Serial.setTxTimeoutMs(0);
    while(!Serial.available()){}
    delay(6000);

    // LED-Pins (optional, Status-Anzeige)
    pinMode(PIN_LED_CONNECTED, OUTPUT);
    pinMode(PIN_LED_SETUP,     OUTPUT);
    digitalWrite(PIN_LED_CONNECTED, LOW);
    digitalWrite(PIN_LED_SETUP,     LOW);

    Serial.println();
    Serial.println(F("========================================="));
    Serial.println(F("  ESP32-C3 Debug-Monitor via ESP-NOW"));
    Serial.println(F("  Firmware v1.2"));
    Serial.println(F("========================================="));

    // WiFi im Station-Modus initialisieren
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    WiFi.macAddress(g_myMac);
    Serial.print(F("[INFO] Eigene MAC: "));
    Serial.println(macToStr(g_myMac));
    Serial.print(F("[INFO] WiFi-Kanal: "));
    Serial.println(ESPNOW_CHANNEL);

    // ESP-NOW initialisieren
    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[FATAL] ESP-NOW Init fehlgeschlagen! Neustart in 2 s..."));
        delay(2000);
        ESP.restart();
    }

    // Nur Empfangs-Callback registrieren (kein Senden)
    esp_now_register_recv_cb(onDataRecv);

    Serial.println(F("[INFO] ESP-NOW bereit - warte auf Debug-Daten..."));
    Serial.println(F("[INFO] Debug-Daten erscheinen als: [MAC] nachricht"));
    Serial.println(F("========================================="));

    // LED einschalten als Betriebs-Indikator
    digitalWrite(PIN_LED_CONNECTED, HIGH);
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // Keine aktive Verarbeitung noetig - alles laeuft im
    // ESP-NOW Empfangs-Callback.
    delay(10);
}
