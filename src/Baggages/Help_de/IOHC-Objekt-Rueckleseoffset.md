# Rückleseoffset

Startposition im bereits empfangenen Diagnosepuffer, dezimal ab 0. „Objektbytes lesen“ zeigt ab dort höchstens 16 Bytes und sendet keinen neuen Funk-Leseauftrag.

Bei einem Auftrag mit Leseoffset 32 entspricht Rückleseoffset 0 dem Objektbyte 32. Rückleseoffset 16 entspricht Objektbyte 48. Für größere Ergebnisse nacheinander 0, 16, 32 usw. wählen, soweit tatsächlich Bytes übertragen wurden.

Bytes lassen sich erst nach einem abgeschlossenen, weiterhin zum Gerät passenden Auftrag auslesen. Nach Neustart, neuem Auftrag oder geänderter Gerätezuordnung den passenden Lesevorgang neu starten.
