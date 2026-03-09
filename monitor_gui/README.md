# ESP Bridge Debug Monitor GUI

Eine moderne Python-GUI zur Visualisierung der ESP-NOW Debug-Daten.

## Voraussetzungen

- Python 3.10+
- Pakete: `pyserial`, `matplotlib`
- ESP32-C3 Debug-Monitor per USB angeschlossen

## Installation

```bash
cd monitor_gui
pip install -r requirements.txt
```

## Starten

```bash
python monitor_gui.py
```

## Funktionen

### ⚙ Einstellungen

- **Serieller Port** – Auswahl des COM-Ports (Debug-Monitor ESP32)
- **Baudrate** – Standard: 115200
- **Verbinden / Trennen** – Startet / stoppt die serielle Verbindung
- **MAC-Zuordnung** – Zeigt erkannte MAC-Adressen und ihre Roboter-Zuordnung
- **Daten zurücksetzen** – Löscht alle gesammelten Daten

### 📋 Tabelle

- Zeigt alle empfangenen Datenwerte mit **Name**, **Typ**, **aktueller Wert**, **Min** und **Max**
- Unterstützte Datentypen: `integer`, `double`, `string`, `bool`
- **CSV-Export** – Exportiert die aktuelle Tabelle als CSV-Datei (Semikolon-getrennt)
- Umschaltung zwischen **Robot 1** und **Robot 2** über den Button oben rechts

### 📈 Monitor

- **Live-Diagramm** – Zeigt numerische Daten als Echtzeit-Graph
- **Zeitfenster** – Schieberegler zum Einstellen der angezeigten Datenlänge (5–120 Sekunden)
- **Datenströme ein/ausblenden** – Checkboxen für jeden Datenstrom

## Datenformat

Die GUI erwartet das Ausgabeformat des ESP32-C3 Debug-Monitors:

```
[MAC_ADRESSE] Schlüssel: Wert
```

Beispiel:
```
[34:94:54:AB:CD:EF] Ball Angle: 180
[34:94:54:AB:CD:EF] Distance: 42.5
[34:94:54:11:22:33] Active: true
[34:94:54:11:22:33] Status: OK
```

- Die **erste** erkannte MAC-Adresse wird **Robot 1** zugeordnet
- Die **zweite** erkannte MAC-Adresse wird **Robot 2** zugeordnet

## Roboter-Umschaltung

Über den **⮂ Robot 1 / Robot 2** Button (oben rechts) kann zwischen den beiden Roboter-Datengruppen gewechselt werden. Tabelle und Monitor zeigen jeweils die Daten des ausgewählten Roboters.
