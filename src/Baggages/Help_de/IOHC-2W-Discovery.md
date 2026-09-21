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

Ein VELUX-SSL-Solaraktor wurde auf realer Hardware mit `0x28`, Ziel `0x00003F`,
ACK **Ein**, Discovery-LOW_POWER **Aus**, Präambel **Normal (32)** und
Discovery-Bestätigung **Senden** erfolgreich gepairt. Meldet das korrelierte
`0x29` LOW_POWER, bleibt dieses Geräteprofil erhalten. Gleichzeitig begrenzt
die tatsächlich erfolgreiche Discovery-Präambel die gerichteten START-Frames
`0x2C`, `0x31` und das optionale `0x6F` auf höchstens 32 Symbole. `0x32` und
`0x3D` bleiben kurze Fortsetzungstelegramme.

Andere Hersteller können andere Profile benötigen. Deshalb bleiben Befehl,
Ziel, ACK, LOW_POWER und Präambel unabhängig einstellbar und die generischen
Defaults unverändert. Die Expertenoptionen nur für kontrollierte Tests oder
mit einem passenden Messprotokoll ändern.
