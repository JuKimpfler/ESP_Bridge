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
        
        // Zeigt die aktuelle Rolle an
        void showRolle();
        // Führt die Rollenlogik aus (Kommunikation, Steuerung)
        void doRolle();
};

// Globale Instanz der Rollenklasse
extern BLC BL;