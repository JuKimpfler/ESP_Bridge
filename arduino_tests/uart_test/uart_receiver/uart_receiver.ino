// ============================================================
//  UART Test – Empfaenger (Arduino Uno via ESP32 Bridge)
//  ============================================================
//  Verdrahtung Empfaenger-Uno <-> lokales ESP32-C3 Bridge-Modul B:
//    Empfaenger Pin 11 (SW-RX)  <- ESP B GPIO21 (UART-TX)
//    Empfaenger Pin 10 (SW-TX)  -> ESP B GPIO20 (UART-RX)
//    Empfaenger GND             -> ESP B GND
//
//  Die Verbindung zum Sender erfolgt drahtlos ueber das zweite
//  ESP32-C3 Bridge-Modul A (ESP-NOW).
//
//  Bedienung:
//    1. Sketch auf diesen Uno hochladen.
//    2. Serial Monitor bei 115200 Baud oeffnen.
//    3. Sender-Sketch auf Uno A starten.
//    4. Beide ESP32-Bridge-Module muessen gepairt sein.
//    Der Empfaenger zeigt jedes eingehende Paket an und
//    schickt eine Bestaetigung zurueck.
// ============================================================

#include <SoftwareSerial.h>

// SoftwareSerial: RX=Pin 11, TX=Pin 10
SoftwareSerial swSerial(11, 10);

const uint32_t USB_BAUD = 115200;  // Serial Monitor
const uint32_t SW_BAUD  = 9600;   // SoftwareSerial

uint32_t packetCount = 0;

void setup() {
    Serial.begin(USB_BAUD);
    while (!Serial) { /* warten bis USB-CDC bereit ist */ }

    swSerial.begin(SW_BAUD);

    Serial.println(F("========================================"));
    Serial.println(F("  UART Test – Empfaenger via ESP Bridge"));
    Serial.println(F("========================================"));
    Serial.print(F("SoftwareSerial TX=Pin10  RX=Pin11  @ "));
    Serial.print(SW_BAUD);
    Serial.println(F(" Baud"));
    Serial.println(F("Warte auf Datenpakete…"));
    Serial.println();
}

void loop() {
    // ---- Eingehende Pakete lesen ----
    while (swSerial.available()) {
        String line = swSerial.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;

        packetCount++;

        Serial.print(F("[RX] Paket #"));
        Serial.print(packetCount);
        Serial.print(F(": "));
        Serial.println(line);

        // Bestaetigung zuruecksenden: "ACK:<nr>:OK"
        String ack = "ACK:";
        ack += packetCount;
        ack += ":OK";

        swSerial.println(ack);   // an Sender zurueck

        Serial.print(F("[TX] Bestätigung: "));
        Serial.println(ack);
        Serial.println();
    }
}
