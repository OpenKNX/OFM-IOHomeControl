# MP / FP auswählen

0 wählt den Hauptwert (MP), 1 bis 16 den jeweiligen Zusatzwert (FP). Zuerst das erkannte Profil bzw. die ausdrücklich gewählte Produktdefinition prüfen.

## MP und FP verstehen

MP ist der Hauptwert, etwa die Rollladenposition. FP1 bis FP16 sind Zusatzwerte, etwa Geschwindigkeit oder Lamellenwinkel. Beim Index bedeutet 0 = MP, 1 = FP1, 2 = FP2 usw. Die Nummer allein sagt nichts über die Bedeutung aus: FP1 kann je nach Profil Geschwindigkeit, Lamellenwinkel oder einen Lichtübergang beschreiben.

Die folgenden Kennungen sind das gepackte Profil einschließlich Subprofil, in Hex. Nicht aufgeführte FP sind für diese Profile im Gateway nicht zugeordnet. Ein bekanntes Profil beschreibt die Funktionsklasse, keine garantierte Unterstützung jedes Befehls.

## Jalousien und Rollläden

- Innenjalousie (0x0040): MP = Behangposition; FP1 = Lamellenwinkel; FP2 = Geschwindigkeit der Lamellendrehung; FP3 = Fahrgeschwindigkeit.
- Rollladen (0x0080): MP = Rollladenposition; FP1 = Fahrgeschwindigkeit.
- Rollladen mit verstellbaren Lamellen (0x0081): MP = Rollladenposition; FP1 = Fahrgeschwindigkeit; FP2 = Drehgeschwindigkeit; FP3 = Lamellenwinkel.
- Rollladen mit Ausstellfunktion (0x0082): MP = Position; FP1 = Fahrgeschwindigkeit. Ein eigener FP für die Ausstellfunktion ist nicht zugeordnet.
- Doppelrollladen (0x0340): MP = Hauptposition; FP1 = oberer Behang; FP2 = unterer Behang; FP3 = Fahrgeschwindigkeit.
- Außenjalousie (0x0440): MP = Behangposition; FP1 = Fahrgeschwindigkeit; FP2 = Drehgeschwindigkeit; FP3 = Lamellenwinkel.
- Lamellenbehang (0x0480): MP = Behangposition; FP1 = Fahrgeschwindigkeit; FP2 = Drehgeschwindigkeit der Aufhängung; FP3 = deren Orientierung.
- Klappladen (0x0600), auch mit unabhängigem Flügel (0x0601): MP = Schließgrad; FP1 = Bewegungsgeschwindigkeit.

## Fenster, Markisen und weitere Antriebe

- Fenster (0x0100), auch mit Regensensor (0x0101): MP = Öffnungsposition; FP1 = Fahrgeschwindigkeit. Der Regensensor ist kein zusätzlicher FP in dieser Zuordnung.
- Vertikale Außenmarkise (0x00C0), Innenrollo (0x0280), horizontale Markise (0x0400): MP = Position; FP1 = Fahrgeschwindigkeit.
- Vorhangschiene (0x04C0): MP = Vorhangposition; FP1 = Fahrgeschwindigkeit.
- Garagentor (0x0140), Tor (0x01C0): MP = Position; FP1 = Bewegungsgeschwindigkeit.
- Garagentor nur Ein/Aus (0x017A), Tor nur Ein/Aus (0x01FA): MP = Endzustand; keine zugeordneten FP. Zwischenstellungen sind für den normalen Stellwert gesperrt.

## Licht, Schloss, Lüftung und Heizung

- Licht (0x0180): MP = Helligkeit; FP1 = Helligkeitsänderung, also Übergang beim Dimmen.
- Licht nur Ein/Aus (0x01BA): MP = Ein/Aus-Endzustand; keine zugeordneten FP.
- Türschloss (0x0240), Fensterschloss (0x0241): MP = Verriegelungszustand; keine zugeordneten FP.
- Schalter (0x03C0): MP = Schaltzustand; keine zugeordneten FP.
- Lüftungspunkt (0x0500), Lufteinlass (0x0501), Luftüberleitung (0x0502), Luftauslass (0x0503): MP = Luftbedarf; keine zugeordneten FP. Das ist keine gemessene Luftmenge.
- Außenheizung (0x0540): MP = Heizleistungsbedarf; FP1 = Änderung der Heizleistung.
- Außenheizung nur Ein/Aus (0x057A): MP = Ein/Aus-Endzustand; keine zugeordneten FP.
- Cozy-Thermostat: separate Atlantic-Cozy-Befehle für Temperatur und Betriebsart. Nicht aus einem der obigen MP/FP-Profile ableiten.
- Automatisch / Generisch: erst erkanntes Profil prüfen; es gibt keine allgemeingültige MP/FP-Zuordnung.

## Prozentwerte und Sonderwerte

Normale Positionsrohwerte reichen von 0x0000 bis 0xC800; 512 Rohwertschritte entsprechen 1 %. Für Fenster, Licht, Schalter, Lüftung und Heizbedarf berücksichtigt das Gateway die profilspezifische Richtung. Einen Rohwert daher nicht ungeprüft als KNX-Prozentwert verwenden.

0xD100 steht für Zielwert, 0xD200 für aktuellen Wert, 0xD300 für Standardwert und 0xD400 für unverändert / ignorieren. Das sind Steuerkennungen, keine Messwerte. Auch 0xD800 ist kein Prozentwert: bei geeigneten Profilen steht es für eine gespeicherte Position.

Weitere bekannte MP-Sonderpositionen: Fenster 0xD803 = gesicherte Lüftung; Tor 0xD807 = Fußgängeröffnung; Garagentor 0xD809 = Teilöffnung; Außenjalousie und Klappladen 0xD80A = gesicherte Position. Das erklärt die Kennung, schaltet aber keinen zusätzlichen KNX-Befehl frei.

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
