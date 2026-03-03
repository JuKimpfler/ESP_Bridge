// ============================================================
//  ESP32-C3 UART-Bridge via ESP-NOW
//  src/main.cpp
//
//  Firmware ist für BEIDE Module identisch.
//  Unterschied entsteht nur durch die gespeicherte Peer-MAC.
//
//  Hardware: Seeed Studio XIAO ESP32-C3
//  Pins:
//    GPIO 5   – Setup-Modus-Eingang (HIGH = Setup aktiv)
//    GPIO 9   – LED Verbindungsstatus  (ACHTUNG: = BOOT-Taste!)
//    GPIO 10  – LED Setup-Modus
//    GPIO 20  – UART1 RX  (D7)
//    GPIO 21  – UART1 TX  (D6)
// ============================================================

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
#include "config.h"

// ============================================================
//  Paket-Struktur
// ============================================================
#pragma pack(push, 1)
struct EspNowPacket {
    uint8_t  type;                      // PKT_*
    uint16_t seq;                       // Sequenznummer
    uint16_t dataLen;                   // Nutzdatenlänge
    uint8_t  data[ESPNOW_MAX_PAYLOAD];  // Nutzdaten
};
#pragma pack(pop)

// Größe des Headers ohne Daten
#define PKT_HEADER_SIZE  5  // type(1) + seq(2) + dataLen(2)

// ============================================================
//  Globale Variablen
// ============================================================

// --- NVS ---
Preferences prefs;

// --- Status ---
bool     g_setupMode     = false;
bool     g_peerConnected = false;
bool     g_peerStored    = false;

// --- MAC-Adressen ---
uint8_t  g_myMac[6]        = {0};
uint8_t  g_peerMac[6]      = {0};
uint8_t  g_broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// --- UART-Empfangspuffer ---
uint8_t  g_uartBuf[UART_RX_BUF_SIZE];
uint16_t g_uartBufLen  = 0;
uint32_t g_lastSendMs  = 0;

// --- Verbindungs-Timeout ---
uint32_t g_lastRxMs       = 0;     // Letzter empfangener ESP-NOW-Frame
uint8_t  g_sendFailCount  = 0;     // Aufeinanderfolgende Sendefehler
#define  SEND_FAIL_THRESHOLD  5     // Ab hier → nicht verbunden

// --- Heartbeat ---
uint32_t g_lastHeartbeatMs = 0;

// --- LED ---
uint32_t g_lastLedBlinkMs  = 0;
bool     g_ledBlinkState   = false;

// --- Sequenznummer ---
uint16_t g_txSeq = 0;

// --- Setup-Modus ---
bool     g_setupPinPrev   = false; // Flanken-Erkennung
uint32_t g_lastScanBcastMs = 0;
uint8_t  g_scanBcastSent  = 0;
bool     g_scanActive     = false;
uint32_t g_scanStartMs    = 0;

struct FoundPeer {
    uint8_t mac[6];
};
FoundPeer g_foundPeers[MAX_FOUND_PEERS];
uint8_t   g_foundPeerCount = 0;

// --- Kommandopuffer (USB-Serial) ---
char    g_cmdBuf[CMD_BUF_SIZE];
uint8_t g_cmdLen = 0;

// ============================================================
//  Hilfsfunktionen
// ============================================================

/** MAC-Array  →  String "AA:BB:CC:DD:EE:FF" */
String macToStr(const uint8_t *mac) {
    char buf[18];
    snprintf(buf, sizeof(buf),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

/** String "AA:BB:CC:DD:EE:FF"  →  MAC-Array, gibt true bei Erfolg */
bool strToMac(const char *str, uint8_t *mac) {
    unsigned int v[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
    return true;
}

/** Gibt den aktuellen Status auf USB-Serial aus */
void printStatus() {
    Serial.println(F("=== ESP-NOW UART Bridge – Status ==="));
    Serial.print  (F("  Firmware:      ")); Serial.println(F("v1.0"));
    Serial.print  (F("  Eigene MAC:    ")); Serial.println(macToStr(g_myMac));
    Serial.print  (F("  Peer MAC:      "));
    if (g_peerStored) Serial.println(macToStr(g_peerMac));
    else              Serial.println(F("(nicht gesetzt)"));
    Serial.print  (F("  Verbunden:     ")); Serial.println(g_peerConnected ? F("JA") : F("NEIN"));
    Serial.print  (F("  Modus:         ")); Serial.println(g_setupMode    ? F("SETUP") : F("NORMAL"));
    Serial.print  (F("  WiFi-Kanal:    ")); Serial.println(ESPNOW_CHANNEL);
    Serial.print  (F("  UART Baud:     ")); Serial.println(HW_UART_BAUD);
    Serial.println(F("===================================="));
}

// ============================================================
//  ESP-NOW Peer-Verwaltung
// ============================================================

bool addEspNowPeer(const uint8_t *mac, uint8_t channel = ESPNOW_CHANNEL) {
    if (esp_now_is_peer_exist(mac)) return true;
    esp_now_peer_info_t info = {};
    memcpy(info.peer_addr, mac, 6);
    info.channel = channel;
    info.encrypt = false;
    if (esp_now_add_peer(&info) == ESP_OK) {
        Serial.print(F("[ESP-NOW] Peer hinzugefügt: "));
        Serial.println(macToStr(mac));
        return true;
    }
    Serial.println(F("[ESP-NOW] Fehler: Peer konnte nicht hinzugefügt werden"));
    return false;
}

void removeEspNowPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
}

// ============================================================
//  ESP-NOW  –  Sende-Callback
// ============================================================
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        g_sendFailCount = 0;
        if (!g_peerConnected && g_peerStored) {
            g_peerConnected = true;
            Serial.println(F("[INFO] Peer erfolgreich erreicht → VERBUNDEN"));
        }
    } else {
        g_sendFailCount++;
        if (g_sendFailCount >= SEND_FAIL_THRESHOLD) {
            if (g_peerConnected) {
                g_peerConnected = false;
                Serial.println(F("[WARN] Mehrere Sendefehler → VERBINDUNG VERLOREN"));
            }
        }
    }
}

// ============================================================
//  ESP-NOW  –  Empfangs-Callback
//  Kompatibel mit Arduino ESP32 Core 2.x und 3.x
// ============================================================
#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *inData, int len) {
    const uint8_t *mac = recv_info->src_addr;
#else
void onDataRecv(const uint8_t *mac, const uint8_t *inData, int len) {
#endif
    if (len < PKT_HEADER_SIZE) return;

    const EspNowPacket *pkt = reinterpret_cast<const EspNowPacket*>(inData);
    g_lastRxMs = millis();

    // --------------------------------------------------------
    //  DATA-Paket:  Daten an Hardware-UART + Debug ausgeben
    // --------------------------------------------------------
    if (pkt->type == PKT_DATA) {
        if (pkt->dataLen > 0 && pkt->dataLen <= ESPNOW_MAX_PAYLOAD) {
            Serial1.write(pkt->data, pkt->dataLen);
            // Debug: auch auf USB ausgeben
            Serial.print(F("[RX ESP-NOW→UART] "));
            Serial.print(pkt->dataLen);
            Serial.print(F(" B  seq="));
            Serial.print(pkt->seq);
            Serial.print(F("  Daten: "));
            // Hex-Dump (max 16 Byte für Übersichtlichkeit)
            uint16_t showLen = min((uint16_t)16, pkt->dataLen);
            for (uint16_t i = 0; i < showLen; i++) {
                if (pkt->data[i] < 0x10) Serial.print('0');
                Serial.print(pkt->data[i], HEX);
                Serial.print(' ');
            }
            if (pkt->dataLen > 16) Serial.print(F("..."));
            Serial.println();
        }

        // Peer als verbunden markieren
        if (!g_peerConnected) {
            g_peerConnected = true;
            g_sendFailCount = 0;
            Serial.println(F("[INFO] Paket empfangen → VERBUNDEN"));
        }

        // Unbekannter Peer: automatisch ergänzen (nur wenn kein Peer gespeichert)
        if (!g_peerStored) {
            memcpy(g_peerMac, mac, 6);
            g_peerStored = true;
            addEspNowPeer(g_peerMac);
            Serial.print(F("[INFO] Peer auto-erkannt: "));
            Serial.println(macToStr(g_peerMac));
        }
    }

    // --------------------------------------------------------
    //  DISCOVERY-Paket: Antworten
    // --------------------------------------------------------
    else if (pkt->type == PKT_DISCOVERY) {
        Serial.print(F("[SETUP] Discovery-Anfrage von: "));
        Serial.println(macToStr(mac));

        // Temporären Peer hinzufügen, um antworten zu können
        addEspNowPeer(mac);

        EspNowPacket resp = {};
        resp.type    = PKT_DISC_RESP;
        resp.seq     = 0;
        resp.dataLen = 0;
        esp_now_send(mac, reinterpret_cast<uint8_t*>(&resp), PKT_HEADER_SIZE);
    }

    // --------------------------------------------------------
    //  DISCOVERY-RESPONSE: In der Scan-Liste speichern
    // --------------------------------------------------------
    else if (pkt->type == PKT_DISC_RESP) {
        if (!g_scanActive) return;

        // Duplikat prüfen
        for (uint8_t i = 0; i < g_foundPeerCount; i++) {
            if (memcmp(g_foundPeers[i].mac, mac, 6) == 0) return;
        }
        if (g_foundPeerCount < MAX_FOUND_PEERS) {
            memcpy(g_foundPeers[g_foundPeerCount++].mac, mac, 6);
            Serial.print(F("[SCAN] Gefunden #"));
            Serial.print(g_foundPeerCount);
            Serial.print(F(": "));
            Serial.println(macToStr(mac));
        }
    }

    // --------------------------------------------------------
    //  HEARTBEAT: Mit ACK antworten
    // --------------------------------------------------------
    else if (pkt->type == PKT_HEARTBEAT) {
        if (!esp_now_is_peer_exist(mac)) addEspNowPeer(mac);
        EspNowPacket ack = {};
        ack.type    = PKT_HEARTBEAT_ACK;
        ack.seq     = pkt->seq;
        ack.dataLen = 0;
        esp_now_send(mac, reinterpret_cast<uint8_t*>(&ack), PKT_HEADER_SIZE);
    }

    // --------------------------------------------------------
    //  HEARTBEAT-ACK: Verbindung bestätigt
    // --------------------------------------------------------
    else if (pkt->type == PKT_HEARTBEAT_ACK) {
        if (!g_peerConnected) {
            g_peerConnected = true;
            g_sendFailCount = 0;
            Serial.println(F("[INFO] Heartbeat-ACK → VERBUNDEN"));
        }
        g_sendFailCount = 0;
    }
}

// ============================================================
//  ESP-NOW initialisieren
// ============================================================
void initEspNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Kanal erzwingen (beide Module müssen den gleichen Kanal nutzen)
    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    // Eigene MAC lesen
    WiFi.macAddress(g_myMac);
    Serial.print(F("[INFO] Eigene MAC: "));
    Serial.println(macToStr(g_myMac));

    if (esp_now_init() != ESP_OK) {
        Serial.println(F("[FATAL] ESP-NOW Init fehlgeschlagen! Neustart in 2 s..."));
        delay(2000);
        ESP.restart();
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // Broadcast-Peer für Discovery
    addEspNowPeer(g_broadcastMac);

    Serial.println(F("[INFO] ESP-NOW bereit"));
}

// ============================================================
//  NVS: Einstellungen laden / speichern
// ============================================================
void loadSettings() {
    prefs.begin(NVS_NAMESPACE, true);  // read-only
    if (prefs.isKey(NVS_KEY_PEER_MAC)) {
        prefs.getBytes(NVS_KEY_PEER_MAC, g_peerMac, 6);
        g_peerStored = true;
        Serial.print(F("[NVS] Gespeicherte Peer-MAC: "));
        Serial.println(macToStr(g_peerMac));
    } else {
        Serial.println(F("[NVS] Kein Peer gespeichert."));
    }
    prefs.end();
}

void saveSettings() {
    prefs.begin(NVS_NAMESPACE, false);  // read-write
    if (g_peerStored) {
        prefs.putBytes(NVS_KEY_PEER_MAC, g_peerMac, 6);
    }
    prefs.end();
    Serial.println(F("[NVS] Einstellungen gespeichert."));
}

void clearSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    memset(g_peerMac, 0, 6);
    g_peerStored    = false;
    g_peerConnected = false;
    Serial.println(F("[NVS] Alle Einstellungen gelöscht."));
}

// ============================================================
//  Setup-Modus: Betreten / Verlassen
// ============================================================
void enterSetupMode() {
    if (g_setupMode) return;
    g_setupMode = true;
    digitalWrite(PIN_LED_SETUP, HIGH);

    Serial.println();
    Serial.println(F("========================================="));
    Serial.println(F("  *** SETUP-MODUS AKTIV ***"));
    Serial.println(F("========================================="));
    Serial.println(F("Verfügbare Befehle:"));
    Serial.println(F("  ET+SCAN          – Peers suchen"));
    Serial.println(F("  ET+LIST          – Gefundene Peers anzeigen"));
    Serial.println(F("  ET+SELECT=N      – Peer N aus Scan-Liste wählen"));
    Serial.println(F("  ET+PEER=AA:...:FF– Peer-MAC manuell setzen"));
    Serial.println(F("  ET+PEER?         – Gespeicherte Peer-MAC anzeigen"));
    Serial.println(F("  ET+MAC?          – Eigene MAC anzeigen"));
    Serial.println(F("  ET+STATUS?       – Vollständigen Status anzeigen"));
    Serial.println(F("  ET+CHANNEL=N     – WiFi-Kanal setzen (1–13, Neustart)"));
    Serial.println(F("  ET+RESET         – Peer-Einstellungen löschen"));
    Serial.println(F("  ET+SAVE          – Speichern & Setup beenden"));
    Serial.println(F("========================================="));
}

void exitSetupMode() {
    g_setupMode = false;
    digitalWrite(PIN_LED_SETUP, LOW);

    // Peer aktivieren falls gespeichert
    if (g_peerStored) {
        removeEspNowPeer(g_peerMac);  // Sicherheitshalber entfernen und neu hinzufügen
        addEspNowPeer(g_peerMac);
    }

    Serial.println(F("[SETUP] Setup beendet → Normaler Betrieb"));
    printStatus();
}

// ============================================================
//  Scan: Discovery-Broadcast absenden
// ============================================================
void startScan() {
    g_foundPeerCount   = 0;
    g_scanActive       = true;
    g_scanStartMs      = millis();
    g_lastScanBcastMs  = 0;
    g_scanBcastSent    = 0;
    Serial.print(F("[SCAN] Suche läuft ("));
    Serial.print(SCAN_DURATION_MS / 1000);
    Serial.println(F(" s)..."));
}

void processScan() {
    if (!g_scanActive) return;
    uint32_t now = millis();

    // Mehrere Broadcasts über die Suchdauer verteilen
    if (g_scanBcastSent < SCAN_BCAST_COUNT &&
        now - g_lastScanBcastMs >= SCAN_BCAST_INTERVAL) {
        g_lastScanBcastMs = now;
        g_scanBcastSent++;
        EspNowPacket pkt = {};
        pkt.type    = PKT_DISCOVERY;
        pkt.seq     = g_txSeq++;
        pkt.dataLen = 0;
        esp_now_send(g_broadcastMac, reinterpret_cast<uint8_t*>(&pkt), PKT_HEADER_SIZE);
        Serial.print(F("[SCAN] Broadcast "));
        Serial.print(g_scanBcastSent);
        Serial.print(F("/"));
        Serial.println(SCAN_BCAST_COUNT);
    }

    // Scan beendet
    if (now - g_scanStartMs >= SCAN_DURATION_MS) {
        g_scanActive = false;
        Serial.print(F("[SCAN] Abgeschlossen. "));
        Serial.print(g_foundPeerCount);
        Serial.println(F(" Peer(s) gefunden:"));
        for (uint8_t i = 0; i < g_foundPeerCount; i++) {
            Serial.print(F("  ["));
            Serial.print(i + 1);
            Serial.print(F("] "));
            Serial.println(macToStr(g_foundPeers[i].mac));
        }
        if (g_foundPeerCount > 0) {
            Serial.println(F("[SETUP] Tipp: ET+SELECT=1  oder  ET+PEER=<MAC>"));
            Serial.println(F("[SETUP] Dann: ET+SAVE"));
        } else {
            Serial.println(F("[SCAN] Kein Peer gefunden. Stellle sicher, dass der"));
            Serial.println(F("       andere ESP läuft und im gleichen Kanal ist."));
        }
    }
}

// ============================================================
//  AT-Befehle verarbeiten
// ============================================================
void processCommand(const char *rawCmd) {
    // Eingabe bereinigen (CR/LF/Leerzeichen am Ende entfernen)
    char cmd[CMD_BUF_SIZE];
    strncpy(cmd, rawCmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    int len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\r' || cmd[len-1] == '\n' || cmd[len-1] == ' '))
        cmd[--len] = '\0';
    if (len == 0) return;

    // Befehle NUR im Setup-Modus oder Status-Abfragen immer erlauben
    bool isQuery = (strncmp(cmd, "ET+STATUS?", 10) == 0 ||
                    strncmp(cmd, "ET+MAC?",     7) == 0);

    if (!g_setupMode && !isQuery) {
        Serial.println(F("[CMD] Nicht im Setup-Modus. GPIO5 HIGH → Setup-Modus."));
        return;
    }

    // Präfix prüfen
    if (strncmp(cmd, "ET+", 3) != 0) {
        Serial.println(F("[CMD] Unbekanntes Format. Befehle beginnen mit ET+"));
        return;
    }

    const char *body = cmd + 3;

    // ---- ET+SCAN ----
    if (strcmp(body, "SCAN") == 0) {
        startScan();
    }

    // ---- ET+LIST ----
    else if (strcmp(body, "LIST") == 0) {
        if (g_scanActive) {
            Serial.println(F("[SCAN] Scan läuft noch..."));
        } else {
            Serial.print(F("[SCAN] "));
            Serial.print(g_foundPeerCount);
            Serial.println(F(" Peer(s):"));
            for (uint8_t i = 0; i < g_foundPeerCount; i++) {
                Serial.print(F("  ["));
                Serial.print(i + 1);
                Serial.print(F("] "));
                Serial.println(macToStr(g_foundPeers[i].mac));
            }
        }
    }

    // ---- ET+SELECT=N ----
    else if (strncmp(body, "SELECT=", 7) == 0) {
        int idx = atoi(body + 7) - 1;
        if (idx >= 0 && idx < (int)g_foundPeerCount) {
            memcpy(g_peerMac, g_foundPeers[idx].mac, 6);
            g_peerStored = true;
            Serial.print(F("[SETUP] Peer gesetzt: "));
            Serial.println(macToStr(g_peerMac));
            Serial.println(F("[SETUP] Zum Speichern: ET+SAVE"));
        } else {
            Serial.println(F("[CMD] Ungültiger Index. Erst ET+SCAN ausführen."));
        }
    }

    // ---- ET+PEER=AA:BB:CC:DD:EE:FF ----
    else if (strncmp(body, "PEER=", 5) == 0) {
        uint8_t newMac[6];
        if (strToMac(body + 5, newMac)) {
            memcpy(g_peerMac, newMac, 6);
            g_peerStored = true;
            Serial.print(F("[SETUP] Peer-MAC gesetzt: "));
            Serial.println(macToStr(g_peerMac));
            Serial.println(F("[SETUP] Zum Speichern: ET+SAVE"));
        } else {
            Serial.println(F("[CMD] Ungültiges MAC-Format. Erwartet: AA:BB:CC:DD:EE:FF"));
        }
    }

    // ---- ET+PEER? ----
    else if (strcmp(body, "PEER?") == 0) {
        Serial.print(F("[INFO] Peer-MAC: "));
        if (g_peerStored) Serial.println(macToStr(g_peerMac));
        else              Serial.println(F("(nicht gesetzt)"));
    }

    // ---- ET+MAC? ----
    else if (strcmp(body, "MAC?") == 0) {
        Serial.print(F("[INFO] Eigene MAC: "));
        Serial.println(macToStr(g_myMac));
    }

    // ---- ET+STATUS? ----
    else if (strcmp(body, "STATUS?") == 0) {
        printStatus();
    }

    // ---- ET+CHANNEL=N ----
    else if (strncmp(body, "CHANNEL=", 8) == 0) {
        int ch = atoi(body + 8);
        if (ch >= 1 && ch <= 13) {
            // Kanal in NVS speichern; Änderung wird nach Neustart aktiv
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putUChar(NVS_KEY_CHANNEL, (uint8_t)ch);
            prefs.end();
            Serial.print(F("[SETUP] Kanal wird nach ET+SAVE/Neustart: "));
            Serial.println(ch);
            Serial.println(F("[WARN] Kanal-Änderung erfordert Neustart!"));
        } else {
            Serial.println(F("[CMD] Ungültiger Kanal. Bereich: 1–13"));
        }
    }

    // ---- ET+RESET ----
    else if (strcmp(body, "RESET") == 0) {
        clearSettings();
    }

    // ---- ET+SAVE ----
    else if (strcmp(body, "SAVE") == 0) {
        saveSettings();
        exitSetupMode();
    }

    // ---- Unbekannt ----
    else {
        Serial.print(F("[CMD] Unbekannter Befehl: "));
        Serial.println(cmd);
        Serial.println(F("[CMD] ET+STATUS? für Hilfe"));
    }
}

// ============================================================
//  Daten per ESP-NOW senden (mit Segmentierung)
// ============================================================
void sendEspNow(const uint8_t *data, uint16_t totalLen) {
    if (!g_peerStored) return;

    uint16_t offset = 0;
    while (offset < totalLen) {
        EspNowPacket pkt = {};
        pkt.type    = PKT_DATA;
        pkt.seq     = g_txSeq++;
        uint16_t chunk = min((uint16_t)ESPNOW_MAX_PAYLOAD, (uint16_t)(totalLen - offset));
        pkt.dataLen = chunk;
        memcpy(pkt.data, data + offset, chunk);

        esp_err_t result = esp_now_send(g_peerMac,
                                        reinterpret_cast<uint8_t*>(&pkt),
                                        PKT_HEADER_SIZE + chunk);
        if (result != ESP_OK) {
            Serial.print(F("[ERROR] ESP-NOW Sendefehler: 0x"));
            Serial.println(result, HEX);
            g_sendFailCount++;
        }

        offset += chunk;
        // Kurze Pause zwischen zwei Paketen (verhindert Queue-Overflow)
        if (offset < totalLen) delayMicroseconds(400);
    }
}

// ============================================================
//  LED-Zustand aktualisieren
// ============================================================
void updateLEDs() {
    // Setup-LED: statisch EIN im Setup-Modus
    digitalWrite(PIN_LED_SETUP, g_setupMode ? HIGH : LOW);

    // Verbindungs-LED
    if (g_peerConnected) {
        digitalWrite(PIN_LED_CONNECTED, HIGH);
    } else {
        // Blinken: 500 ms Periode
        uint32_t now = millis();
        if (now - g_lastLedBlinkMs >= 500) {
            g_lastLedBlinkMs = now;
            g_ledBlinkState  = !g_ledBlinkState;
            digitalWrite(PIN_LED_CONNECTED, g_ledBlinkState ? HIGH : LOW);
        }
    }
}

// ============================================================
//  Verbindungs-Timeout prüfen
// ============================================================
void checkConnectionTimeout() {
    if (!g_peerConnected) return;
    if (millis() - g_lastRxMs > MAX_IDLE_MS) {
        g_peerConnected = false;
        Serial.println(F("[WARN] Timeout: kein Signal → VERBINDUNG VERLOREN"));
    }
}

// ============================================================
//  Heartbeat senden
// ============================================================
void sendHeartbeat() {
    if (!g_peerStored || g_setupMode) return;
    if (millis() - g_lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
    g_lastHeartbeatMs = millis();

    EspNowPacket pkt = {};
    pkt.type    = PKT_HEARTBEAT;
    pkt.seq     = g_txSeq++;
    pkt.dataLen = 0;
    esp_now_send(g_peerMac, reinterpret_cast<uint8_t*>(&pkt), PKT_HEADER_SIZE);
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    // USB-Serial
    Serial.begin(USB_SERIAL_BAUD);
    delay(600);  // Warten bis USB-CDC bereit ist
    Serial.println();
    Serial.println(F("========================================="));
    Serial.println(F("  ESP32-C3 UART-Bridge via ESP-NOW v1.0"));
    Serial.println(F("========================================="));

    // Pins konfigurieren
    pinMode(PIN_SETUP_MODE,    INPUT);
    pinMode(PIN_LED_CONNECTED, OUTPUT);
    pinMode(PIN_LED_SETUP,     OUTPUT);
    digitalWrite(PIN_LED_CONNECTED, LOW);
    digitalWrite(PIN_LED_SETUP,     LOW);

    // Hardware-UART initialisieren
    Serial1.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);
    Serial.println(F("[INFO] Hardware-UART (Serial1) auf 115200 bereit"));
    Serial.print  (F("[INFO] RX: GPIO")); Serial.print  (HW_UART_RX_PIN);
    Serial.print  (F("  TX: GPIO")); Serial.println(HW_UART_TX_PIN);

    // Einstellungen aus NVS laden
    loadSettings();

    // ESP-NOW starten
    initEspNow();

    // Gespeicherten Peer hinzufügen
    if (g_peerStored) {
        addEspNowPeer(g_peerMac);
    } else {
        Serial.println(F("[WARN] Kein Peer konfiguriert."));
        Serial.println(F("[WARN] GPIO5 auf HIGH legen und ET+SCAN ausführen."));
    }

    // Startzeit für RX-Timeout setzen
    g_lastRxMs = millis();

    // Startup-Zustand prüfen (GPIO5)
    g_setupPinPrev = (digitalRead(PIN_SETUP_MODE) == HIGH);
    if (g_setupPinPrev) {
        enterSetupMode();
    } else {
        printStatus();
        Serial.println(F("[INFO] Normaler Betrieb. GPIO5 HIGH → Setup-Modus."));
    }
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // -------------------------------------------------------
    //  GPIO5 Flanken-Erkennung → Setup-Modus ein/aus
    // -------------------------------------------------------
    bool setupPinNow = (digitalRead(PIN_SETUP_MODE) == HIGH);
    if (setupPinNow && !g_setupPinPrev) {
        enterSetupMode();
    }
    // Hinweis: Verlassen nur per ET+SAVE (damit kein versehentliches Beenden)
    g_setupPinPrev = setupPinNow;

    // -------------------------------------------------------
    //  USB-Serial: Befehle lesen
    // -------------------------------------------------------
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_cmdLen > 0) {
                g_cmdBuf[g_cmdLen] = '\0';
                processCommand(g_cmdBuf);
                g_cmdLen = 0;
            }
        } else if (g_cmdLen < CMD_BUF_SIZE - 1) {
            g_cmdBuf[g_cmdLen++] = c;
        }
    }

    // -------------------------------------------------------
    //  Scan verarbeiten (läuft im Hintergrund)
    // -------------------------------------------------------
    processScan();

    // -------------------------------------------------------
    //  NORMALER BETRIEB: UART → ESP-NOW
    // -------------------------------------------------------
    if (!g_setupMode) {
        // Hardware-UART-Eingang puffern
        while (Serial1.available() && g_uartBufLen < UART_RX_BUF_SIZE) {
            g_uartBuf[g_uartBufLen++] = (uint8_t)Serial1.read();
        }

        // Puffer mit Sendeintervall absenden
        uint32_t now = millis();
        if (g_uartBufLen > 0 && (now - g_lastSendMs >= SEND_INTERVAL_MS)) {
            g_lastSendMs = now;

            // Debug: auf USB ausgeben
            Serial.print(F("[TX UART→ESP-NOW] "));
            Serial.print(g_uartBufLen);
            Serial.print(F(" B: "));
            uint16_t showLen = min((uint16_t)16, g_uartBufLen);
            for (uint16_t i = 0; i < showLen; i++) {
                if (g_uartBuf[i] < 0x10) Serial.print('0');
                Serial.print(g_uartBuf[i], HEX);
                Serial.print(' ');
            }
            if (g_uartBufLen > 16) Serial.print(F("..."));
            Serial.println();

            sendEspNow(g_uartBuf, g_uartBufLen);
            g_uartBufLen = 0;
        }

        // Heartbeat und Timeout-Überwachung
        sendHeartbeat();
        checkConnectionTimeout();
    }

    // -------------------------------------------------------
    //  LEDs aktualisieren
    // -------------------------------------------------------
    updateLEDs();
}
