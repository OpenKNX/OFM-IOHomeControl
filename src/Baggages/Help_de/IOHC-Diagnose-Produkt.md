# Produktdefinition für Diagnose

Den exakten Diagnose-Namen eintragen. „Bedeutungen anzeigen“ beschreibt die dokumentierten Werte; „diagnostisch dekodieren“ interpretiert passende Rohdaten. none wählt keine Definition. Die Auswahl sendet keine Stellbefehle.

## Produktabhängige Zusatzwerte

Diese Definitionen gelten nur für das nachgewiesene Produkt, nicht für alle Geräte einer ETS-Kategorie. Die Diagnoseauswahl dekodiert Rohdaten und erteilt keine Freigabe für Schreiben oder KNX-Rückmeldungen.

- Heatpump / heatpump: MP = Temperatursollwert; FP8 = gemessene Temperatur. FP15 beschreibt verfügbare Betriebsfunktionen, FP16 deren Zustände. Temperaturbereich der Umrechnung: -40 bis +80 °C.
- Heating interface / heating-interface: MP = Temperatursollwert mit bestätigten Gerätegrenzen. Die Sonderwerte 0xFC00 bis 0xFC05 bedeuten Aus, Frostschutz, Eco, Komfort minus 2, Komfort minus 1, Komfort; 0xFC07 = Boost, 0xFC3F = gesichert. Sie sind keine Temperaturen.
- Generic heater / generic-heater: FP12 = Komfortsollwert; FP13 = Absenksollwert, berechnet mit FP12 und bestätigten Grenzen. Dafür muss auch ein frischer FP12-Wert aus derselben Lesegruppe vorliegen.
- Atlantic heater / atlantic-heater: FP12 = Komfortsollwert (7 bis 28 °C). FP13 = numerischer Absenkwert (2 bis 9); dessen Einheit und Bedeutung als absolute Temperatur sind nicht bestätigt.
- Atlantic DHW V2 / atlantic-dhw-v2: MP = Warmwasser-Temperatursollwert, umgerechnet anhand bestätigter Gerätegrenzen.
- Atlantic DHW centikelvin / atlantic-dhw-ck: MP = Temperatur in 1/100 Kelvin; 29315 entspricht 20 °C. Diese Variante hat eine andere Kodierung als DHW V2.
- Heatpump modes: FP15 = unterstützte Funktionen; FP16 = gepackte Zustände für Gesamtbetrieb, Heizen, Kühlen, Abwesenheit, Pool und Warmwasser. Kein einzelner KNX-HVAC-Modus.
- Atlantic DHW modes / atlantic-dhw: FP15 = Fähigkeiten, FP16 = Abwesenheit / erneutes Aufheizen. Nicht dieselbe Bitbedeutung wie bei Heatpump modes.
- Pergola / pergola: MP = Lamellenorientierung; FP1 = Bewegungsgeschwindigkeit. Beide werden in Prozent dekodiert.
- Dual shutter / dual-shutter: FP1 = Schließgrad oben; FP2 = Schließgrad unten. MP ist der Hauptwert; getrennte Behangwerte stehen in FP1/FP2.
- Alarm / alarm: MP = aktivierte Alarmzonen, als Menge von bis zu drei Zonen. Nicht als Helligkeit oder Temperatur interpretieren.
- Sliding window lock / sliding-lock: FP9 = Verriegelungszustand. Bestätigte Endwerte: 0x0000 = entriegelt, 0xC800 = verriegelt; andere Werte bleiben unbestätigt.
- Atlantic ventilation / atlantic-ventilation: FP16 = Lüftungsmodus; 0xFC00 = Standard, 0xFC01 = Komfort, 0xFC02 = Eco.
- Siren / siren: FP9/FP10 = Ton und Optionen für Sequenz 1; FP11/FP12 = Sequenz 2; FP13/FP14 = Sequenz 3. Jeweils beide zusammen lesen; Optionen enthalten mehrere Angaben, keinen einzelnen Prozentwert.
- RGB: MP = Helligkeit; FP10/FP11 = zusammengehörige Farbkoordinaten u/v. Sie sind keine einzelnen Rot-/Grün-/Blauwerte.
- Tunable White: MP = Helligkeit; FP14 = Farbtemperatur in Kelvin, nicht in °C.

Andere FP oder unbekannte Sonderwerte bleiben Rohdaten. Grenzen, Generation und genaue Produktvariante lassen sich nicht allein aus der Profilnummer ableiten.
