# I2C Test – Zwei Arduino Unos

Dieses Testprojekt lässt zwei Arduino Unos über den **I²C-Bus** miteinander kommunizieren. Ein Uno agiert als **Master**, der andere als **Slave**.

---

## Verdrahtung

```
Uno A (Master)          Uno B (Slave)
──────────────          ─────────────
Pin A4 (SDA)  ──────── Pin A4 (SDA)
Pin A5 (SCL)  ──────── Pin A5 (SCL)
GND           ──────── GND

            5 V ──┬── 4,7 kΩ ── SDA
                  └── 4,7 kΩ ── SCL
```

> **Pull-up-Widerstände:** 4,7 kΩ von SDA nach 5 V und von SCL nach 5 V werden empfohlen. Auf vielen I2C-Breakout-Boards sind diese bereits vorhanden.

> **Hinweis:** Beide Unos können gleichzeitig per USB am PC angeschlossen sein.

---

## Sketches

| Datei | Funktion |
|---|---|
| `i2c_master/i2c_master.ino` | Schreibt jede Sekunde 6 Byte Testdaten an den Slave und liest anschließend 6 Byte Antwort |
| `i2c_slave/i2c_slave.ino` | Empfängt Daten vom Master, zeigt sie an und antwortet mit einem modifizierten Frame (erstes Byte = `'S'`) |

**Slave-Adresse:** `0x08`

---

## Inbetriebnahme

1. `i2c_slave.ino` auf **Uno B** hochladen.
2. `i2c_master.ino` auf **Uno A** hochladen.
3. Verdrahtung gemäß obiger Tabelle herstellen.
4. Serial Monitor beider Unos bei **115200 Baud** öffnen.

### Erwartete Ausgabe – Master (Uno A)

```
========================================
  I2C Test – Master
========================================
Slave-Adresse: 0x8
SDA=A4  SCL=A5

[I2C TX] Zyklus #1  Daten: 4D 00 01 AA BB FF → OK
[I2C RX] Antwort (6 Byte): 53 00 01 AA BB FF

[I2C TX] Zyklus #2  Daten: 4D 00 02 AA BB FF → OK
[I2C RX] Antwort (6 Byte): 53 00 02 AA BB FF
...
```

### Erwartete Ausgabe – Slave (Uno B)

```
========================================
  I2C Test – Slave
========================================
Slave-Adresse: 0x8
SDA=A4  SCL=A5
Warte auf Master …

[I2C RX] Empfangen (6 Byte): 4D 00 01 AA BB FF
[I2C TX] Antwort:   53 00 01 AA BB FF

[I2C RX] Empfangen (6 Byte): 4D 00 02 AA BB FF
[I2C TX] Antwort:   53 00 02 AA BB FF
...
```

---

## Technische Details

- **I2C-Taktrate:** Standard 100 kHz (Arduino-Default)
- **Nutzlast:** 6 Byte pro Zyklus (erweiterbar)
- **Bibliothek:** `Wire` (im Arduino-Core enthalten, kein zusätzlicher Download nötig)
- **Zyklusrate:** 1 Sekunde
