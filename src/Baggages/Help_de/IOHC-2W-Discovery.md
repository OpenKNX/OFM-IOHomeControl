# 2W Discovery (experimentell)

Diese Einstellungen trennen Befehl, Zieladresse, ACK-, LOW_POWER-Bit und
Präambellänge für die 2W-Kopplung. **Automatisch** verwendet das jeweilige
Befehlsprofil. Die normale kalte Suche bleibt aus Kompatibilitätsgründen bei
`0x28`, Ziel `0x00003B`, ACK aus, LOW_POWER aus und Präambel 1024.
Der authentifizierte SPE-Roll-Call (`0x2A`) nach einer Schlüsselextraktion
verwendet automatisch ebenfalls die Präambel 1024, damit schlafende Solar- und
Batteriegeräte den Scan empfangen können. Für einen Vergleich mit dem
aufgezeichneten 32-Symbol-Profil kann die Präambel explizit auf **Normal**
gestellt werden.

Für einen gezielten KLR300/VELUX-A/B-Test kann zunächst ausschließlich ACK auf
**Ein** gesetzt werden. Danach können LOW_POWER, Ziel, Befehl und Präambel
unabhängig verändert werden. Diese Optionen sind Diagnosefunktionen und sollen
nicht ohne Messprotokoll als allgemeines Herstellerprofil verwendet werden.
