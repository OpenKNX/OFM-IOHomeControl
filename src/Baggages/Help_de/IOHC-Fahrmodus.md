# Fahrmodus

Normal nutzt die Standardgeschwindigkeit des Antriebs. Silent (langsam) fordert eine leisere, langsamere Fahrt an; Fast (schnell) fordert die maximale Geschwindigkeit an. Dies gilt für Positions-, Auf/Ab- und Favoritbefehle kompatibler 2W-Rollläden, auch von Somfy und VELUX. Welche Geschwindigkeiten verfügbar sind, bestimmt der Antrieb.

Das optionale 1-Byte-Kommunikationsobjekt verwendet **0 = Normal, 1 = Silent, 2 = Fast** (DPT 5.010). Ein Schreiben wählt den Modus für die nächste Fahrt; es startet keine Bewegung. Andere Werte werden ignoriert. Lesen liefert den gewählten Modus, keine gemessene Motorgeschwindigkeit.

Nach einem Neustart oder einer Applikationsprogrammierung gilt wieder der Standard-Fahrmodus. Fahrzeiten für die Positionsschätzung passen sich nicht automatisch an; bei langsamer Fahrt sind empfangene Positionswerte genauer. Im 1W-Modus ist die Auswahl nicht verfügbar.
