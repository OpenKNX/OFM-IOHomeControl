# Objektkey

Kennung des zu lesenden Metadatenobjekts, genau vier Hex-Ziffern ohne 0x. „Key“ meint hier eine Objektkennung, keinen AES-Funkschlüssel.

Erlaubte Kombinationen:

- Provider 00: 0000, 0001, 0002, 0003, 030A, 8100, 8103, 4300, 4302.
- Provider 0B: C000.

Diese Kennungen wählen Metadaten- bzw. Datenbankansichten im Gerät. Der genaue Inhalt und die Bedeutung einzelner Bytes sind nicht für jede Produktvariante bestätigt; daher zeigt die Diagnose sie als Rohdaten. Aus 8100 allein lässt sich beispielsweise kein Modellname oder Temperaturwert ableiten.

Der Standard 8100 mit Provider 00 ist eine erlaubte Leseadresse, keine Garantie für eine Antwort. Unbekannte Objekte kann der Aktor ablehnen. Diese Funktion schreibt keine Daten und ändert keine Stellwerte.
