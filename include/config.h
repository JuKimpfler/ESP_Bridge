#pragma once

// ============================================================
//  ESP32-C3 UART-Bridge via ESP-NOW
//  config.h  –  Projekt-Konfiguration
// ============================================================

// ------------------------------------------------------------
//  Pin-Belegung  (Seeed Studio XIAO ESP32-C3)
// ------------------------------------------------------------
#define PIN_LED_CONNECTED   9   // LED Verbindungsstatus  (ACHTUNG: GPIO9 = BOOT-Taste!)
#define PIN_LED_SETUP       10  // LED Setup-Modus aktiv

// ------------------------------------------------------------
//  Hardware-UART  (UART1, Serial1)
//  XIAO ESP32-C3: D6 = GPIO21 (TX), D7 = GPIO20 (RX)
// ------------------------------------------------------------
#define HW_UART_RX_PIN      20
#define HW_UART_TX_PIN      21
#define HW_UART_BAUD        115200
 
// ------------------------------------------------------------
//  USB-CDC Serial (Debuggen / Befehle)
// ------------------------------------------------------------
#define USB_SERIAL_BAUD     115200

// ------------------------------------------------------------
//  ESP-NOW Einstellungen
// ------------------------------------------------------------
#define ESPNOW_CHANNEL      6           // WiFi-Kanal (1–13), beide müssen gleich sein
#define ESPNOW_MAX_PAYLOAD  240         // Nutzlast pro Paket (max 250 – Header)
#define ESPNOW_SEND_RETRIES 3           // Wiederholungen bei Sendefehler

// ------------------------------------------------------------
//  UART-Puffer  &  Sende-Timing
// ------------------------------------------------------------
#define UART_RX_BUF_SIZE    512         // Puffergröße für Hardware-UART-Eingang
#define SEND_INTERVAL_MS    5           // Alle 5 ms gepufferte Daten senden
#define MAX_IDLE_MS         3000        // 3 s kein RX → „nicht verbunden"

// ------------------------------------------------------------
//  Setup-Modus  &  Scan
// ------------------------------------------------------------
#define SCAN_DURATION_MS    5000        // Suchdauer in ms
#define SCAN_BCAST_COUNT    5           // Anzahl Broadcast-Pakete beim Scan
#define SCAN_BCAST_INTERVAL 200         // ms zwischen den Broadcasts
#define MAX_FOUND_PEERS     10          // Maximale gleichzeitig sichtbare Peers
#define CMD_BUF_SIZE        128         // Kommandopuffer (USB-Serial)

// ------------------------------------------------------------
//  NVS / Preferences  (internes EEPROM-Ersatz)
// ------------------------------------------------------------
#define NVS_NAMESPACE       "espbridge"
#define NVS_KEY_PEER_MAC    "peer_mac"
#define NVS_KEY_CHANNEL     "channel"
#define NVS_KEY_BAUD        "uart_baud"

// ------------------------------------------------------------
//  Paket-Header
// ------------------------------------------------------------
#define PKT_HEADER_SIZE     5           // type(1) + seq(2) + dataLen(2)

// ------------------------------------------------------------
//  Paket-Typen
// ------------------------------------------------------------
#define PKT_DATA            0x01        // Nutzdaten (UART-Bridge)
#define PKT_DISCOVERY       0x02        // Suche nach anderen ESPs
#define PKT_DISC_RESP       0x03        // Antwort auf Discovery
#define PKT_HEARTBEAT       0x04        // Verbindungs-Keepalive
#define PKT_HEARTBEAT_ACK   0x05        // Antwort auf Heartbeat
#define PKT_DEBUG           0x06        // Debug-Daten (Roboter -> Debug-Monitor)

// ------------------------------------------------------------
//  Debug-Monitor (dritter ESP fuer PC-Debug-Ausgabe)
// ------------------------------------------------------------
#define NVS_KEY_DBGMON      "dbgmon"    // NVS-Schluessel: Debug-Monitor aktiv (uint8)

// ------------------------------------------------------------
//  Heartbeat
// ------------------------------------------------------------
#define HEARTBEAT_INTERVAL_MS   1000    // Alle 1 s einen Heartbeat senden

// ------------------------------------------------------------
//  Reconnect
// ------------------------------------------------------------
#define RECONNECT_INTERVAL_MS   5000    // Alle 5 s Peer-Neuregistrierung bei Trennung
#define ESPNOW_REINIT_TIMEOUT_MS 30000  // ESP-NOW komplett neu initialisieren nach 30 s Trennung
