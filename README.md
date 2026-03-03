# ESP32-C3 UART-Bridge via ESP-NOW

**Drahtlose UART-Brücke mit zwei Seeed Studio XIAO ESP32-C3 Modulen**

> 🇬🇧 An English version of this documentation is available in [README_EN.md](README_EN.md).

---

## Inhaltsverzeichnis

1. [Einleitung](#1-einleitung)
2. [Übersicht](#2-übersicht)
3. [Hardware-Anforderungen](#3-hardware-anforderungen)
4. [Pinbelegung](#4-pinbelegung)
5. [Projektstruktur](#5-projektstruktur)
6. [Installation & Flashen](#6-installation--flashen)
7. [Erster Start & Pairing (Setup-Modus)](#7-erster-start--pairing-setup-modus)
8. [AT-Befehlsreferenz](#8-at-befehlsreferenz)
9. [Normaler Betrieb](#9-normaler-betrieb)
10. [LED-Verhalten](#10-led-verhalten)
11. [Leistung & Timing](#11-leistung--timing)
12. [Dual-Port-Betrieb](#12-dual-port-betrieb)
13. [I2C-Slave-Modus](#13-i2c-slave-modus)
14. [Automatisches Test-Skript](#14-automatisches-test-skript)
15. [Hinweise & Besonderheiten](#15-hinweise--besonderheiten)
16. [Fehlerbehebung](#16-fehlerbehebung)

---

## 1. Einleitung

Viele Mikrocontroller, Sensoren und eingebettete Systeme kommunizieren über eine serielle UART-Schnittstelle. Sollen zwei solcher Systeme drahtlos miteinander verbunden werden, ist üblicherweise eine vollständige WLAN-Infrastruktur (Router, TCP/IP-Stack usw.) notwendig – ein erheblicher Aufwand für eine einfache Punkt-zu-Punkt-Verbindung.

**Die Grundidee dieses Projekts** ist denkbar einfach: Zwei ESP32-C3-Module bilden eine transparente, drahtlose UART-Brücke. Alles, was in die UART-Schnittstelle des einen Moduls gesendet wird, kommt auf der UART-Schnittstelle des anderen Moduls an – und umgekehrt. Für die angeschlossenen Geräte ist die Drahtlosverbindung vollständig transparent; sie „sehen" lediglich eine serielle Verbindung.

Als Übertragungsprotokoll kommt **ESP-NOW** zum Einsatz – ein von Espressif entwickeltes, verbindungsloses Peer-to-Peer-Protokoll auf Basis von IEEE 802.11. ESP-NOW ermöglicht eine direkte Kommunikation zwischen zwei Modulen ohne Router oder WLAN-Infrastruktur, mit geringer Latenz (< 5 ms) und einer Reichweite von bis zu 100 Metern im Freifeld.

**Typische Anwendungsfälle:**

- Kabellose Verbindung zwischen zwei Mikrocontrollern (z. B. Arduino ↔ Raspberry Pi)
- Drahtlose Verlängerung einer seriellen Schnittstelle (z. B. ein GPS-Modul im Außenbereich)
- Einfacher Ersatz für ein physisches Seriell-Kabel in bestehenden Projekten, ohne Software-Anpassungen
- Drahtlose Kommunikation zwischen Robotern, Drohnen oder anderen mobilen Geräten

---

## 2. Übersicht

Dieses Projekt verbindet zwei XIAO ESP32-C3 Module drahtlos über **ESP-NOW** und bildet eine transparente **UART-Bridge**:

```
Gerät A ──(UART 115200)──► ESP32-C3 [A] ──(ESP-NOW 2.4 GHz)──► ESP32-C3 [B] ──(UART 115200)──► Gerät B
Gerät A ◄─(UART 115200)── ESP32-C3 [A] ◄─(ESP-NOW 2.4 GHz)── ESP32-C3 [B] ◄─(UART 115200)── Gerät B
```

### Eigenschaften

| Eigenschaft              | Wert                                       |
|--------------------------|--------------------------------------------|
| Firmware-Version         | v1.2                                       |
| Protokoll                | ESP-NOW (IEEE 802.11, kein Router nötig)   |
| Reichweite               | ~100 m (Freifeld), ~20–50 m (Gebäude)     |
| UART-Baudrate            | 115200 Baud                                |
| Sendeintervall           | 5 ms                                       |
| Max. Nutzlast/Paket      | 240 Byte                                   |
| Ziel-Last                | 20 Byte alle 10 ms ≈ 2 kB/s               |
| Auto-Reconnect           | Ja (nach Neustart sofort)                  |
| Reconnect-Intervall      | 5 s (Peer-Neuregistrierung)                |
| ESP-NOW-Reinit-Timeout   | 30 s (komplette Neuinitialisierung)        |
| Einstellungsspeicher     | NVS (internes Flash, kein ext. EEPROM)     |
| Dual-Port                | Ja (USB-CDC + HW-UART gleichwertig)        |
| Kommunikations-Modus     | UART (Standard) oder I2C-Slave             |
| Debug-Modus              | Ja (umschaltbar per `ET+DEBUG`)            |

---

## 3. Hardware-Anforderungen

- 2× **Seeed Studio XIAO ESP32-C3**
- 2× LED (z. B. 3 mm grün, für Verbindungsstatus) + 2× LED (z. B. 3 mm gelb/rot, für Setup-Modus)
- 4× Vorwiderstand 220–330 Ω
- USB-C-Kabel (Flashen / Debuggen)

> **Hinweis:** Der Setup-Modus kann alternativ auch per Software-Befehl `ET+OPEN` aktiviert werden – ein externer Hardware-Eingang an GPIO 5 ist damit optional.

---

## 4. Pinbelegung

> Die Pinbelegung ist für **beide** Module identisch.

| GPIO  | Bezeichnung  | Funktion                                      |
|-------|--------------|-----------------------------------------------|
| **6** | D4 / I2C-SDA | I2C-Slave Datenleitung (nur im I2C-Modus)     |
| **7** | D5 / I2C-SCL | I2C-Slave Taktleitung (nur im I2C-Modus)      |
| **9** | D9 / BOOT    | LED Verbindungsstatus ⚠️ siehe Hinweis         |
| **10**| D10          | LED Setup-Modus                               |
| **20**| D7 / UART-RX | Hardware-UART Empfang (von externem Gerät)    |
| **21**| D6 / UART-TX | Hardware-UART Senden (an externes Gerät)      |

### Schaltung

```
3,3 V ──[220Ω]──► LED (Anode) ──► GPIO 10  (Kathode → GND)
3,3 V ──[220Ω]──► LED (Anode) ──► GPIO  9  (Kathode → GND)

Gerät ──────────────  TX  ──► GPIO 20 (UART RX)
Gerät ◄──────────── GPIO 21 (UART TX) ──  RX  ──
GND ─────────────────────────────────────── GND
```

> ⚠️ **GPIO 9 = BOOT-Taste** auf dem XIAO ESP32-C3.  
> Die LED darf angeschlossen werden, stört aber den Boot-Vorgang **nicht**, solange kein HIGH-Signal von außen angelegt wird.  
> Falls Bootprobleme auftreten → LED-Schaltung kurzfristig entfernen und nach dem Flashen wieder anschließen.

---

## 5. Projektstruktur

```
ESP_Bridge/
├── platformio.ini          ← PlatformIO-Konfiguration
├── README.md               ← Diese Dokumentation (Deutsch)
├── README_EN.md            ← Englische Dokumentation
├── test_bridge.py          ← Automatisches Test- und Pairing-Skript (Python)
├── arduino_tests/          ← Arduino-Uno Beispieltests über die ESP32-Bridge
├── include/
│   └── config.h            ← Alle Konstanten & Pin-Definitionen
└── src/
    └── main.cpp            ← Komplette Firmware (für beide Module identisch)
```

---

## 6. Installation & Flashen

### Voraussetzungen

- [VS Code](https://code.visualstudio.com/) mit [PlatformIO-Erweiterung](https://platformio.org/install/ide?install=vscode)
- Oder [Arduino IDE 2.x](https://www.arduino.cc/en/software) mit ESP32 Board-Paket

### Mit PlatformIO (empfohlen)

```bash
# 1. Projekt öffnen
# VS Code → Ordner öffnen → ESP_Bridge/

# 2. Erstes Modul flashen
pio run --target upload

# 3. Zweites Modul anschließen, erneut flashen
pio run --target upload

# 4. Seriellen Monitor öffnen (115200 Baud)
pio device monitor
```

### Mit Arduino IDE

1. Boardverwalter URL hinzufügen:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Board: **Seeed Studio XIAO ESP32-C3** installieren
3. Board auswählen, `USB CDC On Boot: Enabled` setzen
4. `src/main.cpp` in `ESP_Bridge.ino` umbenennen, `include/config.h` in denselben Ordner kopieren
5. Auf beide Module flashen

> **Wichtig:** Die Option `USB CDC On Boot: Enabled` ist zwingend erforderlich, damit der serielle Monitor über USB funktioniert.

---

## 7. Erster Start & Pairing (Setup-Modus)

Da beide Module die **identische Firmware** haben, unterscheiden sie sich nur durch die gespeicherte Peer-MAC. Beim allerersten Start ist noch keine MAC gespeichert – deshalb muss einmal das **Pairing** durchgeführt werden.

### Setup-Modus aktivieren

Es gibt zwei Möglichkeiten, den Setup-Modus zu starten:

1. **Software** (empfohlen): Befehl `ET+OPEN` über USB-Serial oder Hardware-UART senden
2. **Hardware** (optional): GPIO 5 auf 3,3 V legen (HIGH-aktiv)

### Schritt-für-Schritt

#### Vorbereitung
1. Beide Module mit USB verbinden und je ein Terminal öffnen (115200 Baud, USB-CDC)
2. Beide Module mit Spannung versorgen (oder Reset drücken)

#### Pairing (auf **einem** der beiden Module)

```
1. ET+OPEN eingeben
   → LED GPIO10 leuchtet auf
   → Setup-Modus aktiv
   → Verfügbare Befehle werden angezeigt

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
> Nach dem Empfang des ersten Datenpaketes wird die MAC des Absenders intern gespeichert (Auto-Peer-Discovery).  
> Um die Verbindung dauerhaft zu machen, auch auf dem zweiten Modul:

```
ET+OPEN → ET+PEER=AA:BB:CC:DD:EE:FF → ET+SAVE
```
(MAC vom ersten Modul via `ET+MAC?` abfragen)

---

## 8. AT-Befehlsreferenz

Alle Befehle können sowohl per **USB-Serial** als auch per **Hardware-UART** gesendet werden.  
Format: `ET+BEFEHL` – Zeilenendzeichen: `\n` oder `\r\n`

| Befehl                       | Setup nötig | Beschreibung                                          |
|------------------------------|:-----------:|-------------------------------------------------------|
| `ET+OPEN`                    | –           | Setup-Modus aktivieren                                |
| `ET+MAC?`                    | –           | Eigene MAC-Adresse anzeigen                           |
| `ET+STATUS?`                 | –           | Vollständigen Status anzeigen                         |
| `ET+DEBUG`                   | –           | Debug-Ausgaben ein-/ausschalten                       |
| `ET+SCAN`                    | ✓           | 5-Sekunden-Scan nach ESP-NOW-Peers starten            |
| `ET+LIST`                    | ✓           | Ergebnisse des letzten Scans anzeigen                 |
| `ET+SELECT=N`                | ✓           | Peer Nr. N aus Scan-Liste auswählen                   |
| `ET+PEER=AA:BB:CC:DD:EE:FF`  | ✓           | Peer-MAC manuell eingeben                             |
| `ET+PEER?`                   | ✓           | Gespeicherte Peer-MAC auslesen                        |
| `ET+CHANNEL=N`               | ✓           | WiFi-Kanal setzen (1–13, wirkt nach Neustart)         |
| `ET+ComMode=N`               | ✓           | Kommunikations-Modus setzen (1=UART, 2=I2C-Slave)    |
| `ET+I2CAddr=0xNN`            | ✓           | I2C-Slave-Adresse setzen (Hex, wirkt nach Neustart)   |
| `ET+RESET`                   | ✓           | Peer-MAC und alle Einstellungen löschen               |
| `ET+SAVE`                    | ✓           | Einstellungen im Flash speichern & Setup beenden      |

> **Hinweis:** Die Befehle `ET+OPEN`, `ET+MAC?`, `ET+STATUS?` und `ET+DEBUG` sind **immer** verfügbar, unabhängig vom aktuellen Modus. Alle anderen Befehle erfordern den Setup-Modus (aktiviert via `ET+OPEN`).

### Beispiel-Session

```
ET+MAC?
[INFO] Eigene MAC: 34:94:54:AB:CD:EF

ET+OPEN
=========================================
  *** SETUP-MODUS AKTIV ***
=========================================
Verfügbare Befehle:
  ET+SCAN          - Peers suchen
  ...

ET+SCAN
[SCAN] Suche läuft (5 s)...
[SCAN] Broadcast 1/5
[SCAN] Gefunden #1: 34:94:54:11:22:33
[SCAN] Abgeschlossen. 1 Peer(s) gefunden:
  [1] 34:94:54:11:22:33
[SETUP] Tipp: ET+SELECT=1  oder  ET+PEER=<MAC>

ET+SELECT=1
[SETUP] Peer gesetzt: 34:94:54:11:22:33
[SETUP] Zum Speichern: ET+SAVE

ET+STATUS?
=== ESP-NOW UART Bridge - Status ===
  Firmware:      v1.2
  Eigene MAC:    34:94:54:AB:CD:EF
  Peer MAC:      34:94:54:11:22:33
  Verbunden:     NEIN
  Modus:         SETUP
  ComMode:       UART
  WiFi-Kanal:    6
  UART Baud:     115200
  Debug:         AUS
====================================

ET+SAVE
[NVS] Einstellungen gespeichert.
[SETUP] Setup beendet → Normaler Betrieb
[INFO] Verbunden → LED GPIO9 leuchtet
```

---

## 9. Normaler Betrieb

Nach erfolgreichem Pairing und gespeicherter Peer-MAC läuft die Bridge vollautomatisch:

1. **Einschalten**: beide Module mit Spannung versorgen
2. **Auto-Connect**: jedes Modul lädt die Peer-MAC aus dem Flash und fügt sie als ESP-NOW-Peer hinzu
3. **Heartbeat**: alle 1 Sekunde wird ein kleines Keep-Alive-Paket gesendet/empfangen
4. **UART → ESP-NOW**: Hardware-UART-Eingang wird alle **5 ms** gepuffert und versendet
5. **ESP-NOW → UART**: empfangene Pakete werden sofort auf Hardware-UART **und** USB-Serial ausgegeben
6. **Reconnect**: bei Verbindungsverlust wird alle 5 s versucht, den Peer neu zu registrieren; nach 30 s wird ESP-NOW komplett neu initialisiert

### Automatischer Reconnect

| Ereignis                        | Reaktion                                         |
|---------------------------------|--------------------------------------------------|
| Sendefehler (< 5 ×)             | Fehlerzähler erhöhen, weiter senden              |
| Sendefehler (≥ 5 ×)             | Peer neu registrieren, `VERBINDUNG VERLOREN`     |
| Verbindungsverlust seit 5 s     | Peer erneut als ESP-NOW-Peer hinzufügen          |
| Verbindungsverlust seit 30 s    | ESP-NOW komplett neu initialisieren              |

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

## 10. LED-Verhalten

| LED             | GPIO | Zustand             | Bedeutung                     |
|-----------------|------|---------------------|-------------------------------|
| Setup-LED       | 10   | EIN (dauerhaft)     | Setup-Modus aktiv             |
| Setup-LED       | 10   | AUS                 | Normaler Betrieb              |
| Verbindungs-LED | 9    | EIN (dauerhaft)     | Peer erreichbar, verbunden    |
| Verbindungs-LED | 9    | Blinkt (0,5 Hz)     | Nicht verbunden / sucht Peer  |

---

## 11. Leistung & Timing

### Theoretische Grenzwerte

| Parameter              | Wert              |
|------------------------|-------------------|
| ESP-NOW max. Baudrate  | ~1 Mbps brutto    |
| Nutzlast pro Paket     | max. 250 Byte     |
| Min. Paketabstand      | ~1–2 ms           |
| Max. empfohlene Last   | ~50–80 kB/s netto |

### Konfigurierbare Parameter (`config.h`)

| Konstante                  | Standard  | Beschreibung                               |
|----------------------------|-----------|--------------------------------------------|
| `SEND_INTERVAL_MS`         | 5 ms      | Sendeintervall für UART-Puffer             |
| `ESPNOW_MAX_PAYLOAD`       | 240 B     | Max. Nutzdaten pro Paket                   |
| `UART_RX_BUF_SIZE`         | 512 B     | UART-Empfangspuffer                        |
| `HEARTBEAT_INTERVAL_MS`    | 1000 ms   | Keep-Alive-Intervall                       |
| `MAX_IDLE_MS`              | 3000 ms   | Timeout bis „nicht verbunden"              |
| `ESPNOW_CHANNEL`           | 6         | WiFi-Kanal (1–13, beide müssen gleich!)    |
| `ESPNOW_SEND_RETRIES`      | 3         | Wiederholungen bei Sendefehler             |
| `RECONNECT_INTERVAL_MS`    | 5000 ms   | Intervall für Peer-Neuregistrierung        |
| `ESPNOW_REINIT_TIMEOUT_MS` | 30000 ms  | Timeout für komplette ESP-NOW-Neuinit.     |
| `I2C_SDA_PIN`              | 6         | SDA-Pin für I2C-Slave (D4)                 |
| `I2C_SCL_PIN`              | 7         | SCL-Pin für I2C-Slave (D5)                 |
| `I2C_BUFFER_SIZE`          | 10 B      | I2C-Datenpuffer (feste Paketgröße)         |
| `I2C_DEFAULT_ADDR`         | 0xEE      | Standard-I2C-Slave-Adresse (8-Bit)         |

---

## 12. Dual-Port-Betrieb

Eine besondere Eigenschaft dieser Firmware ist der **gleichwertige Dual-Port-Betrieb**: Beide Schnittstellen – USB-CDC (`Serial`) und Hardware-UART (`Serial1`) – werden vollständig parallel behandelt.

| Funktion                         | USB-CDC (Serial) | HW-UART (Serial1) |
|----------------------------------|:----------------:|:-----------------:|
| ET+ Befehle senden               | ✓                | ✓                 |
| Befehlsantworten empfangen       | ✓                | ✓                 |
| Bridge-Daten senden (→ ESP-NOW)  | ✓                | ✓                 |
| Bridge-Daten empfangen (ESP-NOW→)| ✓                | ✓                 |
| Debug-Ausgaben (`ET+DEBUG`)      | ✓                | ✓                 |

Das bedeutet: Ein angeschlossenes Gerät kann `ET+`-Befehle direkt über die Hardware-UART-Pins senden, ohne dass ein Computer per USB angeschlossen sein muss.

---

## 13. I2C-Slave-Modus

Ab Firmware v1.2 unterstützt die Bridge neben dem Standard-UART-Betrieb auch einen **I2C-Slave-Modus**. In diesem Modus werden Daten nicht über die Hardware-UART-Pins, sondern über eine I2C-Schnittstelle (GPIO 6 = SDA, GPIO 7 = SCL) empfangen und gesendet.

### Anwendungsfall

Der I2C-Slave-Modus eignet sich, wenn ein Mikrocontroller (z. B. Arduino) keine freie UART-Schnittstelle hat, aber I2C verfügbar ist. Der ESP32-C3 verhält sich dann als I2C-Slave:

- **Master → Slave (Register 0x01):** Der Master schreibt bis zu 10 Byte, die sofort per ESP-NOW zum anderen Modul übertragen werden.
- **Slave → Master (Register 0x02):** Der Master liest die letzten 10 Byte, die vom anderen Modul empfangen wurden.

### I2C-Modus aktivieren

```
ET+OPEN
ET+ComMode=2
ET+I2CAddr=0x42   (optional, Standard: 0xEE als 8-Bit-Adresse → 7-Bit: 0x77)
ET+SAVE
```

Nach dem Neustart ist I2C aktiv. USB-Serial steht weiterhin für Befehle und Debug-Ausgaben zur Verfügung, überträgt aber keine Bridge-Daten.

### I2C-Registerbelegung

| Register | Richtung       | Beschreibung                                  |
|----------|----------------|-----------------------------------------------|
| `0x01`   | Master → Slave | Master schreibt 10 Byte → wird per ESP-NOW gesendet |
| `0x02`   | Slave → Master | Master liest 10 Byte ← zuletzt empfangene Daten |

### Zurück zum UART-Modus

```
ET+OPEN
ET+ComMode=1
ET+SAVE
```

---

## 14. Automatisches Test-Skript

Das enthaltene Python-Skript `test_bridge.py` verbindet sich mit beiden Modulen gleichzeitig über USB-Serial, liest Boot-Ausgaben aus, führt das Pairing automatisch durch und verifiziert die Verbindung.

### Arduino-Uno Hardwaretests

Zusätzlich enthält `arduino_tests/` zwei manuelle Arduino-Testsets:

- `arduino_tests/uart_test/`: Uno A ↔ ESP A und Uno B ↔ ESP B im **UART-Modus** (`ComMode=1`, Uno jeweils via `SoftwareSerial`)
- `arduino_tests/i2c_test/`: Uno A ↔ ESP A und Uno B ↔ ESP B im **I2C-Modus** (`ComMode=2`, Bridge-Register `0x01`/`0x02`)

Wichtig: Die Unos werden hierbei **nicht direkt miteinander** verbunden, sondern ausschließlich über die beiden ESP32-Bridge-Module.

### Voraussetzungen

```bash
pip install pyserial
```

### Verwendung

```bash
# Port-Einstellungen anpassen (oben im Skript)
# PORT_A = "COM21"  (oder z. B. /dev/ttyUSB0 unter Linux)
# PORT_B = "COM22"

python test_bridge.py
```

### Ablauf

1. Beide Module werden verbunden und Boot-Ausgaben werden gesammelt
2. MAC-Adressen werden automatisch erkannt
3. Ist das Pairing bereits vorhanden, wird es übersprungen
4. Andernfalls wird `ET+OPEN` → `ET+PEER=<MAC>` → `ET+SAVE` auf beiden Modulen ausgeführt
5. Es wird auf Verbindungsbestätigung via Heartbeat gewartet
6. `ET+STATUS?` wird auf beiden Modulen abgefragt und ausgegeben
7. Ein abschließendes Ergebnis wird ausgegeben: ✓ BRIDGE IS WORKING oder ✗ Connection issue

---

## 15. Hinweise & Besonderheiten

### GPIO 9 = BOOT-Taste
Der XIAO ESP32-C3 hat auf GPIO 9 den eingebauten BOOT-Button. Eine LED daran **stört den Normalbetrieb nicht**, kann aber beim Flashen (wenn GPIO 9 gedrückt gehalten wird) zu Problemen führen. Im Zweifelsfall den LED-Vorwiderstand vor dem Flashen kurz unterbrechen.

### ESP-NOW & WiFi-Kanal
Beide ESPs **müssen auf demselben Kanal** arbeiten. Standard: Kanal **6**. Änderung per `ET+OPEN` → `ET+CHANNEL=N` → `ET+SAVE` → Neustart auf **beiden** Modulen. Wird der gleiche Kanal verwendet wie ein verbundenes WiFi-Netzwerk, kann die Performance leicht sinken.

### Kein Router benötigt
ESP-NOW arbeitet im **Ad-hoc-Modus** direkt zwischen den Modulen. Kein WLAN-Router oder Internet notwendig.

### Gleiche Firmware für beide Module
Beide Module bekommen **dieselbe Firmware** geflasht. Die Unterscheidung erfolgt allein durch die im Flash gespeicherte Peer-MAC-Adresse.

### Reichweite & Antenne
Der XIAO ESP32-C3 hat eine externe PCB-Antenne. Für maximale Reichweite Module nicht metallisch abschirmen. Bei schlechtem Signal: `ET+CHANNEL=1` oder `ET+CHANNEL=11` versuchen (weniger Interferenz mit anderen 2,4-GHz-Geräten).

### Paket-Segmentierung
Übersteigen die gepufferten UART-Daten 240 Byte, werden sie automatisch in mehrere ESP-NOW-Pakete aufgeteilt (Segmentierung). Jedes Paket enthält eine Sequenznummer zur Fehlererkennung.

### Debug-Modus
Der Debug-Modus kann jederzeit mit `ET+DEBUG` umgeschaltet werden (auch im normalen Betrieb). Im Debug-Modus werden detaillierte Informationen zu gesendeten und empfangenen Paketen ausgegeben, einschließlich Hex-Dumps der ersten 16 Byte.

---

## 16. Fehlerbehebung

| Problem                              | Mögliche Ursache                         | Lösung                                            |
|--------------------------------------|------------------------------------------|---------------------------------------------------|
| LED GPIO9 blinkt dauerhaft           | Kein Peer gespeichert / nicht erreichbar | `ET+OPEN` → Scan → Pairing wiederholen            |
| `ET+SCAN` findet keine Peers         | Anderes Modul nicht in Betrieb           | Zweites Modul einschalten, gleichen Kanal prüfen  |
| Kein USB-Serial                      | CDC nicht aktiviert                      | `USB CDC On Boot: Enabled` in Board-Einstellungen |
| UART-Daten fehlen oder korrumpiert   | Falsche Pinzuweisung oder Verkabelung    | GPIO 20 (RX) und GPIO 21 (TX) prüfen             |
| Module verbinden sich nicht          | Unterschiedliche WiFi-Kanäle             | `ET+STATUS?` auf beiden → Kanal vergleichen       |
| Daten mit hoher Latenz               | `SEND_INTERVAL_MS` zu groß               | In `config.h` auf 2–3 ms reduzieren               |
| Flash-Einstellungen weg              | Firmware neu geflasht                    | Pairing wiederholen (`ET+OPEN` → `ET+SCAN` → `ET+SAVE`) |
| Befehle werden nicht erkannt         | Nicht im Setup-Modus                     | `ET+OPEN` eingeben (gilt nicht für `ET+MAC?`, `ET+STATUS?`, `ET+DEBUG`) |
| Verbindung bricht immer wieder ab    | Schlechter Empfang / Kanalstörung        | `ET+CHANNEL=N` auf beiden Modulen ändern          |
