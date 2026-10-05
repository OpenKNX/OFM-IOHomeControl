# Leseoffset

Startposition im Metadatenobjekt, in Bytes ab 0. Die Eingabe ist dezimal, nicht Hex.

Beispiel: Leseoffset 32 und Lesespanne 16 fordert die Bytes 32 bis 47 des Objekts an. Die verfügbare Objektlänge bestimmt der Aktor; ein Bereich außerhalb seines Objekts kann abgewiesen oder verkürzt beantwortet werden. Offset plus Spanne darf höchstens 65535 ergeben.

Dieser Offset wird beim Funk-Leseauftrag verwendet. Der Rückleseoffset dagegen wählt später Bytes im bereits empfangenen Diagnosepuffer aus.
