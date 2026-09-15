### 2W Befehlsprofil

Legt das ACEI-Byte für normale bidirektionale Execute-Befehle dieses Kanals fest. Dadurch können unterschiedliche Hub-Profile ohne neue Firmware getestet werden.

* **Standard / Somfy (0x67)** ist der Standardwert und entspricht direkt beobachteten Somfy-Hub-Telegrammen.
* **Alternative / KIG300-Capture (0x63)** verwendet den alternativen Wert aus einem KIG300-Mitschnitt.

Die Einstellung gilt für Position, Auf/Ab, Stopp und Favorit. Spezielle Telegrammformate mit eigenem, protokollspezifischem ACEI – beispielsweise Lamellenbefehle und Atlantic-Cozy-Kommandos – werden nicht verändert.
