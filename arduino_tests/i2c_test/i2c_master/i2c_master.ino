// ============================================================
//  I2C Test – Uno A (über ESP32 Bridge, I2C-Modus)
//  ============================================================
//  Dieser Uno ist I2C-Master und spricht mit seinem lokalen
//  ESP32-C3 (Bridge-Modul A) als I2C-Slave:
//    Uno A A4 (SDA)  -> ESP A GPIO6 (SDA)
//    Uno A A5 (SCL)  -> ESP A GPIO7 (SCL)
//    Uno A GND       -> ESP A GND
//
//  Bridge-Protokoll (siehe Haupt-README):
//    Register 0x01: 10 Byte an Peer senden
//    Register 0x02: 10 Byte vom Peer lesen
//
//  Voraussetzung:
//    Beide ESP-Module sind gepairt, ESP A im I2C-Modus.
// ============================================================

#include <Wire.h>

const uint8_t  BRIDGE_ADDR = 0x77;   // Standard aus ET+I2CAddr=0xEE (7-Bit)
const uint8_t  REG_WRITE   = 0x01;
const uint8_t  REG_READ    = 0x02;
const uint32_t USB_BAUD    = 115200;
const uint32_t SEND_PERIOD = 1000;   // ms zwischen Zyklen

uint16_t cycleCount = 0;

const uint8_t PAYLOAD_LEN = 10;

void setup() {
    Wire.begin();          // I2C Master (Uno)
    Serial.begin(USB_BAUD);
    while (!Serial) {}

    Serial.println(F("========================================"));
    Serial.println(F("  I2C Test – Uno A via ESP Bridge"));
    Serial.println(F("========================================"));
    Serial.print(F("Bridge-Adresse (7-Bit): 0x"));
    Serial.println(BRIDGE_ADDR, HEX);
    Serial.println(F("Register 0x01=TX  0x02=RX"));
    Serial.println();
}

void loop() {
    cycleCount++;

    // ---- Testdaten (10 Byte) ueber Register 0x01 senden ----
    uint8_t txData[PAYLOAD_LEN] = {
        'A',                                    // Senderkennung
        (uint8_t)(cycleCount >> 8),
        (uint8_t)(cycleCount & 0xFF),
        0xAA,
        0xBB,
        0xCC,
        0x11,
        0x22,
        0x33,
        0x44
    };

    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(REG_WRITE);
    Wire.write(txData, PAYLOAD_LEN);
    uint8_t err = Wire.endTransmission();

    Serial.print(F("[I2C TX] Zyklus #"));
    Serial.print(cycleCount);
    Serial.print(F("  Daten (10 B): "));
    for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
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

    // ---- Register 0x02 setzen und 10 Byte Antwort lesen ----
    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(REG_READ);
    uint8_t regErr = Wire.endTransmission();

    uint8_t rxLen = Wire.requestFrom(BRIDGE_ADDR, PAYLOAD_LEN);
    Serial.print(F("[I2C RX] Register 0x02 ("));
    Serial.print(rxLen);
    Serial.print(F(" Byte): "));

    for (uint8_t i = 0; i < rxLen; i++) {
        uint8_t b = Wire.read();
        if (b < 0x10) Serial.print('0');
        Serial.print(b, HEX);
        Serial.print(' ');
    }
    if (regErr != 0) {
        Serial.print(F("  [Reg-Write Fehler: "));
        Serial.print(regErr);
        Serial.print(F("]"));
    }
    Serial.println();
    Serial.println();

    delay(SEND_PERIOD);
}
