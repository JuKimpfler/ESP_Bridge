// ============================================================
//  I2C Test – Uno B / Receiver (über ESP32 Bridge, I2C-Modus)
//  ============================================================
//  Dieser Uno ist I2C-Controller und spricht mit seinem lokalen
//  ESP32-C3 (Bridge-Modul B) als I2C-Peripheral:
//    Uno B A4 (SDA)  -> ESP B GPIO6 (SDA)
//    Uno B A5 (SCL)  -> ESP B GPIO7 (SCL)
//    Uno B GND       -> ESP B GND
//
//  Der Sketch pollt Register 0x02 und sendet fuer neue Pakete
//  eine Bestaetigung ueber Register 0x01 zurueck.
// ============================================================

#include <Wire.h>

const uint8_t BRIDGE_ADDR   = 0x77;   // Standard aus ET+I2CAddr=0xEE (7-Bit)
const uint8_t REG_WRITE     = 0x01;
const uint8_t REG_READ      = 0x02;
const uint32_t USB_BAUD     = 115200;
const uint8_t PAYLOAD_LEN   = 10;
const uint32_t POLL_PERIOD  = 200;

uint32_t lastPoll = 0;
uint16_t lastSeq  = 0;

bool readPeerData(uint8_t *buf, uint8_t len) {
    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(REG_READ);
    uint8_t err = Wire.endTransmission();
    if (err != 0) return false;

    uint8_t got = Wire.requestFrom(BRIDGE_ADDR, len);
    if (got != len) return false;

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = Wire.read();
    }
    return true;
}

bool sendAck(uint16_t seq) {
    uint8_t ack[PAYLOAD_LEN] = {
        'B',
        (uint8_t)(seq >> 8),
        (uint8_t)(seq & 0xFF),
        'O', 'K',
        0, 0, 0, 0, 0
    };

    Wire.beginTransmission(BRIDGE_ADDR);
    Wire.write(REG_WRITE);
    Wire.write(ack, PAYLOAD_LEN);
    return Wire.endTransmission() == 0;
}

void setup() {
    Wire.begin();                // I2C Controller (Uno)
    Serial.begin(USB_BAUD);
    while (!Serial) {}

    Serial.println(F("========================================"));
    Serial.println(F("  I2C Test – Uno B via ESP Bridge"));
    Serial.println(F("========================================"));
    Serial.print(F("Bridge-Adresse (7-Bit): 0x"));
    Serial.println(BRIDGE_ADDR, HEX);
    Serial.println(F("Polling Register 0x02 ..."));
    Serial.println();
}

void loop() {
    uint32_t now = millis();
    if (now - lastPoll < POLL_PERIOD) return;
    lastPoll = now;

    uint8_t rxBuf[PAYLOAD_LEN];
    if (!readPeerData(rxBuf, PAYLOAD_LEN)) return;

    uint16_t seq = ((uint16_t)rxBuf[1] << 8) | rxBuf[2];
    if (seq == 0 || seq == lastSeq || rxBuf[0] != 'A') return;
    lastSeq = seq;

    Serial.print(F("[I2C RX] Paket #"));
    Serial.print(seq);
    Serial.print(F(": "));
    for (uint8_t i = 0; i < PAYLOAD_LEN; i++) {
        if (rxBuf[i] < 0x10) Serial.print('0');
        Serial.print(rxBuf[i], HEX);
        Serial.print(' ');
    }
    Serial.println();

    if (sendAck(seq)) {
        Serial.print(F("[I2C TX] ACK fuer #"));
        Serial.println(seq);
    } else {
        Serial.print(F("[I2C TX] ACK Fehler fuer #"));
        Serial.println(seq);
    }
    Serial.println();
}
