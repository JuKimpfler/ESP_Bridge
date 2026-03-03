# I2C Test – Zwei Arduino Unos über ESP32-Bridge (I2C-Modus)

Dieses Testprojekt verbindet zwei Arduino Unos **nicht direkt** miteinander, sondern über die im Projekt beschriebene **ESP32-Bridge** (ESP-NOW).

- **Uno A** spricht per I2C mit **ESP A** und sendet Testdaten.
- **Uno B** spricht per I2C mit **ESP B**, liest die Daten und sendet ein ACK zurück.

---

## Verdrahtung

```
Uno A (i2c_master)         ESP A (I2C-Slave, ComMode=2)
──────────────────         ─────────────────────────────
A4 (SDA)             ────► GPIO6 (SDA)
A5 (SCL)             ────► GPIO7 (SCL)
GND                  ────► GND

Uno B (i2c_slave)          ESP B (I2C-Slave, ComMode=2)
─────────────────          ─────────────────────────────
A4 (SDA)             ────► GPIO6 (SDA)
A5 (SCL)             ────► GPIO7 (SCL)
GND                  ────► GND
```

> Die beiden Unos werden **nicht** direkt per SDA/SCL miteinander verbunden.
>  
> Für I2C sind Pull-ups erforderlich (oft auf ESP-/I2C-Boards bereits vorhanden).

---

## Sketches

| Datei | Funktion |
|---|---|
| `i2c_master/i2c_master.ino` | Uno A sendet zyklisch 10 Byte über Register `0x01` an ESP A und liest Register `0x02` |
| `i2c_slave/i2c_slave.ino` | Uno B pollt Register `0x02` auf ESP B und sendet ACK über Register `0x01` zurück |

> Hinweis: Die Dateinamen `i2c_master`/`i2c_slave` sind historisch; in der Bridge-Topologie arbeiten beide Unos als I2C-Master gegenüber ihrem lokalen ESP32.

**Standard-I2C-Adresse der Bridge:** `0x77` (7-Bit, entspricht `ET+I2CAddr=0xEE`)

---

## Inbetriebnahme

1. Beide ESP32-C3 Module pairen (siehe Haupt-README, `ET+PEER`, `ET+SAVE`).
2. Auf **beiden ESPs** I2C-Modus aktivieren:
   - `ET+OPEN`
   - `ET+ComMode=2`
   - optional `ET+I2CAddr=0xEE`
   - `ET+SAVE` (Neustart)
3. `i2c_master.ino` auf **Uno A** hochladen.
4. `i2c_slave.ino` auf **Uno B** hochladen.
5. Verdrahtung gemäß obiger Tabelle herstellen.
6. Serial Monitor beider Unos bei **115200 Baud** öffnen.

### Erwartete Ausgabe – Master (Uno A)

```
========================================
  I2C Test – Uno A via ESP Bridge
========================================
Bridge-Adresse (7-Bit): 0x77
Register 0x01=TX  0x02=RX

[I2C TX] Zyklus #1  Daten (10 B): 41 00 01 AA BB CC 11 22 33 44 → OK
[I2C RX] Register 0x02 (10 Byte): 42 00 01 4F 4B 00 00 00 00 00

[I2C TX] Zyklus #2  Daten (10 B): 41 00 02 AA BB CC 11 22 33 44 → OK
[I2C RX] Register 0x02 (10 Byte): 42 00 02 4F 4B 00 00 00 00 00
...
```

### Erwartete Ausgabe – Slave (Uno B)

```
========================================
  I2C Test – Uno B via ESP Bridge
========================================
Bridge-Adresse (7-Bit): 0x77
Polling Register 0x02 ...

[I2C RX] Paket #1: 41 00 01 AA BB CC 11 22 33 44
[I2C TX] ACK fuer #1

[I2C RX] Paket #2: 41 00 02 AA BB CC 11 22 33 44
[I2C TX] ACK fuer #2
...
```

---

## Technische Details

- **Rolle beider Unos:** I2C-Master (`Wire.begin()`)
- **Bridge-Protokoll:** Register `0x01` (Write), Register `0x02` (Read), je **10 Byte**
- **Bibliothek:** `Wire` (im Arduino-Core enthalten, kein zusätzlicher Download nötig)
- **Sendeintervall Uno A:** 1 Sekunde
