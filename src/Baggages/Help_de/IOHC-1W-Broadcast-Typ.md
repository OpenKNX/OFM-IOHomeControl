### 1W Broadcast-Typ

Bestimmt die typabhängige Broadcast-Adresse. Normale 1W-Befehle verwenden
diese Klasse, wenn **1W Befehls-Ziel = Typ-Broadcast** (oder Automatisch)
gewählt ist. Beim Anlernen gilt zusätzlich **1W Anmelde-Ziel**.

* **Automatisch nach Gerätetyp** übersetzt die ETS-Geräterolle in die passende io-homecontrol-Protokollklasse: Jalousie/Rollladen=2, Fenster=4, Markise=3, Garagentor=5, Thermostat=14, Licht=6, Tor=7, Schloss=9, horizontaler Sonnenschutz=16, Vorhangschiene=19, Lüftung=20 und Schalter=15. Generisch verwendet Typ 0.
* **Typ 0** sendet an alle Geräte. Die weiteren Auswahlwerte sind die direkten io-homecontrol-Protokollklassen und können für Sonderfälle explizit gewählt werden.

Bei Somfy mit automatischem Anmelde-Ziel gehen REMOVE und ADD unabhängig
von der Geräteklasse an `00003F`. Für besondere Empfänger kann das Anmelde-Ziel
explizit auf Typ-Broadcast gesetzt werden. Diese Wahl ändert das Ziel normaler
1W-Befehle nicht. Eine sichtbare Zielklasse ist kein sicherer Nachweis des
realen Gerätetyps.
