# UART Test – Zwei Arduino Unos

Dieses Testprojekt lässt zwei Arduino Unos über **SoftwareSerial** (Software-UART) miteinander kommunizieren. Der Hardware-UART (Pin 0/1) bleibt für den Serial Monitor (USB-Verbindung zum PC) frei.

---

## Verdrahtung

```
Uno A (Sender)          Uno B (Empfänger)
─────────────           ─────────────────
Pin 10 (SW-TX) ──────→  Pin 11 (SW-RX)
Pin 11 (SW-RX) ←──────  Pin 10 (SW-TX)
GND            ──────── GND
```

> **Hinweis:** Beide Unos können gleichzeitig per USB am PC angeschlossen sein – der Serial Monitor funktioniert auf beiden unabhängig.

---

## Sketches

| Datei | Funktion |
|---|---|
| `uart_sender/uart_sender.ino` | Sendet jede Sekunde ein Testpaket (`PKT:<nr>:TEST_DATA_<nr>`) und zeigt eingehende Bestätigungen an |
| `uart_receiver/uart_receiver.ino` | Empfängt Pakete, zeigt sie an und sendet eine Bestätigung (`ACK:<nr>:OK`) zurück |

---

## Inbetriebnahme

1. `uart_receiver.ino` auf **Uno B** hochladen.
2. `uart_sender.ino` auf **Uno A** hochladen.
3. Verdrahtung gemäß obiger Tabelle herstellen.
4. Serial Monitor beider Unos bei **115200 Baud** öffnen.

### Erwartete Ausgabe – Sender (Uno A)

```
========================================
  UART Test – Sender
========================================
SoftwareSerial TX=Pin10  RX=Pin11  @ 9600 Baud
Warte auf Empfaenger …

[TX] PKT:1:TEST_DATA_1
[RX] ACK:1:OK
[TX] PKT:2:TEST_DATA_2
[RX] ACK:2:OK
...
```

### Erwartete Ausgabe – Empfänger (Uno B)

```
========================================
  UART Test – Empfaenger
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
