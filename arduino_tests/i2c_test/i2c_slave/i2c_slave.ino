// ============================================================
//  I2C Test – Slave (Arduino Uno)
//  ============================================================
//  Verdrahtung zwischen den beiden Unos:
//    Slave SDA (Pin A4)  →  Master SDA (Pin A4)
//    Slave SCL (Pin A5)  →  Master SCL (Pin A5)
//    Slave GND           →  Master GND
//
//  Ein externer Pull-up-Widerstand (4,7 kΩ) von SDA nach 5 V
//  und von SCL nach 5 V wird empfohlen.
//
//  Slave-Adresse: 0x08  (muss mit Master uebereinstimmen)
//
//  Bedienung:
//    1. Diesen Sketch auf den Slave-Uno hochladen.
//    2. Master-Sketch auf zweiten Uno hochladen.
//    3. Serial Monitor beider Unos bei 115200 Baud oeffnen.
//    Der Slave zeigt empfangene Daten an und antwortet mit
//    einem modifizierten Datensatz (erstes Byte = 'S').
// ============================================================

#include <Wire.h>

const uint8_t SLAVE_ADDR    = 0x08;
const uint32_t USB_BAUD     = 115200;
const uint8_t PAYLOAD_LEN   = 6;

// Puffer fuer empfangene und zu sendende Daten
volatile uint8_t rxBuf[PAYLOAD_LEN];
volatile uint8_t rxLen = 0;
volatile bool    newData = false;

// Callback: Master hat Daten geschrieben
void onReceive(int numBytes) {
    rxLen = 0;
    while (Wire.available() && rxLen < PAYLOAD_LEN) {
        rxBuf[rxLen++] = Wire.read();
    }
    newData = true;
}

// Callback: Master fragt Daten an
void onRequest() {
    // Antwort: erstes Byte 'S' (Slave), Rest gespiegelt
    uint8_t txBuf[PAYLOAD_LEN];
    txBuf[0] = 'S';
    for (uint8_t i = 1; i < PAYLOAD_LEN && i < rxLen; i++) {
        txBuf[i] = rxBuf[i];
    }
    // Fehlende Bytes mit 0x00 auffüllen
    for (uint8_t i = rxLen; i < PAYLOAD_LEN; i++) {
        txBuf[i] = 0x00;
    }
    Wire.write(txBuf, PAYLOAD_LEN);
}

void setup() {
    Wire.begin(SLAVE_ADDR);      // I2C Slave mit Adresse 0x08
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);

    Serial.begin(USB_BAUD);
    while (!Serial) {}

    Serial.println(F("========================================"));
    Serial.println(F("  I2C Test – Slave"));
    Serial.println(F("========================================"));
    Serial.print(F("Slave-Adresse: 0x"));
    Serial.println(SLAVE_ADDR, HEX);
    Serial.println(F("SDA=A4  SCL=A5"));
    Serial.println(F("Warte auf Master …"));
    Serial.println();
}

void loop() {
    if (newData) {
        newData = false;

        Serial.print(F("[I2C RX] Empfangen ("));
        Serial.print(rxLen);
        Serial.print(F(" Byte): "));
        for (uint8_t i = 0; i < rxLen; i++) {
            if (rxBuf[i] < 0x10) Serial.print('0');
            Serial.print(rxBuf[i], HEX);
            Serial.print(' ');
        }
        Serial.println();

        // Zeige den zusammengestellten Antwort-Frame
        Serial.print(F("[I2C TX] Antwort:   "));
        Serial.print(F("53 "));  // 'S' = 0x53
        for (uint8_t i = 1; i < PAYLOAD_LEN && i < rxLen; i++) {
            if (rxBuf[i] < 0x10) Serial.print('0');
            Serial.print(rxBuf[i], HEX);
            Serial.print(' ');
        }
        Serial.println();
        Serial.println();
    }
}
