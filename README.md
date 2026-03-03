# ESP32-C3 UART-Bridge via ESP-NOW

**Wireless UART-Brücke mit zwei Seeed Studio XIAO ESP32-C3 Modulen**

---

## Inhaltsverzeichnis

1. [Übersicht](#1-übersicht)
2. [Hardware-Anforderungen](#2-hardware-anforderungen)
3. [Pinbelegung](#3-pinbelegung)
4. [Projektstruktur](#4-projektstruktur)
5. [Installation & Flashen](#5-installation--flashen)
6. [Erster Start & Pairing (Setup-Modus)](#6-erster-start--pairing-setup-modus)
7. [AT-Befehlsreferenz](#7-at-befehlsreferenz)
8. [Normaler Betrieb](#8-normaler-betrieb)
9. [LED-Verhalten](#9-led-verhalten)
10. [Leistung & Timing](#10-leistung--timing)
11. [Hinweise & Besonderheiten](#11-hinweise--besonderheiten)
12. [Fehlerbehebung](#12-fehlerbehebung)

---

## 1. Übersicht

Dieses Projekt verbindet zwei XIAO ESP32-C3 Module drahtlos über **ESP-NOW** und bildet eine transparente **UART-Bridge**:

```
Gerät A ──(UART 115200)──► ESP32-C3 [A] ──(ESP-NOW 2.4 GHz)──► ESP32-C3 [B] ──(UART 115200)──► Gerät B
Gerät A ◄─(UART 115200)── ESP32-C3 [A] ◄─(ESP-NOW 2.4 GHz)── ESP32-C3 [B] ◄─(UART 115200)── Gerät B
```

### Eigenschaften

| Eigenschaft         | Wert                                    |
|---------------------|-----------------------------------------|
| Protokoll           | ESP-NOW (IEEE 802.11, kein Router nötig)|
| Reichweite          | ~100 m (Freifeld), ~20–50 m (Gebäude)  |
| UART-Baudrate       | 115200 Baud                             |
| Sendeintervall      | 5 ms                                    |
| Max. Nutzlast/Paket | 240 Byte                                |
| Ziel-Last           | 20 Byte alle 10 ms ≈ 2 kB/s            |
| Auto-Reconnect      | Ja (nach Neustart sofort)               |
| Einstellungsspeicher| NVS (internes Flash, kein ext. EEPROM)  |

---

## 2. Hardware-Anforderungen

- 2× **Seeed Studio XIAO ESP32-C3**
- 2× LED (z. B. 3 mm grün) + 2× LED (z. B. 3 mm gelb/rot)
- 4× Vorwiderstand 220–330 Ω
- Optionaler Pull-Down-Widerstand 10 kΩ an GPIO 5 (wenn kein externer Signalgeber)
- USB-C-Kabel (Flashen / Debuggen)

---

## 3. Pinbelegung

> Die Pinbelegung ist für **beide** Module identisch.

| GPIO  | Bezeichnung  | Funktion                                      |
|-------|--------------|-----------------------------------------------|
| **5** | D3           | Setup-Modus-Eingang (HIGH-aktiv, 3,3 V)       |
| **9** | D9 / BOOT    | LED Verbindungsstatus ⚠️ siehe Hinweis         |
| **10**| D10          | LED Setup-Modus                               |
| **20**| D7 / UART-RX | Hardware-UART Empfang (von externem Gerät)    |
| **21**| D6 / UART-TX | Hardware-UART Senden (an externes Gerät)      |

### Schaltung

```
3,3 V ──[220Ω]──► LED (Anode) ──► GPIO 10  (Kathode → GND)
3,3 V ──[220Ω]──► LED (Anode) ──► GPIO  9  (Kathode → GND)

Externer Signalgeber: GPIO 5 ── Signal  (0 V / 3,3 V)
                                            │
                              10 kΩ Pull-Down nach GND empfohlen

Gerät ──────────────  TX  ──► GPIO 20 (UART RX)
Gerät ◄──────────── GPIO 21 (UART TX) ──  RX  ──
GND ─────────────────────────────────────── GND
```

> ⚠️ **GPIO 9 = BOOT-Taste** auf dem XIAO ESP32-C3.  
> Die LED darf angeschlossen werden, stört aber den Boot-Vorgang **nicht**, solange kein HIGH-Signal von außen angelegt wird.  
> Falls Bootprobleme auftreten → LED-Schaltung kurzfristig entfernen und nach dem Flashen wieder anschließen.

---

## 4. Projektstruktur

```
ESP_Com/
├── platformio.ini          ← PlatformIO-Konfiguration
├── Anleitung.md            ← Diese Datei
├── include/
│   └── config.h            ← Alle Konstanten & Pin-Definitionen
└── src/
    └── main.cpp            ← Komplette Firmware (für beide Module identisch)
```

---

## 5. Installation & Flashen

### Voraussetzungen

- [VS Code](https://code.visualstudio.com/) mit [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode)
- Oder [Arduino IDE 2.x](https://www.arduino.cc/en/software) mit ESP32 Board-Paket

### Mit PlatformIO (empfohlen)

```bash
# 1. Projekt öffnen
# VS Code → Ordner öffnen → ESP_Com/

# 2. Erstes Modul flashen
pio run --target upload

# 3. Zweites Modul anschließen, erneut flashen
pio run --target upload
```

### Mit Arduino IDE

1. Boardverwalter URL hinzufügen:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Board: **Seeed Studio XIAO ESP32-C3** installieren
3. Board auswählen, `USB CDC On Boot: Enabled` setzen
4. `src/main.cpp` in `ESP_Bridge.ino` umbenennen, beide `.h`-Dateien in denselben Ordner kopieren
5. Auf beide Module flashen

---

## 6. Erster Start & Pairing (Setup-Modus)

Da beide Module die **identische Firmware** haben, unterscheiden sie sich nur durch die gespeicherte Peer-MAC. Beim allerersten Start ist noch keine MAC gespeichert – deshalb muss einmal das **Pairing** durchgeführt werden.

### Schritt-für-Schritt

#### Vorbereitung
1. Beide Module mit USB verbinden und je ein Terminal öffnen (115200 Baud, USB-CDC)
2. Beide Module mit Spannung versorgen (oder Reset drücken)

#### Pairing (auf **einem** der beiden Module)

```
1. GPIO5 auf HIGH legen (externer Eingang oder Brücken-Jumper)
   → LED GPIO10 leuchtet auf
   → Setup-Modus aktiv

2. ET+SCAN
   → Startet einen 5-sekündigen Scan
   → Ausgabe: "[SCAN] Gefunden #1: AA:BB:CC:DD:EE:FF"

3. ET+SELECT=1
   → Auswahl des ersten gefundenen Peers
   
4. ET+SAVE
   → MAC wird im Flash gespeichert
   → Setup-Modus wird beendet
   → Verbindung wird sofort aufgebaut
```

> **Das zweite Modul muss NICHT in den Setup-Modus versetzt werden!**  
> Es erkennt eingehende Discovery-Pakete automatisch, antwortet und verbindet sich.  
> Nach dem Empfang des ersten Datenpaketes wird die MAC des Absenders intern gespeichert.  
> Um die Verbindung dauerhaft zu machen, auch auf dem zweiten Modul:

```
GPIO5 HIGH → ET+PEER=AA:BB:CC:DD:EE:FF → ET+SAVE
```
(MAC vom ersten Modul via `ET+MAC?` abfragen)

---

## 7. AT-Befehlsreferenz

Alle Befehle werden per **USB-Serial** (Terminal) im Format `ET+BEFEHL` gesendet.  
Zeilenendzeichen: `\n` oder `\r\n`

| Befehl                       | Beschreibung                                          |
|------------------------------|-------------------------------------------------------|
| `ET+SCAN`                    | 5-Sekunden-Scan nach ESP-NOW-Peers                    |
| `ET+LIST`                    | Ergebnisse des letzten Scans anzeigen                 |
| `ET+SELECT=N`                | Peer Nr. N aus Scan-Liste auswählen                   |
| `ET+PEER=AA:BB:CC:DD:EE:FF`  | Peer-MAC manuell eingeben                             |
| `ET+PEER?`                   | Gespeicherte Peer-MAC auslesen                        |
| `ET+MAC?`                    | Eigene MAC-Adresse anzeigen (auch außerhalb Setup)    |
| `ET+STATUS?`                 | Vollständigen Status anzeigen (auch außerhalb Setup)  |
| `ET+CHANNEL=N`               | WiFi-Kanal setzen (1–13, wirkt nach Neustart)         |
| `ET+RESET`                   | Peer-MAC und Einstellungen löschen                    |
| `ET+SAVE`                    | Einstellungen im Flash speichern & Setup beenden      |

### Beispiel-Session

```
ET+MAC?
[INFO] Eigene MAC: 34:94:54:AB:CD:EF

ET+SCAN
[SCAN] Suche läuft (5 s)...
[SCAN] Broadcast 1/5
[SCAN] Gefunden #1: 34:94:54:11:22:33
[SCAN] Abgeschlossen. 1 Peer(s) gefunden:
  [1] 34:94:54:11:22:33
[SETUP] Tipp: ET+SELECT=1  oder  ET+PEER=<MAC>

ET+SELECT=1
[SETUP] Peer gesetzt: 34:94:54:11:22:33

ET+STATUS?
=== ESP-NOW UART Bridge – Status ===
  Firmware:      v1.0
  Eigene MAC:    34:94:54:AB:CD:EF
  Peer MAC:      34:94:54:11:22:33
  Verbunden:     NEIN
  Modus:         SETUP
  WiFi-Kanal:    1
  UART Baud:     115200
====================================

ET+SAVE
[NVS] Einstellungen gespeichert.
[SETUP] Setup beendet → Normaler Betrieb
[INFO] Verbunden → LED GPIO9 leuchtet
```

---

## 8. Normaler Betrieb

Nach erfolgreichem Pairing und gespeicherter Peer-MAC läuft die Bridge vollautomatisch:

1. **Einschalten**: beide Module mit Spannung versorgen
2. **Auto-Connect**: jedes Modul lädt die Peer-MAC aus dem Flash und fügt sie als ESP-NOW-Peer hinzu
3. **Heartbeat**: alle 1 Sekunde wird ein kleines Keep-Alive-Paket gesendet/empfangen
4. **UART → ESP-NOW**: Hardware-UART-Eingang wird alle **5 ms** gepuffert und versendet
5. **ESP-NOW → UART**: empfangene Pakete werden sofort auf Hardware-UART ausgegeben
6. **Debug**: alle Aktionen werden zusätzlich auf USB-Serial (115200) ausgegeben

### Timing-Analyse

```
Externe Quelle sendet 20 Byte alle 10 ms:
  → UART-Eingang:    ~2.000 Byte/s  =  ~16 kbps
  → Pufferung:       0–5 ms Verzögerung
  → ESP-NOW:         ~1–2 ms Übertragungszeit
  → Gesamtlatenz:    ~3–7 ms  ✓ (deutlich unter 10 ms)
  → ESP-NOW-Kapazität: ~250 kbps (Reserve: >15×)
```

---

## 9. LED-Verhalten

| LED          | GPIO | Zustand             | Bedeutung                     |
|--------------|------|---------------------|-------------------------------|
| Setup-LED    | 10   | EIN (dauerhaft)     | Setup-Modus aktiv             |
| Setup-LED    | 10   | AUS                 | Normaler Betrieb              |
| Verbindungs-LED | 9 | EIN (dauerhaft)     | Peer erreichbar, verbunden    |
| Verbindungs-LED | 9 | Blinkt (0,5 Hz)     | Nicht verbunden / sucht Peer  |

---

## 10. Leistung & Timing

### Theoretische Grenzwerte

| Parameter              | Wert              |
|------------------------|-------------------|
| ESP-NOW max. Baudrate  | ~1 Mbps brutto    |
| Nutzlast pro Paket     | max. 250 Byte     |
| Min. Paketabstand      | ~1–2 ms           |
| Max. empfohlene Last   | ~50–80 kB/s netto |

### Konfigurierbare Parameter (`config.h`)

| Konstante              | Standard | Beschreibung                               |
|------------------------|----------|--------------------------------------------|
| `SEND_INTERVAL_MS`     | 5 ms     | Sendeintervall für UART-Puffer             |
| `ESPNOW_MAX_PAYLOAD`   | 240 B    | Max. Nutzdaten pro Paket                   |
| `UART_RX_BUF_SIZE`     | 512 B    | UART-Empfangspuffer                        |
| `HEARTBEAT_INTERVAL_MS`| 1000 ms  | Keep-Alive-Intervall                       |
| `MAX_IDLE_MS`          | 3000 ms  | Timeout bis „nicht verbunden"              |
| `ESPNOW_CHANNEL`       | 1        | WiFi-Kanal (1–13, beide müssen gleich!)    |

---

## 11. Hinweise & Besonderheiten

### GPIO 9 = BOOT-Taste
Der XIAO ESP32-C3 hat auf GPIO 9 den eingebauten BOOT-Button. Eine LED daran **stört den Normalbetrieb nicht**, kann aber beim Flashen (wenn man GPIO 9 gedrückt hält) zu Problemen führen. Im Zweifelsfall LED-Widerstand vor dem Flashen kurz unterbrechen.

### ESP-NOW & WiFi-Kanal
Beide ESPs **müssen auf demselben Kanal** arbeiten. Standard: Kanal 1. Änderung per `ET+CHANNEL=N` + `ET+SAVE` + Neustart. Wird der gleiche Kanal verwendet wie ein verbundenes WiFi-Netzwerk, kann die Performance leicht sinken.

### Kein Router benötigt
ESP-NOW arbeitet im **Ad-hoc-Modus** direkt zwischen den Modulen. Kein WLAN-Router oder Internet notwendig.

### Gleiche Firmware für beide Module
Beide Module bekommen **dieselbe Firmware** geflasht. Die Unterscheidung erfolgt allein durch die im Flash gespeicherte Peer-MAC-Adresse.

### Reichweite & Antenne
Der XIAO ESP32-C3 hat eine externe PCB-Antenne. Für maximale Reichweite Module nicht metallisch abschirmen. Bei schlechtem Signal: `ET+CHANNEL=6` oder `ET+CHANNEL=11` versuchen (weniger Interferenz mit anderen 2,4-GHz-Geräten).

---

## 12. Fehlerbehebung

| Problem                          | Mögliche Ursache                     | Lösung                                       |
|----------------------------------|--------------------------------------|----------------------------------------------|
| LED GPIO9 blinkt dauerhaft       | Kein Peer gespeichert / nicht erreichbar | Setup-Modus → Scan → Pairing wiederholen  |
| `ET+SCAN` findet keine Peers     | Anderes Modul nicht in Betrieb       | Zweites Modul einschalten, gleichen Kanal prüfen |
| Kein USB-Serial                  | CDC nicht aktiviert                  | `USB CDC On Boot: Enabled` in Board-Einstellungen |
| UART-Daten fehlen oder korrumpiert | Falsche Pinzuweisung               | GPIO 20 (RX) und GPIO 21 (TX) prüfen        |
| Module verbinden sich nicht      | Unterschiedliche WiFi-Kanäle         | `ET+STATUS?` auf beiden → Kanal vergleichen  |
| Daten mit hoher Latenz           | `SEND_INTERVAL_MS` zu groß           | In `config.h` auf 2–3 ms reduzieren          |
| Flash-Einstellungen weg          | Firmware neu geflasht ohne EEPROM-Erhalt | Pairing wiederholen (ET+SCAN → ET+SAVE)  |
