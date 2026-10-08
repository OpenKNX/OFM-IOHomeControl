# 2W Energieklasse

Mit **Automatisch** verwendet das Modul die aus Discovery- oder PRIVATE-Antworten gelernte und im Flash gespeicherte Energieklasse. Solange noch keine Klasse bekannt ist, wird das Gerät als immer aktiv behandelt.

**Immer aktiv** erzwingt normale 2W-START-Frames mit kurzem Start-Preamble. Diese Einstellung eignet sich insbesondere für netzversorgte VELUX-Aktoren.

**Energiesparend** setzt das LOW_POWER-Bit und verwendet das lange Aufweck-Preamble für schlafende Batterie- oder Solargeräte.

Diese Auswahl steuert Funk-Header und Aufweck-Präambel des Gateways. Sie ändert
keinen Energiesparmodus im Antrieb. Eine solche Geräteeinstellung benötigt einen
separat belegten Befehl; dafür gibt es derzeit keinen verifizierten Setter.
Hersteller oder allgemeiner Gerätetyp allein belegen nicht die Stromversorgung.
Ein netzversorgtes VELUX-Produkt und ein Solarprodukt können verschiedene Profile
benötigen. In der Konsole sind `power`, `preamble` und `wake` getrennte,
nur zur Laufzeit geltende Diagnose-Overrides; `wake off` löscht keine gelernte
Energieklasse und beweist nicht, dass das Gerät dauerhaft wach ist.
