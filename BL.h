#pragma once
#include "Elementar.h"

// Klasse zur Steuerung der Ballrolle und Kommunikation
class BLC {

    private:
        // Dekodiert eingehende Nachrichten
        void decode(String message1);
    public:
        String Rolle="N"; // Aktuelle Rolle (z.B. "N" für Neutral)
        String message;   // Letzte empfangene Nachricht

        bool SinglePlayer; // True, wenn nur ein Spieler aktiv ist
        bool debugEnabled = true; // Debug-Ausgabe aktiviert (abschaltbar)
        
        // Zeigt die aktuelle Rolle an
        void showRolle();
        // Führt die Rollenlogik aus (Kommunikation, Steuerung)
        void doRolle();

        // Sendet Debug-Daten ueber die UART-Bridge zum Debug-Monitor-ESP
        // Die Nachricht wird mit dem Prefix "DBG:" markiert und per '\n'
        // terminiert, sodass die ESP-Bridge sie als Debug erkennt und
        // an den Debug-Monitor weiterleitet (nicht an den Peer-Roboter).
        void sendDebug(String msg);

        // Aktiviert/Deaktiviert die Debug-Ausgabe
        void setDebugEnabled(bool enabled);
};

// Globale Instanz der Rollenklasse
extern BLC BL;