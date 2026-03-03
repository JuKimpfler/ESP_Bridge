// ============================================================
//  I2C Test – Master (Arduino Uno)
//  ============================================================
//  Verdrahtung zwischen den beiden Unos:
//    Master SDA (Pin A4)  →  Slave SDA (Pin A4)
//    Master SCL (Pin A5)  →  Slave SCL (Pin A5)
//    Master GND           →  Slave GND
//
//  Ein externer Pull-up-Widerstand (4,7 kΩ) von SDA nach 5 V
//  und von SCL nach 5 V wird empfohlen (oft auf I2C-Modulen
//  bereits vorhanden).
//
//  Slave-Adresse: 0x08
//
//  Bedienung:
//    1. Slave-Sketch auf zweiten Uno hochladen.
//    2. Diesen Sketch auf Master-Uno hochladen.
//    3. Serial Monitor beider Unos bei 115200 Baud oeffnen.
//    Der Master schreibt alle 1 s Testdaten an den Slave und
//    liest anschliessend die Antwort. Beide Seiten zeigen die
//    ausgetauschten Daten im Serial Monitor an.
// ============================================================

#include <Wire.h>

const uint8_t  SLAVE_ADDR  = 0x08;
const uint32_t USB_BAUD    = 115200;
const uint32_t SEND_PERIOD = 1000;   // ms zwischen Zyklen

uint16_t cycleCount = 0;

// Testnutzlast: 6 Byte Schreib-Daten
const uint8_t TX_PAYLOAD_LEN = 6;
// Anzahl Byte, die vom Slave gelesen werden
const uint8_t RX_PAYLOAD_LEN = 6;

void setup() {
    Wire.begin();          // I2C Master
    Serial.begin(USB_BAUD);
    while (!Serial) {}

    Serial.println(F("========================================"));
    Serial.println(F("  I2C Test – Master"));
    Serial.println(F("========================================"));
    Serial.print(F("Slave-Adresse: 0x"));
    Serial.println(SLAVE_ADDR, HEX);
    Serial.println(F("SDA=A4  SCL=A5"));
    Serial.println();
}

void loop() {
    cycleCount++;

    // ---- Testdaten zusammenstellen ----
    // Format: 'M', highByte(cycleCount), lowByte(cycleCount), 0xAA, 0xBB, 0xFF
    uint8_t txData[TX_PAYLOAD_LEN] = {
        'M',
        (uint8_t)(cycleCount >> 8),
        (uint8_t)(cycleCount & 0xFF),
        0xAA,
        0xBB,
        0xFF
    };

    // ---- An Slave schreiben ----
    Wire.beginTransmission(SLAVE_ADDR);
    Wire.write(txData, TX_PAYLOAD_LEN);
    uint8_t err = Wire.endTransmission();

    Serial.print(F("[I2C TX] Zyklus #"));
    Serial.print(cycleCount);
    Serial.print(F("  Daten: "));
    for (uint8_t i = 0; i < TX_PAYLOAD_LEN; i++) {
        if (txData[i] < 0x10) Serial.print('0');
        Serial.print(txData[i], HEX);
        Serial.print(' ');
    }
    if (err == 0) {
        Serial.println(F("→ OK"));
    } else {
        Serial.print(F("→ Fehler: "));
        Serial.println(err);
    }

    // ---- Vom Slave lesen ----
    uint8_t rxLen = Wire.requestFrom(SLAVE_ADDR, RX_PAYLOAD_LEN);
    Serial.print(F("[I2C RX] Antwort ("));
    Serial.print(rxLen);
    Serial.print(F(" Byte): "));

    for (uint8_t i = 0; i < rxLen; i++) {
        uint8_t b = Wire.read();
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');
    }
    Serial.println();
    Serial.println();

    delay(SEND_PERIOD);
}
