// ============================================================
//  ESP32-C3 UART-Bridge via ESP-NOW
//  src/main.cpp
//
//  Firmware ist fuer BEIDE Module identisch.
//  Unterschied entsteht nur durch die gespeicherte Peer-MAC.
//
//  Hardware: Seeed Studio XIAO ESP32-C3
//  Pins:
//    GPIO 9   - LED Verbindungsstatus  (ACHTUNG: = BOOT-Taste!)
//    GPIO 10  - LED Setup-Modus
//    GPIO 20  - UART1 RX  (D7)
//    GPIO 21  - UART1 TX  (D6)
//
//  Setup-Modus wird per Befehl ET+OPEN aktiviert.
//
//  USB (Serial) und Hardware-UART (Serial1) sind gleichwertig:
//    - Beide Eingaben fuehren in denselben Bridge-Puffer
//    - Empfangene ESP-NOW-Daten werden auf beide ausgegeben
//    - ET+ Befehle werden von beiden Eingaengen erkannt
//    - Debug-Ausgaben (ET+Debug) erscheinen auf beiden gleich
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
    uint16_t dataLen;                   // Nutzdatenlaenge
    uint8_t  data[ESPNOW_MAX_PAYLOAD];  // Nutzdaten
};
#pragma pack(pop)

// Timeout fuer Kommando-Erkennung (ms)
#define CMD_TIMEOUT_MS   500

// ============================================================
//  Globale Variablen
// ============================================================

// --- NVS ---
Preferences prefs;

// --- Status ---
bool     g_setupMode     = false;
bool     g_peerConnected = false;
bool     g_peerStored    = false;

// --- Debug-Modus ---
volatile bool g_debugMode = false;

// --- Debug-Monitor (dritter ESP fuer PC-Ausgabe) ---
volatile bool g_debugMonitorEnabled = false;

// --- MAC-Adressen ---
uint8_t  g_myMac[6]        = {0};
uint8_t  g_peerMac[6]      = {0};
uint8_t  g_broadcastMac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

// --- Bridge-Empfangspuffer (gemeinsam fuer USB + UART) ---
uint8_t  g_bridgeBuf[UART_RX_BUF_SIZE];
uint16_t g_bridgeBufLen = 0;
uint32_t g_lastSendMs   = 0;

// --- Verbindungs-Timeout ---
uint32_t g_lastRxMs       = 0;
uint8_t  g_sendFailCount  = 0;
#define  SEND_FAIL_THRESHOLD  5

// --- Heartbeat ---
uint32_t g_lastHeartbeatMs = 0;

// --- Reconnect ---
uint32_t g_lastReconnectMs    = 0;
uint32_t g_disconnectedSinceMs = 0;

// --- LED ---
uint32_t g_lastLedBlinkMs  = 0;
bool     g_ledBlinkState   = false;

// --- Sequenznummer ---
uint16_t g_txSeq = 0;

// --- Setup-Modus ---
uint32_t g_lastScanBcastMs = 0;
uint8_t  g_scanBcastSent  = 0;
bool     g_scanActive     = false;
uint32_t g_scanStartMs    = 0;

struct FoundPeer {
    uint8_t mac[6];
};
FoundPeer g_foundPeers[MAX_FOUND_PEERS];
uint8_t   g_foundPeerCount = 0;

// --- Kommandopuffer (je einer fuer USB-Serial und HW-UART) ---
char     g_usbCmdBuf[CMD_BUF_SIZE];
uint8_t  g_usbCmdLen     = 0;
uint32_t g_usbCmdLastMs  = 0;

char     g_hwCmdBuf[CMD_BUF_SIZE];
uint8_t  g_hwCmdLen      = 0;
uint32_t g_hwCmdLastMs   = 0;

// ============================================================
//  Ausgabe-Hilfsfunktionen
//
//  cmdPrint/cmdPrintln  - Immer auf BEIDE Ausgaenge (Kommando-Antworten)
//  dbgPrint/dbgPrintln  - Nur bei g_debugMode, auf BEIDE Ausgaenge
// ============================================================

template<typename T> void cmdPrint(T v)              { Serial.print(v);   Serial1.print(v);   }
template<typename T> void cmdPrintln(T v)            { Serial.println(v); Serial1.println(v); }
inline                void cmdPrintln()               { Serial.println();  Serial1.println();  }
template<typename T> void cmdPrint(T v, int base)    { Serial.print(v, base);   Serial1.print(v, base);   }
template<typename T> void cmdPrintln(T v, int base)  { Serial.println(v, base); Serial1.println(v, base); }

template<typename T> void dbgPrint(T v)              { if (!g_debugMode) return; Serial.print(v);   Serial1.print(v);   }
template<typename T> void dbgPrintln(T v)            { if (!g_debugMode) return; Serial.println(v); Serial1.println(v); }
inline                void dbgPrintln()               { if (!g_debugMode) return; Serial.println();  Serial1.println();  }
template<typename T> void dbgPrint(T v, int base)    { if (!g_debugMode) return; Serial.print(v, base);   Serial1.print(v, base);   }
template<typename T> void dbgPrintln(T v, int base)  { if (!g_debugMode) return; Serial.println(v, base); Serial1.println(v, base); }

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

/** String "AA:BB:CC:DD:EE:FF"  ->  MAC-Array, gibt true bei Erfolg */
bool strToMac(const char *str, uint8_t *mac) {
    unsigned int v[6];
    if (sscanf(str, "%x:%x:%x:%x:%x:%x",
               &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6) return false;
    for (int i = 0; i < 6; i++) mac[i] = (uint8_t)v[i];
    return true;
}

/** Gibt den aktuellen Status aus (auf beide Ausgaenge) */
void printStatus() {
    cmdPrintln(F("=== ESP-NOW UART Bridge - Status ==="));
    cmdPrint  (F("  Firmware:      ")); cmdPrintln(F("v1.2"));
    cmdPrint  (F("  Eigene MAC:    ")); cmdPrintln(macToStr(g_myMac));
    cmdPrint  (F("  Peer MAC:      "));
    if (g_peerStored) cmdPrintln(macToStr(g_peerMac));
    else              cmdPrintln(F("(nicht gesetzt)"));
    cmdPrint  (F("  Verbunden:     ")); cmdPrintln(g_peerConnected ? F("JA") : F("NEIN"));
    cmdPrint  (F("  Modus:         ")); cmdPrintln(g_setupMode     ? F("SETUP") : F("NORMAL"));
    cmdPrint  (F("  WiFi-Kanal:    ")); cmdPrintln(ESPNOW_CHANNEL);
    cmdPrint  (F("  UART Baud:     ")); cmdPrintln(HW_UART_BAUD);
    cmdPrint  (F("  Debug:         ")); cmdPrintln(g_debugMode     ? F("AN") : F("AUS"));
    cmdPrint  (F("  Debug-Monitor: ")); cmdPrintln(g_debugMonitorEnabled ? F("AN") : F("AUS"));
    cmdPrintln(F("===================================="));
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
        dbgPrint(F("[ESP-NOW] Peer hinzugefuegt: "));
        dbgPrintln(macToStr(mac));
        return true;
    }
    dbgPrintln(F("[ESP-NOW] Fehler: Peer konnte nicht hinzugefuegt werden"));
    return false;
}

void removeEspNowPeer(const uint8_t *mac) {
    if (esp_now_is_peer_exist(mac)) {
        esp_now_del_peer(mac);
    }
}

// ============================================================
//  ESP-NOW  -  Sende-Callback
// ============================================================
void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
    if (status == ESP_NOW_SEND_SUCCESS) {
        g_sendFailCount = 0;
        if (!g_peerConnected && g_peerStored) {
            g_peerConnected = true;
            dbgPrintln(F("[INFO] Peer erfolgreich erreicht -> VERBUNDEN"));
        }
    } else {
        g_sendFailCount++;
        if (g_sendFailCount >= SEND_FAIL_THRESHOLD) {
            if (g_peerConnected) {
                g_peerConnected = false;
                g_disconnectedSinceMs = millis();
                g_lastReconnectMs = 0;
                dbgPrintln(F("[WARN] Mehrere Sendefehler -> VERBINDUNG VERLOREN"));
                if (g_peerStored) {
                    removeEspNowPeer(g_peerMac);
                    addEspNowPeer(g_peerMac);
                }
            }
        }
    }
}

// ============================================================
//  ESP-NOW  -  Empfangs-Callback
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
    //  DATA-Paket:  Daten auf BEIDE Ausgaenge (USB + HW-UART)
    // --------------------------------------------------------
    if (pkt->type == PKT_DATA) {
        if (pkt->dataLen > 0 && pkt->dataLen <= ESPNOW_MAX_PAYLOAD) {
            // Bridge-Daten auf beide Ausgaenge schreiben
            Serial.write(pkt->data, pkt->dataLen);
            Serial1.write(pkt->data, pkt->dataLen);

            // Debug: Hex-Dump (nur bei aktivem Debug-Modus)
            if (g_debugMode) {
                dbgPrint(F("[RX ESP-NOW] "));
                dbgPrint(pkt->dataLen);
                dbgPrint(F(" B  seq="));
                dbgPrint(pkt->seq);
                dbgPrint(F("  Daten: "));
                uint16_t showLen = min((uint16_t)16, pkt->dataLen);
                for (uint16_t i = 0; i < showLen; i++) {
                    if (pkt->data[i] < 0x10) dbgPrint('0');
                    dbgPrint(pkt->data[i], HEX);
                    dbgPrint(' ');
                }
                if (pkt->dataLen > 16) dbgPrint(F("..."));
                dbgPrintln();
            }
        }

        // Peer als verbunden markieren
        if (!g_peerConnected) {
            g_peerConnected = true;
            g_sendFailCount = 0;
            dbgPrintln(F("[INFO] Paket empfangen -> VERBUNDEN"));
        }

        // Unbekannter Peer: automatisch ergaenzen
        if (!g_peerStored) {
            memcpy(g_peerMac, mac, 6);
            g_peerStored = true;
            addEspNowPeer(g_peerMac);
            dbgPrint(F("[INFO] Peer auto-erkannt: "));
            dbgPrintln(macToStr(g_peerMac));
        }
    }

    // --------------------------------------------------------
    //  DISCOVERY-Paket: Antworten
    // --------------------------------------------------------
    else if (pkt->type == PKT_DISCOVERY) {
        dbgPrint(F("[SETUP] Discovery-Anfrage von: "));
        dbgPrintln(macToStr(mac));

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

        for (uint8_t i = 0; i < g_foundPeerCount; i++) {
            if (memcmp(g_foundPeers[i].mac, mac, 6) == 0) return;
        }
        if (g_foundPeerCount < MAX_FOUND_PEERS) {
            memcpy(g_foundPeers[g_foundPeerCount++].mac, mac, 6);
            dbgPrint(F("[SCAN] Gefunden #"));
            dbgPrint(g_foundPeerCount);
            dbgPrint(F(": "));
            dbgPrintln(macToStr(mac));
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
    //  HEARTBEAT-ACK: Verbindung bestaetigt
    // --------------------------------------------------------
    else if (pkt->type == PKT_HEARTBEAT_ACK) {
        if (!g_peerConnected) {
            g_peerConnected = true;
            g_sendFailCount = 0;
            dbgPrintln(F("[INFO] Heartbeat-ACK -> VERBUNDEN"));
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

    esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);

    WiFi.macAddress(g_myMac);
    dbgPrint(F("[INFO] Eigene MAC: "));
    dbgPrintln(macToStr(g_myMac));

    if (esp_now_init() != ESP_OK) {
        dbgPrintln(F("[FATAL] ESP-NOW Init fehlgeschlagen! Neustart in 2 s..."));
        delay(2000);
        ESP.restart();
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    addEspNowPeer(g_broadcastMac);

    dbgPrintln(F("[INFO] ESP-NOW bereit"));
}

// ============================================================
//  NVS: Einstellungen laden / speichern
// ============================================================
void loadSettings() {
    prefs.begin(NVS_NAMESPACE, true);
    if (prefs.isKey(NVS_KEY_PEER_MAC)) {
        prefs.getBytes(NVS_KEY_PEER_MAC, g_peerMac, 6);
        g_peerStored = true;
        dbgPrint(F("[NVS] Gespeicherte Peer-MAC: "));
        dbgPrintln(macToStr(g_peerMac));
    } else {
        dbgPrintln(F("[NVS] Kein Peer gespeichert."));
    }
    if (prefs.isKey(NVS_KEY_DBGMON)) {
        g_debugMonitorEnabled = (prefs.getUChar(NVS_KEY_DBGMON, 0) != 0);
        dbgPrint(F("[NVS] Debug-Monitor: "));
        dbgPrintln(g_debugMonitorEnabled ? F("AN") : F("AUS"));
    }
    prefs.end();
}

void saveSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    if (g_peerStored) {
        prefs.putBytes(NVS_KEY_PEER_MAC, g_peerMac, 6);
    }
    prefs.end();
    cmdPrintln(F("[NVS] Einstellungen gespeichert."));
}

void clearSettings() {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.clear();
    prefs.end();
    memset(g_peerMac, 0, 6);
    g_peerStored    = false;
    g_peerConnected = false;
    cmdPrintln(F("[NVS] Alle Einstellungen geloescht."));
}

// ============================================================
//  Setup-Modus: Betreten / Verlassen
// ============================================================
void enterSetupMode() {
    if (g_setupMode) return;
    g_setupMode = true;
    digitalWrite(PIN_LED_SETUP, HIGH);

    cmdPrintln();
    cmdPrintln(F("========================================="));
    cmdPrintln(F("  *** SETUP-MODUS AKTIV ***"));
    cmdPrintln(F("========================================="));
    cmdPrintln(F("Verfuegbare Befehle:"));
    cmdPrintln(F("  ET+SCAN          - Peers suchen"));
    cmdPrintln(F("  ET+LIST          - Gefundene Peers anzeigen"));
    cmdPrintln(F("  ET+SELECT=N      - Peer N aus Scan-Liste waehlen"));
    cmdPrintln(F("  ET+PEER=AA:...:FF- Peer-MAC manuell setzen"));
    cmdPrintln(F("  ET+PEER?         - Gespeicherte Peer-MAC anzeigen"));
    cmdPrintln(F("  ET+MAC?          - Eigene MAC anzeigen"));
    cmdPrintln(F("  ET+STATUS?       - Vollstaendigen Status anzeigen"));
    cmdPrintln(F("  ET+CHANNEL=N     - WiFi-Kanal setzen (1-13, Neustart)"));
    cmdPrintln(F("  ET+RESET         - Peer-Einstellungen loeschen"));
    cmdPrintln(F("  ET+SAVE          - Speichern & Setup beenden"));
    cmdPrintln(F("  ET+Debug         - Debug-Ausgaben umschalten"));
    cmdPrintln(F("  ET+DBGMON        - Debug-Monitor ein-/ausschalten"));
    cmdPrintln(F("========================================="));
}

void exitSetupMode() {
    g_setupMode = false;
    digitalWrite(PIN_LED_SETUP, LOW);

    if (g_peerStored) {
        removeEspNowPeer(g_peerMac);
        addEspNowPeer(g_peerMac);
    }

    cmdPrintln(F("[SETUP] Setup beendet -> Normaler Betrieb"));
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
    cmdPrint(F("[SCAN] Suche laeuft ("));
    cmdPrint(SCAN_DURATION_MS / 1000);
    cmdPrintln(F(" s)..."));
}

void processScan() {
    if (!g_scanActive) return;
    uint32_t now = millis();

    if (g_scanBcastSent < SCAN_BCAST_COUNT &&
        now - g_lastScanBcastMs >= SCAN_BCAST_INTERVAL) {
        g_lastScanBcastMs = now;
        g_scanBcastSent++;
        EspNowPacket pkt = {};
        pkt.type    = PKT_DISCOVERY;
        pkt.seq     = g_txSeq++;
        pkt.dataLen = 0;
        esp_now_send(g_broadcastMac, reinterpret_cast<uint8_t*>(&pkt), PKT_HEADER_SIZE);
        dbgPrint(F("[SCAN] Broadcast "));
        dbgPrint(g_scanBcastSent);
        dbgPrint(F("/"));
        dbgPrintln(SCAN_BCAST_COUNT);
    }

    if (now - g_scanStartMs >= SCAN_DURATION_MS) {
        g_scanActive = false;
        cmdPrint(F("[SCAN] Abgeschlossen. "));
        cmdPrint(g_foundPeerCount);
        cmdPrintln(F(" Peer(s) gefunden:"));
        for (uint8_t i = 0; i < g_foundPeerCount; i++) {
            cmdPrint(F("  ["));
            cmdPrint(i + 1);
            cmdPrint(F("] "));
            cmdPrintln(macToStr(g_foundPeers[i].mac));
        }
        if (g_foundPeerCount > 0) {
            cmdPrintln(F("[SETUP] Tipp: ET+SELECT=1  oder  ET+PEER=<MAC>"));
            cmdPrintln(F("[SETUP] Dann: ET+SAVE"));
        } else {
            cmdPrintln(F("[SCAN] Kein Peer gefunden. Stelle sicher, dass der"));
            cmdPrintln(F("       andere ESP laeuft und im gleichen Kanal ist."));
        }
    }
}

// ============================================================
//  ET-Befehle verarbeiten
// ============================================================
void processCommand(const char *rawCmd) {
    char cmd[CMD_BUF_SIZE];
    strncpy(cmd, rawCmd, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    int len = strlen(cmd);
    while (len > 0 && (cmd[len-1] == '\r' || cmd[len-1] == '\n' || cmd[len-1] == ' '))
        cmd[--len] = '\0';
    if (len == 0) return;

    // Bestimmte Befehle sind IMMER erlaubt (auch ausserhalb Setup)
    bool isAlwaysAllowed = (strncmp(cmd, "ET+STATUS?", 10) == 0 ||
                            strncmp(cmd, "ET+MAC?",     7) == 0 ||
                            strncmp(cmd, "ET+OPEN",     7) == 0 ||
                            strncasecmp(cmd, "ET+DEBUG",  8) == 0 ||
                            strncasecmp(cmd, "ET+DBGMON", 9) == 0);

    if (!g_setupMode && !isAlwaysAllowed) {
        cmdPrintln(F("[CMD] Nicht im Setup-Modus. ET+OPEN eingeben."));
        return;
    }

    if (strncmp(cmd, "ET+", 3) != 0) {
        cmdPrintln(F("[CMD] Unbekanntes Format. Befehle beginnen mit ET+"));
        return;
    }

    const char *body = cmd + 3;

    // ---- ET+DEBUG (case-insensitive) ----
    if (strcasecmp(body, "DEBUG") == 0 || strcasecmp(body, "Debug") == 0) {
        g_debugMode = !g_debugMode;
        cmdPrint(F("[DEBUG] Debug-Modus: "));
        cmdPrintln(g_debugMode ? F("AN") : F("AUS"));
        return;
    }

    // ---- ET+DBGMON (case-insensitive) ----
    if (strcasecmp(body, "DBGMON") == 0) {
        g_debugMonitorEnabled = !g_debugMonitorEnabled;
        // In NVS speichern
        prefs.begin(NVS_NAMESPACE, false);
        prefs.putUChar(NVS_KEY_DBGMON, g_debugMonitorEnabled ? 1 : 0);
        prefs.end();
        cmdPrint(F("[DBGMON] Debug-Monitor: "));
        cmdPrintln(g_debugMonitorEnabled ? F("AN") : F("AUS"));
        return;
    }

    // ---- ET+SCAN ----
    if (strcmp(body, "SCAN") == 0) {
        startScan();
    }

    // ---- ET+LIST ----
    else if (strcmp(body, "LIST") == 0) {
        if (g_scanActive) {
            cmdPrintln(F("[SCAN] Scan laeuft noch..."));
        } else {
            cmdPrint(F("[SCAN] "));
            cmdPrint(g_foundPeerCount);
            cmdPrintln(F(" Peer(s):"));
            for (uint8_t i = 0; i < g_foundPeerCount; i++) {
                cmdPrint(F("  ["));
                cmdPrint(i + 1);
                cmdPrint(F("] "));
                cmdPrintln(macToStr(g_foundPeers[i].mac));
            }
        }
    }

    // ---- ET+SELECT=N ----
    else if (strncmp(body, "SELECT=", 7) == 0) {
        int idx = atoi(body + 7) - 1;
        if (idx >= 0 && idx < (int)g_foundPeerCount) {
            memcpy(g_peerMac, g_foundPeers[idx].mac, 6);
            g_peerStored = true;
            cmdPrint(F("[SETUP] Peer gesetzt: "));
            cmdPrintln(macToStr(g_peerMac));
            cmdPrintln(F("[SETUP] Zum Speichern: ET+SAVE"));
        } else {
            cmdPrintln(F("[CMD] Ungueltiger Index. Erst ET+SCAN ausfuehren."));
        }
    }

    // ---- ET+PEER=AA:BB:CC:DD:EE:FF ----
    else if (strncmp(body, "PEER=", 5) == 0) {
        uint8_t newMac[6];
        if (strToMac(body + 5, newMac)) {
            memcpy(g_peerMac, newMac, 6);
            g_peerStored = true;
            cmdPrint(F("[SETUP] Peer-MAC gesetzt: "));
            cmdPrintln(macToStr(g_peerMac));
            cmdPrintln(F("[SETUP] Zum Speichern: ET+SAVE"));
        } else {
            cmdPrintln(F("[CMD] Ungueltiges MAC-Format. Erwartet: AA:BB:CC:DD:EE:FF"));
        }
    }

    // ---- ET+PEER? ----
    else if (strcmp(body, "PEER?") == 0) {
        cmdPrint(F("[INFO] Peer-MAC: "));
        if (g_peerStored) cmdPrintln(macToStr(g_peerMac));
        else              cmdPrintln(F("(nicht gesetzt)"));
    }

    // ---- ET+MAC? ----
    else if (strcmp(body, "MAC?") == 0) {
        cmdPrint(F("[INFO] Eigene MAC: "));
        cmdPrintln(macToStr(g_myMac));
    }

    // ---- ET+STATUS? ----
    else if (strcmp(body, "STATUS?") == 0) {
        printStatus();
    }

    // ---- ET+CHANNEL=N ----
    else if (strncmp(body, "CHANNEL=", 8) == 0) {
        int ch = atoi(body + 8);
        if (ch >= 1 && ch <= 13) {
            prefs.begin(NVS_NAMESPACE, false);
            prefs.putUChar(NVS_KEY_CHANNEL, (uint8_t)ch);
            prefs.end();
            cmdPrint(F("[SETUP] Kanal wird nach ET+SAVE/Neustart: "));
            cmdPrintln(ch);
            cmdPrintln(F("[WARN] Kanal-Aenderung erfordert Neustart!"));
        } else {
            cmdPrintln(F("[CMD] Ungueltiger Kanal. Bereich: 1-13"));
        }
    }

    // ---- ET+RESET ----
    else if (strcmp(body, "RESET") == 0) {
        clearSettings();
    }

    // ---- ET+OPEN ----
    else if (strcmp(body, "OPEN") == 0) {
        enterSetupMode();
    }

    // ---- ET+SAVE ----
    else if (strcmp(body, "SAVE") == 0) {
        saveSettings();
        exitSetupMode();
    }

    // ---- Unbekannt ----
    else {
        cmdPrint(F("[CMD] Unbekannter Befehl: "));
        cmdPrintln(cmd);
        cmdPrintln(F("[CMD] ET+STATUS? fuer Hilfe"));
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
            dbgPrint(F("[ERROR] ESP-NOW Sendefehler: 0x"));
            dbgPrintln(result, HEX);
            g_sendFailCount++;
        }

        offset += chunk;
        if (offset < totalLen) delayMicroseconds(400);
    }
}

// ============================================================
//  Debug-Daten per ESP-NOW Broadcast senden (-> Debug-Monitor)
//
//  Sendet PKT_DEBUG Pakete als Broadcast. Der Debug-Monitor-ESP
//  empfaengt diese und gibt sie per USB-Serial an den PC aus.
//  Die normale Bridge-Kommunikation wird nicht gestoert.
// ============================================================
void sendDebugBroadcast(const uint8_t *data, uint16_t totalLen) {
    if (!g_debugMonitorEnabled) return;

    uint16_t offset = 0;
    while (offset < totalLen) {
        EspNowPacket pkt = {};
        pkt.type    = PKT_DEBUG;
        pkt.seq     = g_txSeq++;
        uint16_t chunk = min((uint16_t)ESPNOW_MAX_PAYLOAD, (uint16_t)(totalLen - offset));
        pkt.dataLen = chunk;
        memcpy(pkt.data, data + offset, chunk);

        esp_now_send(g_broadcastMac,
                     reinterpret_cast<uint8_t*>(&pkt),
                     PKT_HEADER_SIZE + chunk);

        offset += chunk;
        if (offset < totalLen) delayMicroseconds(400);
    }
}

// ============================================================
//  Eingabe-Stream lesen (fuer USB und HW-UART gleichermassen)
//
//  Erkennt ET+ Befehle und DBG: Debug-Nachrichten, leitet
//  sonstige Daten in den gemeinsamen Bridge-Puffer (g_bridgeBuf).
// ============================================================
void readInputStream(Stream &input, char *cmdBuf, uint8_t &cmdLen, uint32_t &cmdLastMs) {
    while (input.available()) {
        char c = (char)input.read();
        cmdLastMs = millis();

        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) {
                cmdBuf[cmdLen] = '\0';
                // Pruefen ob Kommando (ET+...)
                if (cmdLen >= 3 && cmdBuf[0] == 'E' && cmdBuf[1] == 'T' && cmdBuf[2] == '+') {
                    processCommand(cmdBuf);
                }
                // Pruefen ob Debug-Nachricht (DBG:...)
                else if (cmdLen >= 4 && cmdBuf[0] == 'D' && cmdBuf[1] == 'B' && cmdBuf[2] == 'G' && cmdBuf[3] == ':') {
                    // Debug-Daten an den Debug-Monitor senden (nicht in Bridge-Puffer!)
                    sendDebugBroadcast(reinterpret_cast<uint8_t*>(cmdBuf + 4), cmdLen - 4);
                }
                else {
                    // Kein Kommando -> in Bridge-Puffer uebernehmen
                    for (uint8_t i = 0; i < cmdLen; i++) {
                        if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                            g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)cmdBuf[i];
                        }
                    }
                    // Zeilenende ebenfalls uebernehmen
                    if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                        g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)c;
                    }
                }
                cmdLen = 0;
            }
        } else {
            if (cmdLen < CMD_BUF_SIZE - 1) {
                cmdBuf[cmdLen++] = c;

                // Fruehe Erkennung: Sobald klar ist, dass es weder ET+ noch DBG: ist,
                // sofort in den Bridge-Puffer uebertragen (geringe Latenz)
                bool couldBeCmd = true;
                if (cmdLen >= 1 && cmdBuf[0] != 'E') couldBeCmd = false;
                if (couldBeCmd && cmdLen >= 2 && cmdBuf[1] != 'T') couldBeCmd = false;
                if (couldBeCmd && cmdLen >= 3 && cmdBuf[2] != '+') couldBeCmd = false;

                bool couldBeDbg = true;
                if (cmdLen >= 1 && cmdBuf[0] != 'D') couldBeDbg = false;
                if (couldBeDbg && cmdLen >= 2 && cmdBuf[1] != 'B') couldBeDbg = false;
                if (couldBeDbg && cmdLen >= 3 && cmdBuf[2] != 'G') couldBeDbg = false;
                if (couldBeDbg && cmdLen >= 4 && cmdBuf[3] != ':') couldBeDbg = false;

                if (!couldBeCmd && !couldBeDbg) {
                    for (uint8_t i = 0; i < cmdLen; i++) {
                        if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                            g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)cmdBuf[i];
                        }
                    }
                    cmdLen = 0;
                }
            } else {
                // Kommandopuffer voll -> als Daten behandeln
                for (uint8_t i = 0; i < cmdLen; i++) {
                    if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                        g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)cmdBuf[i];
                    }
                }
                if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                    g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)c;
                }
                cmdLen = 0;
            }
        }
    }
}

/** Kommandopuffer-Timeout: Zeichen die lange auf Zeilenende warten als Daten senden */
void flushStaleCmdBuf(char *cmdBuf, uint8_t &cmdLen, uint32_t &cmdLastMs) {
    if (cmdLen > 0 && (millis() - cmdLastMs > CMD_TIMEOUT_MS)) {
        for (uint8_t i = 0; i < cmdLen; i++) {
            if (g_bridgeBufLen < UART_RX_BUF_SIZE) {
                g_bridgeBuf[g_bridgeBufLen++] = (uint8_t)cmdBuf[i];
            }
        }
        cmdLen = 0;
    }
}

// ============================================================
//  LED-Zustand aktualisieren
// ============================================================
void updateLEDs() {
    digitalWrite(PIN_LED_SETUP, g_setupMode ? HIGH : LOW);

    if (g_peerConnected) {
        digitalWrite(PIN_LED_CONNECTED, HIGH);
    } else {
        uint32_t now = millis();
        if (now - g_lastLedBlinkMs >= 500) {
            g_lastLedBlinkMs = now;
            g_ledBlinkState  = !g_ledBlinkState;
            digitalWrite(PIN_LED_CONNECTED, g_ledBlinkState ? HIGH : LOW);
        }
    }
}

// ============================================================
//  Verbindungs-Timeout pruefen
// ============================================================
void checkConnectionTimeout() {
    if (!g_peerConnected) return;
    if (millis() - g_lastRxMs > MAX_IDLE_MS) {
        g_peerConnected = false;
        g_sendFailCount = 0;
        g_disconnectedSinceMs = millis();
        g_lastReconnectMs = 0;
        dbgPrintln(F("[WARN] Timeout: kein Signal -> VERBINDUNG VERLOREN"));
        if (g_peerStored) {
            removeEspNowPeer(g_peerMac);
            addEspNowPeer(g_peerMac);
        }
    }
}

// ============================================================
//  Reconnect-Versuch (periodisch solange getrennt)
// ============================================================
void tryReconnect() {
    if (g_peerConnected || !g_peerStored || g_setupMode) return;
    uint32_t now = millis();
    if (now - g_lastReconnectMs < RECONNECT_INTERVAL_MS) return;
    g_lastReconnectMs = now;

    if (now - g_disconnectedSinceMs >= ESPNOW_REINIT_TIMEOUT_MS) {
        dbgPrintln(F("[INFO] Verbindung lange unterbrochen - ESP-NOW wird neu initialisiert..."));
        esp_now_deinit();
        delay(100);
        initEspNow();
        addEspNowPeer(g_peerMac);
        g_disconnectedSinceMs = now;
        return;
    }

    removeEspNowPeer(g_peerMac);
    addEspNowPeer(g_peerMac);
    g_lastHeartbeatMs = 0;
    dbgPrintln(F("[INFO] Peer neu registriert - Verbindungsversuch..."));
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
    Serial.setTxTimeoutMs(0);
    delay(600);

    // Pins konfigurieren
    pinMode(PIN_LED_CONNECTED, OUTPUT);
    pinMode(PIN_LED_SETUP,     OUTPUT);
    digitalWrite(PIN_LED_CONNECTED, LOW);
    digitalWrite(PIN_LED_SETUP,     LOW);

    // Hardware-UART initialisieren
    Serial1.begin(HW_UART_BAUD, SERIAL_8N1, HW_UART_RX_PIN, HW_UART_TX_PIN);

    // Debug-Meldungen beim Start (nur sichtbar wenn Debug aktiv)
    dbgPrintln();
    dbgPrintln(F("========================================="));
    dbgPrintln(F("  ESP32-C3 UART-Bridge via ESP-NOW v1.2"));
    dbgPrintln(F("========================================="));
    dbgPrintln(F("[INFO] Hardware-UART (Serial1) bereit"));
    dbgPrint  (F("[INFO] RX: GPIO")); dbgPrint  (HW_UART_RX_PIN);
    dbgPrint  (F("  TX: GPIO"));      dbgPrintln(HW_UART_TX_PIN);

    // Einstellungen aus NVS laden
    loadSettings();

    // ESP-NOW starten
    initEspNow();

    // Gespeicherten Peer hinzufuegen
    if (g_peerStored) {
        addEspNowPeer(g_peerMac);
    } else {
        dbgPrintln(F("[WARN] Kein Peer konfiguriert."));
        dbgPrintln(F("[WARN] ET+OPEN eingeben und ET+SCAN ausfuehren."));
    }

    g_lastRxMs = millis();

    dbgPrintln(F("[INFO] Normaler Betrieb. ET+OPEN -> Setup | ET+Debug -> Debug"));
    dbgPrint  (F("[INFO] Debug-Monitor: "));
    dbgPrintln(g_debugMonitorEnabled ? F("AN") : F("AUS"));
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // -------------------------------------------------------
    //  Eingaben von USB-Serial und HW-UART gleichermassen lesen
    // -------------------------------------------------------
    readInputStream(Serial,  g_usbCmdBuf, g_usbCmdLen, g_usbCmdLastMs);
    readInputStream(Serial1, g_hwCmdBuf,  g_hwCmdLen,  g_hwCmdLastMs);

    // Kommandopuffer-Timeout (falls ET+ Praefix ohne Zeilenende)
    flushStaleCmdBuf(g_usbCmdBuf, g_usbCmdLen, g_usbCmdLastMs);
    flushStaleCmdBuf(g_hwCmdBuf,  g_hwCmdLen,  g_hwCmdLastMs);

    // -------------------------------------------------------
    //  Scan verarbeiten (laeuft im Hintergrund)
    // -------------------------------------------------------
    processScan();

    // -------------------------------------------------------
    //  NORMALER BETRIEB: Bridge-Puffer -> ESP-NOW
    // -------------------------------------------------------
    if (!g_setupMode) {
        // Bridge-Puffer Ueberlauf vermeiden
        if (g_bridgeBufLen >= UART_RX_BUF_SIZE) {
            g_bridgeBufLen = 0;
            dbgPrintln(F("[WARN] Bridge-Puffer Ueberlauf - alte Daten verworfen"));
        }

        // Puffer mit Sendeintervall absenden
        uint32_t now = millis();
        if (g_bridgeBufLen > 0 && (now - g_lastSendMs >= SEND_INTERVAL_MS)) {
            g_lastSendMs = now;

            // Debug: Hex-Dump der gesendeten Daten
            if (g_debugMode) {
                dbgPrint(F("[TX->ESP-NOW] "));
                dbgPrint(g_bridgeBufLen);
                dbgPrint(F(" B: "));
                uint16_t showLen = min((uint16_t)16, g_bridgeBufLen);
                for (uint16_t i = 0; i < showLen; i++) {
                    if (g_bridgeBuf[i] < 0x10) dbgPrint('0');
                    dbgPrint(g_bridgeBuf[i], HEX);
                    dbgPrint(' ');
                }
                if (g_bridgeBufLen > 16) dbgPrint(F("..."));
                dbgPrintln();
            }

            sendEspNow(g_bridgeBuf, g_bridgeBufLen);
            g_bridgeBufLen = 0;
        }

        // Heartbeat und Timeout-Ueberwachung
        sendHeartbeat();
        checkConnectionTimeout();
        tryReconnect();
    }

    // -------------------------------------------------------
    //  LEDs aktualisieren
    // -------------------------------------------------------
    updateLEDs();
}
