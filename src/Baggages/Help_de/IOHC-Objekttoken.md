# Lesevorgang (Boot / Token / Node)

Die Anzeige bindet Status und gelesene Bytes an genau einen Auftrag:

- Boot: Kennung des Gateway-Starts; verhindert die Verwendung eines Ergebnisses aus einer früheren Sitzung.
- Token: fortlaufende Auftragskennung; ein neuer Leseauftrag erhält eine neue Kennung.
- Node: Funkadresse des gelesenen Aktors; verhindert die Verwechslung mit einem anderen Kanal.

Der Wert wird beim Start automatisch eingetragen. Nicht von Hand bearbeiten oder auf einen anderen Kanal kopieren. Nach einem Neustart oder neuen Auftrag passt der alte Wert nicht mehr; „Status lesen“ allein übernimmt keinen fremden Auftrag.
