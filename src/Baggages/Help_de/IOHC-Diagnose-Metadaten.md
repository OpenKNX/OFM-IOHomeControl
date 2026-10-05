# Metadaten lesen

Metadaten sind Geräteinformationen bzw. Datenbankbytes, keine normalen Positionen oder Temperaturen. Hier wird ausschließlich gelesen. Nur für einen gepaarten 2W-Aktor und während kein anderer Verwaltungsauftrag läuft.

1. Provider und Objektkey wählen: Provider 00 erlaubt 0000, 0001, 0002, 0003, 030A, 8100, 8103, 4300, 4302; Provider 0B erlaubt C000. Alle Kennungen sind Hex.
2. Leseoffset und Lesespanne dezimal angeben. Beispiel: 0 und 64 für die ersten 64 Bytes.
3. „Metadatenobjekt lesen“ startet den Auftrag und trägt Boot / Token / Node ein.
4. „Objektstatus lesen“ zeigt den Fortschritt. Erst bei Stufe 4 und Identität gültig = 1 sind die Ergebnisbytes verfügbar.
5. „Objektbytes lesen“ zeigt höchstens 16 Bytes ab Rückleseoffset. Für weitere Bytes 16, 32 usw. wählen. Der Rückleseoffset zählt ab Beginn des empfangenen Bereichs.

Stufen im Status:

- 0: kein Auftrag; 1: Öffnungsanfrage; 2: bereit; 3: wartet auf Antwort.
- 4: abgeschlossen; 5: abgewiesen; 6: Gegenstelle hat beendet; 7: lokal abgebrochen.
- 8: Zeitbudget überschritten; 9: ungültiger Ablauf; 10: Übertragungsfehler; 11: Gerätezuordnung geändert.

„übertragen“ ist die tatsächlich gelesene Bytezahl. „Disposition“ ist der rohe Antwortcode, kein Temperatur- oder Fehlertext; beim Öffnen führen Werte größer als 1 zur Ablehnung. „Identität gültig“ prüft die Zuordnung zu Adresse und Schlüssel, nicht die Bedeutung der Daten.

„Objektlesen abbrechen“ beendet den passenden laufenden Auftrag. Nach einem Gateway-Neustart wird er nicht automatisch fortgesetzt. Das Gateway erlaubt bestimmte Objektadressen, garantiert aber weder ihre Existenz im Aktor noch eine bekannte Bedeutung jedes Bytes.
