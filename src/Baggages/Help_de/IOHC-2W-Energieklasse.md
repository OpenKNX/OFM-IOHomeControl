# 2W Energieklasse

Mit **Automatisch** verwendet das Modul die aus Discovery- oder PRIVATE-Antworten gelernte und im Flash gespeicherte Energieklasse. Solange noch keine Klasse bekannt ist, wird das Gerät als immer aktiv behandelt.

**Immer aktiv** erzwingt normale 2W-START-Frames mit kurzem Start-Preamble. Diese Einstellung eignet sich insbesondere für netzversorgte VELUX-Aktoren.

**Energiesparend** setzt das LOW_POWER-Bit und verwendet das lange Aufweck-Preamble für schlafende Batterie- oder Solargeräte.
