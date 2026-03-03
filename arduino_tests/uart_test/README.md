# UART Test – Zwei Arduino Unos über ESP32-Bridge (UART-Modus)

Dieses Testprojekt verbindet zwei Arduino Unos **nicht direkt** miteinander, sondern über die ESP32-Bridge im **UART-Modus**.

Auf beiden Unos wird dafür **SoftwareSerial** verwendet, damit der Hardware-UART (Pin 0/1) für den Serial Monitor frei bleibt.

---

## Verdrahtung

```
Uno A (uart_sender)         ESP A (ComMode=1 / UART)
───────────────────         ─────────────────────────
Pin 10 (SW-TX)        ────► GPIO20 (UART-RX)
Pin 11 (SW-RX)        ◄──── GPIO21 (UART-TX)
GND                   ────► GND

Uno B (uart_receiver)       ESP B (ComMode=1 / UART)
─────────────────────       ─────────────────────────
Pin 10 (SW-TX)        ────► GPIO20 (UART-RX)
Pin 11 (SW-RX)        ◄──── GPIO21 (UART-TX)
GND                   ────► GND
```

> Die beiden Unos werden **nicht** direkt per TX/RX miteinander verbunden.

---

## Sketches

| Datei | Funktion |
|---|---|
| `uart_sender/uart_sender.ino` | Sendet jede Sekunde ein Testpaket (`PKT:<nr>:TEST_DATA_<nr>`) und zeigt eingehende Bestätigungen an |
| `uart_receiver/uart_receiver.ino` | Empfängt Pakete, zeigt sie an und sendet eine Bestätigung (`ACK:<nr>:OK`) zurück |

---

## Inbetriebnahme

1. Beide ESP32-C3 Module pairen (siehe Haupt-README, `ET+PEER`, `ET+SAVE`).
2. Sicherstellen, dass beide ESPs im UART-Modus laufen (`ET+ComMode=1`).
3. `uart_receiver.ino` auf **Uno B** hochladen.
4. `uart_sender.ino` auf **Uno A** hochladen.
5. Verdrahtung gemäß obiger Tabelle herstellen.
6. Serial Monitor beider Unos bei **115200 Baud** öffnen.

### Erwartete Ausgabe – Sender (Uno A)

```
========================================
  UART Test – Sender via ESP Bridge
========================================
SoftwareSerial TX=Pin10  RX=Pin11  @ 9600 Baud
Warte auf Empfänger …

[TX] PKT:1:TEST_DATA_1
[RX] ACK:1:OK
[TX] PKT:2:TEST_DATA_2
[RX] ACK:2:OK
...
```

### Erwartete Ausgabe – Empfänger (Uno B)

```
========================================
  UART Test – Empfaenger via ESP Bridge
========================================
SoftwareSerial TX=Pin10  RX=Pin11  @ 9600 Baud
Warte auf Datenpakete …

[RX] Paket #1: PKT:1:TEST_DATA_1
[TX] Bestaetigung: ACK:1:OK

[RX] Paket #2: PKT:2:TEST_DATA_2
[TX] Bestaetigung: ACK:2:OK
...
```

---

## Technische Details

- **SoftwareSerial-Baudrate:** 9600 Baud (zuverlässiger Betrieb auf dem Uno)
- **Serial-Monitor-Baudrate:** 115200 Baud
- **Bibliothek:** `SoftwareSerial` (im Arduino-Core enthalten, kein zusätzlicher Download nötig)
