### 1W Broadcast-Typ

Bestimmt die typabhängige Broadcast-Adresse für Pairing und normale 1W-Befehle.

* **Automatisch nach Gerätetyp** übersetzt die ETS-Geräterolle in die passende io-homecontrol-Protokollklasse: Jalousie/Rollladen=2, Fenster=4, Markise=3, Garagentor=5, Thermostat=14, Licht=6, Tor=7, Schloss=9, horizontaler Sonnenschutz=16, Vorhangschiene=19, Lüftung=20 und Schalter=15. Generisch verwendet Typ 0.
* **Typ 0** sendet an alle Geräte. Die weiteren Auswahlwerte sind die direkten io-homecontrol-Protokollklassen und können für Sonderfälle explizit gewählt werden.

Der Broadcast-Typ wird sowohl beim Pairing als auch für normale 1W-Befehle verwendet und muss zum Aktor passen. Bei einem falschen Typ reagiert der Aktor weder auf das Pairing noch auf spätere Befehle.
