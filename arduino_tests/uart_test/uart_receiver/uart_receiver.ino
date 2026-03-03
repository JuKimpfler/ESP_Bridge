// ============================================================
//  UART Test – Empfaenger (Arduino Uno)
//  ============================================================
//  Verdrahtung zwischen den beiden Unos:
//    Empfaenger Pin 11 (SW-RX)  ←  Sender Pin 10 (SW-TX)
//    Empfaenger Pin 10 (SW-TX)  →  Sender Pin 11 (SW-RX)
//    Empfaenger GND             →  Sender GND
//
//  Der Hardware-UART (Pin 0/1) bleibt fuer den Serial Monitor
//  (USB-Verbindung zum PC) frei – kein Kabel-Umstecken noetig.
//
//  Bedienung:
//    1. Sketch auf diesen Uno hochladen.
//    2. Serial Monitor bei 115200 Baud oeffnen.
//    3. Sender-Sketch auf zweiten Uno laden und verbinden.
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
    Serial.println(F("  UART Test – Empfaenger"));
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
