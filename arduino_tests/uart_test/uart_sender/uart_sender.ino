// ============================================================
//  UART Test – Sender (Arduino Uno via ESP32 Bridge)
//  ============================================================
//  Verdrahtung Sender-Uno <-> lokales ESP32-C3 Bridge-Modul A:
//    Sender Pin 10 (SW-TX)  -> ESP A GPIO20 (UART-RX)
//    Sender Pin 11 (SW-RX)  <- ESP A GPIO21 (UART-TX)
//    Sender GND             -> ESP A GND
//
//  Die Verbindung zum Empfaenger erfolgt drahtlos ueber das
//  zweite ESP32-C3 Bridge-Modul B (ESP-NOW).
//
//  Der Hardware-UART (Pin 0/1) des Uno bleibt fuer den
//  Serial Monitor frei, deshalb wird SoftwareSerial genutzt.
//
//  Bedienung:
//    1. Sketch auf diesen Uno hochladen.
//    2. Serial Monitor bei 115200 Baud oeffnen.
//    3. Empfaenger-Sketch auf Uno B starten.
//    4. Beide ESP32-Bridge-Module muessen gepairt sein.
//    Der Sender schickt alle 1 s ein Testpaket und zeigt
//    Antworten vom entfernten Uno B an.
// ============================================================

#include <SoftwareSerial.h>

// SoftwareSerial: RX=Pin 11, TX=Pin 10
SoftwareSerial swSerial(11, 10);

const uint32_t USB_BAUD    = 115200;  // Serial Monitor
const uint32_t SW_BAUD     = 9600;   // SoftwareSerial (max. zuverlässig)
const uint32_t SEND_PERIOD = 1000;   // ms zwischen zwei Testpaketen

uint32_t lastSend    = 0;
uint16_t packetCount = 0;

void setup() {
    Serial.begin(USB_BAUD);
    while (!Serial) { /* warten bis USB-CDC bereit ist */ }

    swSerial.begin(SW_BAUD);

    Serial.println(F("========================================"));
    Serial.println(F("  UART Test – Sender via ESP Bridge"));
    Serial.println(F("========================================"));
    Serial.print(F("SoftwareSerial TX=Pin10  RX=Pin11  @ "));
    Serial.print(SW_BAUD);
    Serial.println(F(" Baud"));
    Serial.println(F("Warte auf Empfänger …"));
    Serial.println();
}

void loop() {
    uint32_t now = millis();

    // ---- Testpaket senden ----
    if (now - lastSend >= SEND_PERIOD) {
        lastSend = now;
        packetCount++;

        // Format: "PKT:<nr>:TEST_DATA_<nr>"
        String msg = "PKT:";
        msg += packetCount;
        msg += ":TEST_DATA_";
        msg += packetCount;

        swSerial.println(msg);   // an Empfaenger senden

        Serial.print(F("[TX] "));
        Serial.println(msg);     // im Serial Monitor anzeigen
    }

    // ---- Antwort vom Empfaenger auslesen ----
    while (swSerial.available()) {
        String line = swSerial.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            Serial.print(F("[RX] "));
            Serial.println(line);
        }
    }
}
