# Gerätetyp

Deaktiviert blendet die Kanalseite und ihre Kommunikationsobjekte aus. Eine andere Auswahl aktiviert den Kanal und bestimmt seine KNX-Funktionen.

Automatisch (Discovery): Nach 2W-Pairing „Status / Erkennung lesen“. Bekannte Funktionen werden übernommen, sofern die Erkennungsautomatik dies erlaubt. Danach die Applikation programmieren. Bei unbekannten Profilen und 1W ist eine manuelle Auswahl nötig. Die Kategorie bezeichnet kein genaues Produktmodell.

Jeder Gerätetyp ist einzeln auswählbar. Die Auswahl bestimmt das passende Profil und stellen Lamellenfunktion, Binärmodus und Dimmen ein. Diese manuelle Wahl bleibt bei der Erkennung erhalten. „Automatik wiederherstellen“ erlaubt wieder die automatische Zuordnung. Lamellenobjekte erscheinen automatisch bei passenden Profilen.

Einträge mit „(Diagnose)“ unterscheiden bekannte Gerätefamilien, deren Steuerung noch nicht vollständig unterstützt wird. Sie bieten Erkennung und Diagnose, aber keine normalen Aktor-Steuerobjekte. Cozy-Thermostat verwendet eigene Temperatur-/Modusbefehle und ist von der Heizungs-Temperaturschnittstelle getrennt.

RGB / Farbtemperatur erscheint nur bei den passenden Lichttypen. Temperatur / Modus erscheint bei Wärmepumpe, Warmwasserbereiter, elektrischen Heizkörpern und Heizungs-Temperaturschnittstelle. Die bisherige Produktqualifikation bleibt erforderlich.

## MP und FP verstehen

MP ist der Hauptwert, etwa die Rollladenposition. FP1 bis FP16 sind Zusatzwerte, etwa Geschwindigkeit oder Lamellenwinkel. Beim Index bedeutet 0 = MP, 1 = FP1, 2 = FP2 usw. Die Nummer allein sagt nichts über die Bedeutung aus: FP1 kann je nach Profil Geschwindigkeit, Lamellenwinkel oder einen Lichtübergang beschreiben.

Die folgenden Kennungen sind das gepackte Profil einschließlich Subprofil, in Hex. Nicht aufgeführte FP sind für diese Profile im Gateway nicht zugeordnet. Ein bekanntes Profil beschreibt die Funktionsklasse, keine garantierte Unterstützung jedes Befehls.

## Jalousien und Rollläden

- Innenjalousie (0x0040): MP = Behangposition; FP1 = Lamellenwinkel; FP2 = Geschwindigkeit der Lamellendrehung; FP3 = Fahrgeschwindigkeit.
- Rollladen (0x0080): MP = Rollladenposition; FP1 = Fahrgeschwindigkeit.
- Rollladen mit verstellbaren Lamellen (0x0081): MP = Rollladenposition; FP1 = Fahrgeschwindigkeit; FP2 = Drehgeschwindigkeit; FP3 = Lamellenwinkel.
- Rollladen mit Ausstellfunktion (0x0082): MP = Position; FP1 = Fahrgeschwindigkeit. FP9 = Ausstellwert, nur als Rohwert lesbar; Winkelumrechnung und Schreiben sind nicht bestätigt.
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
- Türschloss (0x0240), Fensterschloss (0x0241): MP = Verriegelungszustand. Beim Fensterschloss ist zusätzlich FP1 = Sicherheitsmodus, nur lesbar (0 = daylocked, 1 = homesecure, 2 = secured).
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
