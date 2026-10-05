# Lesespanne

Anzahl der anzufordernden Bytes, dezimal von 1 bis 1024. Zusammen mit dem Leseoffset bestimmt sie den Bereich im Gerät.

Beispiel: Offset 0 und Spanne 64 fordert die ersten 64 Bytes an. Der Aktor kann eine kleinere Länge bestätigen. „übertragen“ im Status zeigt die tatsächlich empfangene Bytezahl, nicht die angeforderte Spanne. Kleine Bereiche verringern Funkverkehr; ein Auftrag hat ein Zeitbudget von 30 Sekunden.
