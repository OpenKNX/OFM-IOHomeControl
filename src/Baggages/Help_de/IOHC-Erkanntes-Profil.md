# Erkanntes Profil

Die Anzeige enthält die vom Aktor gemeldete Funktionsklasse und deren Variante (Subprofil). Sie identifiziert kein genaues Modell. Bei 1W liegt keine bestätigte Aktorerkennung vor.

- Profil: Hauptklasse, z. B. 2 = Rollladen, 4 = Fenster, 17 = Außenjalousie.
- Subprofil: Variante dieser Klasse, z. B. Rollladen 0 = normal, 1 = verstellbare Lamellen, 2 = Ausstellfunktion.
- Gepackte Kennung: Profil mal 64 plus Subprofil. Profil 2 / Subprofil 0 ergibt 128, in Hex 0x0080. Profil 17 / Subprofil 0 ergibt 1088, in Hex 0x0440.

Die Varianten unterscheiden die FP-Belegung: Innenjalousie FP1 = Lamellenwinkel, FP3 = Fahrgeschwindigkeit; Rollladen FP1 = Fahrgeschwindigkeit; Außenjalousie FP1 = Fahrgeschwindigkeit, FP3 = Lamellenwinkel. Die vollständigen Varianten stehen in der Hilfe zu Gerätetyp und MP/FP-Index.

Ein manueller Profil-Override ändert die Befehlszuordnung, nicht diese gemeldete Identität. Ein unbekanntes Subprofil ist nicht automatisch gleichbedeutend mit Subprofil 0.
