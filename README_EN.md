# ESP32-C3 UART Bridge via ESP-NOW

**Wireless UART bridge using two Seeed Studio XIAO ESP32-C3 modules with optional debug monitor**

> 🇩🇪 Eine deutsche Version dieser Dokumentation ist in [README.md](README.md) verfügbar.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Overview](#2-overview)
3. [Hardware Requirements](#3-hardware-requirements)
4. [Pin Assignment](#4-pin-assignment)
5. [Project Structure](#5-project-structure)
6. [Installation & Flashing](#6-installation--flashing)
7. [First Start & Pairing (Setup Mode)](#7-first-start--pairing-setup-mode)
8. [AT Command Reference](#8-at-command-reference)
9. [Normal Operation](#9-normal-operation)
10. [Debug Monitor (Third ESP)](#10-debug-monitor-third-esp)
11. [LED Behavior](#11-led-behavior)
12. [Performance & Timing](#12-performance--timing)
13. [Dual-Port Operation](#13-dual-port-operation)
14. [Automated Test Script](#14-automated-test-script)
15. [Notes & Particularities](#15-notes--particularities)
16. [Troubleshooting](#16-troubleshooting)

---

## 1. Introduction

Many microcontrollers, sensors, and embedded systems communicate via a serial UART interface. Connecting two such systems wirelessly typically requires a full Wi-Fi infrastructure (router, TCP/IP stack, etc.) — a considerable overhead for a simple point-to-point link.

**The core idea of this project** is straightforward: two ESP32-C3 modules form a transparent, wireless UART bridge. Any data sent into the UART interface of one module arrives at the UART interface of the other — and vice versa. The wireless link is completely transparent to the connected devices; they simply "see" a serial connection.

The transport protocol used is **ESP-NOW** — a connectionless peer-to-peer protocol developed by Espressif, based on IEEE 802.11. ESP-NOW allows direct communication between two modules without any router or Wi-Fi infrastructure, with low latency (< 5 ms) and a range of up to 100 meters in open space.

**Typical use cases:**

- Wireless connection between two microcontrollers (e.g. Arduino ↔ Raspberry Pi)
- Wireless extension of a serial interface (e.g. a GPS module located outdoors)
- A drop-in replacement for a physical serial cable in existing projects, without any software changes
- Wireless communication between robots, drones, or other mobile devices

---

## 2. Overview

This project connects two XIAO ESP32-C3 modules wirelessly via **ESP-NOW**, forming a transparent **UART bridge**:

```
Device A ──(UART 115200)──► ESP32-C3 [A] ──(ESP-NOW 2.4 GHz)──► ESP32-C3 [B] ──(UART 115200)──► Device B
Device A ◄─(UART 115200)── ESP32-C3 [A] ◄─(ESP-NOW 2.4 GHz)── ESP32-C3 [B] ◄─(UART 115200)── Device B
```

### Debug Monitor (optional)

A **third ESP32-C3** can be connected to a PC via USB as a debug monitor.
Both bridge ESPs can send debug data to the monitor — the normal bridge communication is **not** affected:

```
Robot A ──(UART)──► ESP32 [A] ══(ESP-NOW)══ ESP32 [B] ◄──(UART)── Robot B
                       ║                        ║
                  (PKT_DEBUG)              (PKT_DEBUG)
                       ║                        ║
                       ╚════════╗   ╔═══════════╝
                                ▼   ▼
                         ESP32 [Debug Monitor]
                                │
                            USB-Serial
                                │
                                ▼
                               PC
```

- Debug data flows **only** from robots to PC (not vice versa)
- The debug feature is **toggleable** (`ET+DBGMON`)
- The existing bridge communication is **not affected**

### Features

| Feature                   | Value                                          |
|---------------------------|------------------------------------------------|
| Firmware version          | v1.2                                           |
| Protocol                  | ESP-NOW (IEEE 802.11, no router required)      |
| Range                     | ~100 m (open space), ~20–50 m (indoors)        |
| UART baud rate            | 115200 baud                                    |
| Send interval             | 5 ms                                           |
| Max. payload per packet   | 240 bytes                                      |
| Target throughput         | 20 bytes every 10 ms ≈ 2 kB/s                 |
| Auto-reconnect            | Yes (immediately after power-on)               |
| Reconnect interval        | 5 s (peer re-registration)                     |
| ESP-NOW re-init timeout   | 30 s (full re-initialization)                  |
| Settings storage          | NVS (internal flash, no external EEPROM)       |
| Dual-port                 | Yes (USB-CDC + HW-UART fully equivalent)       |
| Debug mode                | Yes (toggle with `ET+DEBUG`)                   |
| Debug monitor             | Yes (third ESP, toggle with `ET+DBGMON`)       |

---

## 3. Hardware Requirements

- 2× **Seeed Studio XIAO ESP32-C3** (bridge modules)
- 1× **Seeed Studio XIAO ESP32-C3** (debug monitor, optional)
- 2× LED (e.g. 3 mm green, for connection status) + 2× LED (e.g. 3 mm yellow/red, for setup mode)
- 4× series resistor 220–330 Ω
- USB-C cable (for flashing and debugging)

> **Note:** The debug monitor ESP only requires a USB-C cable to the PC. No additional hardware (LEDs, resistors) is required.
> Setup mode can also be activated via the software command `ET+OPEN`, so an external hardware input on GPIO 5 is entirely optional.

---

## 4. Pin Assignment

> The pin assignment is identical for **both** modules.

| GPIO  | Label        | Function                                        |
|-------|--------------|-------------------------------------------------|
| **9** | D9 / BOOT    | Connection status LED ⚠️ see note below          |
| **10**| D10          | Setup mode LED                                  |
| **20**| D7 / UART-RX | Hardware UART receive (from external device)    |
| **21**| D6 / UART-TX | Hardware UART transmit (to external device)     |

### Wiring Diagram

```
3.3 V ──[220Ω]──► LED (anode) ──► GPIO 10  (cathode → GND)
3.3 V ──[220Ω]──► LED (anode) ──► GPIO  9  (cathode → GND)

Device ──────────────  TX  ──► GPIO 20 (UART RX)
Device ◄──────────── GPIO 21 (UART TX) ──  RX  ──
GND ─────────────────────────────────────── GND
```

> ⚠️ **GPIO 9 = BOOT button** on the XIAO ESP32-C3.  
> Connecting an LED here does **not interfere with normal operation**, but it may cause issues during flashing if GPIO 9 is held high from outside.  
> If boot problems occur → temporarily disconnect the LED circuit before flashing and reconnect it afterwards.

---

## 5. Project Structure

```
ESP_Bridge/
├── platformio.ini          ← PlatformIO configuration (bridge + debug monitor)
├── README.md               ← German documentation
├── README_EN.md            ← This file (English documentation)
├── BL.h                    ← Teensy robot: role/debug class (header)
├── BL.cpp                  ← Teensy robot: role/debug class (implementation)
├── test_bridge.py          ← Automated test and pairing script (Python)
├── include/
│   └── config.h            ← All constants and pin definitions
├── src/
│   └── main.cpp            ← Bridge firmware (identical for both bridge modules)
└── src_debug/
    └── main.cpp            ← Debug monitor firmware (third ESP, USB to PC)
```

---

## 6. Installation & Flashing

### Prerequisites

- [VS Code](https://code.visualstudio.com/) with the [PlatformIO extension](https://platformio.org/install/ide?install=vscode)
- Or [Arduino IDE 2.x](https://www.arduino.cc/en/software) with the ESP32 board package

### With PlatformIO (recommended)

```bash
# 1. Open the project
# VS Code → Open Folder → ESP_Bridge/

# 2. Flash the first bridge module
pio run --target upload

# 3. Connect the second bridge module and flash again
pio run --target upload

# 4. Flash the debug monitor ESP (third ESP)
pio run -e debug_monitor --target upload

# 5. Open the serial monitor (115200 baud)
pio device monitor                          # Bridge module
pio device monitor -e debug_monitor         # Debug monitor
```

### With Arduino IDE

1. Add the board manager URL:  
   `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Install the board: **Seeed Studio XIAO ESP32-C3**
3. Select the board and set `USB CDC On Boot: Enabled`
4. Rename `src/main.cpp` to `ESP_Bridge.ino` and copy `include/config.h` into the same folder
5. Flash both modules

> **Important:** The `USB CDC On Boot: Enabled` option is required for the serial monitor to work over USB.

---

## 7. First Start & Pairing (Setup Mode)

Since both modules run the **identical firmware**, the only difference between them is the stored peer MAC address. On first boot, no MAC address has been saved yet — a one-time **pairing** procedure is therefore required.

### Activating Setup Mode

There are two ways to enter setup mode:

1. **Software** (recommended): send the command `ET+OPEN` via USB-Serial or hardware UART
2. **Hardware** (optional): pull GPIO 5 to 3.3 V (active-high)

### Step-by-Step Guide

#### Preparation
1. Connect both modules via USB and open a terminal for each (115200 baud, USB-CDC)
2. Power both modules (or press the reset button)

#### Pairing (on **one** of the two modules)

```
1. Type ET+OPEN
   → Setup mode LED (GPIO 10) lights up
   → Setup mode is active
   → Available commands are displayed

2. ET+SCAN
   → Starts a 5-second scan
   → Output: "[SCAN] Gefunden #1: AA:BB:CC:DD:EE:FF"

3. ET+SELECT=1
   → Selects the first found peer

4. ET+SAVE
   → MAC address is saved to flash
   → Setup mode ends
   → Connection is established immediately
```

> **The second module does NOT need to be put into setup mode!**  
> It automatically detects incoming discovery packets, responds, and establishes a connection.  
> After receiving the first data packet, the sender's MAC address is automatically stored internally (auto-peer-discovery).  
> To make the pairing permanent on the second module as well:

```
ET+OPEN → ET+PEER=AA:BB:CC:DD:EE:FF → ET+SAVE
```
(Query the first module's MAC using `ET+MAC?`)

---

## 8. AT Command Reference

All commands can be sent via either **USB-Serial** or **hardware UART**.  
Format: `ET+COMMAND` — line ending: `\n` or `\r\n`

| Command                      | Setup required | Description                                             |
|------------------------------|:--------------:|---------------------------------------------------------|
| `ET+OPEN`                    | –              | Activate setup mode                                     |
| `ET+MAC?`                    | –              | Display own MAC address                                 |
| `ET+STATUS?`                 | –              | Display full status                                     |
| `ET+DEBUG`                   | –              | Toggle debug output on/off                              |
| `ET+DBGMON`                  | –              | Toggle debug monitor on/off (send to 3rd ESP)           |
| `ET+SCAN`                    | ✓              | Start a 5-second scan for ESP-NOW peers                 |
| `ET+LIST`                    | ✓              | Show results of the last scan                           |
| `ET+SELECT=N`                | ✓              | Select peer number N from the scan list                 |
| `ET+PEER=AA:BB:CC:DD:EE:FF`  | ✓              | Set peer MAC address manually                           |
| `ET+PEER?`                   | ✓              | Read the stored peer MAC address                        |
| `ET+CHANNEL=N`               | ✓              | Set Wi-Fi channel (1–13, takes effect after restart)    |
| `ET+RESET`                   | ✓              | Delete peer MAC address and all settings                |
| `ET+SAVE`                    | ✓              | Save settings to flash and exit setup mode              |

> **Note:** The commands `ET+OPEN`, `ET+MAC?`, `ET+STATUS?`, `ET+DEBUG`, and `ET+DBGMON` are **always** available, regardless of the current mode. All other commands require setup mode (activated via `ET+OPEN`).

### Example Session

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
  Firmware:      v1.1
  Eigene MAC:    34:94:54:AB:CD:EF
  Peer MAC:      34:94:54:11:22:33
  Verbunden:     NEIN
  Modus:         SETUP
  WiFi-Kanal:    6
  UART Baud:     115200
  Debug:         AUS
====================================

ET+SAVE
[NVS] Einstellungen gespeichert.
[SETUP] Setup beendet → Normaler Betrieb
[INFO] Verbunden → LED GPIO9 leuchtet
```

> **Note:** The firmware's status and debug messages are currently output in German. The `ET+` command syntax is language-independent.

---

## 9. Normal Operation

After successful pairing and a stored peer MAC address, the bridge operates fully automatically:

1. **Power on**: supply both modules with power
2. **Auto-connect**: each module loads the peer MAC from flash and registers it as an ESP-NOW peer
3. **Heartbeat**: a small keep-alive packet is sent and received every 1 second
4. **UART → ESP-NOW**: the hardware UART input is buffered every **5 ms** and transmitted wirelessly
5. **ESP-NOW → UART**: received packets are immediately forwarded to both the hardware UART **and** USB-Serial
6. **Reconnect**: if the connection is lost, the module attempts to re-register the peer every 5 seconds; after 30 seconds without a connection, ESP-NOW is fully re-initialized

### Automatic Reconnect Behavior

| Event                           | Response                                              |
|---------------------------------|-------------------------------------------------------|
| Send failure (< 5 times)        | Increment error counter, keep trying                  |
| Send failure (≥ 5 times)        | Re-register peer, report `CONNECTION LOST`            |
| Disconnected for 5 s            | Re-add peer as ESP-NOW peer                           |
| Disconnected for 30 s           | Full ESP-NOW re-initialization                        |

### Timing Analysis

```
External source sends 20 bytes every 10 ms:
  → UART input:      ~2,000 bytes/s  =  ~16 kbps
  → Buffering:       0–5 ms delay
  → ESP-NOW:         ~1–2 ms transmission time
  → Total latency:   ~3–7 ms  ✓ (well below 10 ms)
  → ESP-NOW capacity: ~250 kbps (headroom: >15×)
```

---

## 10. Debug Monitor (Third ESP)

The debug monitor allows receiving debug data from both robots on a PC without affecting the normal bridge communication.

### Concept

- A **third ESP32-C3** is connected to a PC via USB
- Both bridge ESPs send debug data via **ESP-NOW broadcast** (`PKT_DEBUG`)
- The debug monitor receives these packets and outputs them on the **USB serial console**
- Data flow is **unidirectional**: robots → PC only (not vice versa)
- The debug feature is **toggleable** via `ET+DBGMON` on the bridge ESPs
- The existing bridge communication is **not affected**

### Setup

1. **Flash the debug monitor:**
   ```bash
   pio run -e debug_monitor --target upload
   ```

2. **Enable the debug monitor** (on both bridge ESPs):
   ```
   ET+DBGMON
   [DBGMON] Debug-Monitor: AN
   ```

3. **Open the monitor:**
   ```bash
   pio device monitor -e debug_monitor
   ```

### Sending Debug Data from Teensy (BL.h / BL.cpp)

The `BLC` class provides the `sendDebug()` method to send debug data from the Teensy robot through the UART bridge to the debug monitor:

```cpp
// Send a debug message
BL.sendDebug("Ball Angle: " + String(Ball.Angle));
BL.sendDebug("Distance: " + String(Ball.Distance));
BL.sendDebug("Rolle: " + BL.Rolle);

// Enable/disable debug output
BL.setDebugEnabled(true);   // enable
BL.setDebugEnabled(false);  // disable
```

### Protocol

Debug messages are sent with the prefix `DBG:` over the UART connection:

```
DBG:Ball Angle: 180\n
```

The ESP bridge recognizes this prefix and:
- Does **not** forward the message to the peer robot (not bridge data)
- Instead sends it as a `PKT_DEBUG` via ESP-NOW broadcast
- The debug monitor receives the packet and displays it with the source MAC:

```
[34:94:54:AB:CD:EF] Ball Angle: 180
[34:94:54:11:22:33] Distance: 42
```

### Disabling the Debug Monitor

On the bridge ESPs:
```
ET+DBGMON
[DBGMON] Debug-Monitor: AUS
```

The setting is saved in NVS and persists across reboots.

---

## 11. LED Behavior

| LED              | GPIO | State               | Meaning                        |
|------------------|------|---------------------|--------------------------------|
| Setup LED        | 10   | ON (steady)         | Setup mode active              |
| Setup LED        | 10   | OFF                 | Normal operation               |
| Connection LED   | 9    | ON (steady)         | Peer reachable, connected      |
| Connection LED   | 9    | Blinking (0.5 Hz)   | Not connected / searching      |

---

## 12. Performance & Timing

### Theoretical Limits

| Parameter               | Value              |
|-------------------------|--------------------|
| ESP-NOW max. throughput | ~1 Mbps gross      |
| Payload per packet      | up to 250 bytes    |
| Minimum packet interval | ~1–2 ms            |
| Recommended max. load   | ~50–80 kB/s net    |

### Configurable Parameters (`config.h`)

| Constant                   | Default    | Description                                      |
|----------------------------|------------|--------------------------------------------------|
| `SEND_INTERVAL_MS`         | 5 ms       | UART buffer send interval                        |
| `ESPNOW_MAX_PAYLOAD`       | 240 B      | Max. payload per ESP-NOW packet                  |
| `UART_RX_BUF_SIZE`         | 512 B      | Hardware UART receive buffer size                |
| `HEARTBEAT_INTERVAL_MS`    | 1000 ms    | Keep-alive interval                              |
| `MAX_IDLE_MS`              | 3000 ms    | Timeout before marking connection as lost        |
| `ESPNOW_CHANNEL`           | 6          | Wi-Fi channel (1–13, must match on both modules) |
| `ESPNOW_SEND_RETRIES`      | 3          | Retries on send failure                          |
| `RECONNECT_INTERVAL_MS`    | 5000 ms    | Interval for peer re-registration attempts       |
| `ESPNOW_REINIT_TIMEOUT_MS` | 30000 ms   | Timeout for full ESP-NOW re-initialization       |

---

## 13. Dual-Port Operation

A key feature of this firmware is **fully symmetrical dual-port operation**: both interfaces — USB-CDC (`Serial`) and hardware UART (`Serial1`) — are treated entirely in parallel.

| Feature                              | USB-CDC (Serial) | HW-UART (Serial1) |
|--------------------------------------|:----------------:|:-----------------:|
| Send ET+ commands                    | ✓                | ✓                 |
| Receive command responses            | ✓                | ✓                 |
| Send bridge data (→ ESP-NOW)         | ✓                | ✓                 |
| Receive bridge data (ESP-NOW →)      | ✓                | ✓                 |
| Debug output (`ET+DEBUG`)            | ✓                | ✓                 |

This means a connected device can send `ET+` commands directly over the hardware UART pins without requiring a computer connected via USB.

---

## 14. Automated Test Script

The included Python script `test_bridge.py` connects to both modules simultaneously via USB-Serial, reads boot output, performs the pairing procedure automatically, and verifies the connection.

### Prerequisites

```bash
pip install pyserial
```

### Usage

```bash
# Adjust the port settings at the top of the script:
# PORT_A = "COM21"  (or e.g. /dev/ttyUSB0 on Linux/macOS)
# PORT_B = "COM22"

python test_bridge.py
```

### Script Workflow

1. Both modules are connected and boot output is collected
2. MAC addresses are automatically extracted from the output
3. If the modules are already correctly paired, pairing is skipped
4. Otherwise, `ET+OPEN` → `ET+PEER=<MAC>` → `ET+SAVE` is executed on both modules
5. The script waits for a connection confirmation via heartbeat
6. `ET+STATUS?` is queried on both modules and printed
7. A final result is shown: ✓ BRIDGE IS WORKING or ✗ Connection issue

---

## 15. Notes & Particularities

### GPIO 9 = BOOT Button
The XIAO ESP32-C3 has its built-in BOOT button on GPIO 9. An LED connected here **does not interfere with normal operation**, but may cause problems during flashing if GPIO 9 is held high by an external circuit. If in doubt, disconnect the LED's series resistor before flashing and reconnect it afterwards.

### ESP-NOW & Wi-Fi Channel
Both modules **must operate on the same channel**. Default: channel **6**. To change it, run `ET+OPEN` → `ET+CHANNEL=N` → `ET+SAVE` → restart on **both** modules. If the selected channel is shared with an active Wi-Fi network, performance may be slightly reduced due to interference.

### No Router Required
ESP-NOW operates in **ad-hoc mode** directly between the two modules. No Wi-Fi router or internet connection is needed.

### Identical Firmware for Both Modules
Both modules receive **the same firmware**. The distinction between them is determined solely by the peer MAC address stored in flash memory.

### Range & Antenna
The XIAO ESP32-C3 has an external PCB antenna. For maximum range, avoid shielding the modules with metal enclosures. If the signal is poor, try changing to a less congested channel such as `ET+CHANNEL=1` or `ET+CHANNEL=11`.

### Packet Segmentation
If the buffered UART data exceeds 240 bytes, it is automatically split into multiple ESP-NOW packets. Each packet carries a sequence number for error detection.

### Debug Mode
Debug mode can be toggled at any time using `ET+DEBUG` — even during normal operation. When enabled, detailed information about sent and received packets is printed, including hex dumps of the first 16 bytes of each payload.

### Debug Monitor
The debug monitor (`ET+DBGMON`) allows sending debug data to a third ESP connected to a PC via USB. See [Section 10](#10-debug-monitor-third-esp) for details.

---

## 16. Troubleshooting

| Problem                               | Possible Cause                             | Solution                                                       |
|---------------------------------------|--------------------------------------------|----------------------------------------------------------------|
| GPIO 9 LED blinks continuously        | No peer stored / peer unreachable          | `ET+OPEN` → scan → repeat pairing                             |
| `ET+SCAN` finds no peers              | Other module not running                   | Power on the second module; verify both use the same channel   |
| No USB-Serial output                  | CDC not enabled                            | Set `USB CDC On Boot: Enabled` in board settings               |
| UART data missing or corrupted        | Incorrect pin assignment or wiring         | Check GPIO 20 (RX) and GPIO 21 (TX)                            |
| Modules do not connect                | Different Wi-Fi channels configured        | Run `ET+STATUS?` on both modules and compare the channel       |
| High data latency                     | `SEND_INTERVAL_MS` too large               | Reduce to 2–3 ms in `config.h`                                 |
| Flash settings lost                   | Firmware re-flashed                        | Repeat pairing: `ET+OPEN` → `ET+SCAN` → `ET+SAVE`             |
| Commands not recognized               | Not in setup mode                          | Send `ET+OPEN` first (exceptions: `ET+MAC?`, `ET+STATUS?`, `ET+DEBUG`, `ET+DBGMON`) |
| Connection drops repeatedly           | Poor reception / channel interference      | Change to a different channel using `ET+CHANNEL=N` on both modules |
| Debug monitor receives no data        | Debug monitor not enabled                  | Send `ET+DBGMON` on both bridge ESPs                           |
| Debug monitor receives no data        | Wrong Wi-Fi channel                        | All three ESPs must be on the same channel                     |
