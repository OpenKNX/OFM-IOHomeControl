### 1W Broadcast-Typ

Bestimmt die typabhängige Broadcast-Adresse für Pairing und normale 1W-Befehle.

* **Automatisch nach Gerätetyp** übersetzt die ETS-Geräterolle in die passende io-homecontrol-Protokollklasse: Jalousie/Rollladen=2, Fenster=4, Markise=3, Garagentor=5, Thermostat=14, Licht=6, Tor=7, Schloss=9, horizontaler Sonnenschutz=16, Vorhangschiene=19, Lüftung=20 und Schalter=15. Generisch verwendet Typ 0.
* **Typ 0** sendet an alle Geräte. Die weiteren Auswahlwerte sind die direkten io-homecontrol-Protokollklassen und können für Sonderfälle explizit gewählt werden.

Der Broadcast-Typ wird sowohl beim Pairing als auch für normale 1W-Befehle verwendet und wählt die Zielklasse des Telegramms. Er muss zu einer vom Aktor akzeptierten Klasse passen; ein falscher Wert kann dazu führen, dass Pairing und spätere Befehle ignoriert werden. Eine in einem empfangenen 1W-Telegramm sichtbare Zielklasse ist jedoch kein sicherer Nachweis des realen Gerätetyps des gesteuerten Produkts.
