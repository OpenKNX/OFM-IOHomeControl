### Protokoll-Modus

Dieser Parameter legt fest, ob der Kanal bidirektional (2W) oder unidirektional (1W) mit dem Gerät kommuniziert.

Im Modus 2W sendet das Gateway einen Befehl und erwartet eine Bestätigung bzw. Rückmeldung des Geräts.
Dieser Modus ist der vorgesehene Standard.

Im Modus 1W werden Befehle ohne Rückmeldung übertragen.
Er ist für Geräte ohne Rückkanal oder für Sonderfälle gedacht, in denen der bidirektionale Handshake nicht zuverlässig funktioniert.

Im 1W-Modus werden zusätzlich die Aktor-Node-ID, der Broadcast-Typ und das Controllerprofil konfiguriert. Der Broadcast-Typ bestimmt, welche Geräteklasse das Pairing und die späteren Befehle empfängt. Das Controllerprofil bestimmt die emulierte Fernbedienungsadresse, den Schlüssel, den Sequenzzähler und den Hersteller.

Die globale Systemidentität wird nur für 2W verwendet. 1W-Telegramme verwenden das wirksame Controllerprofil des Kanals, das Low-Power-Flag und eine typabhängige Broadcast-Adresse.

Sofern kein konkreter Grund für 1W vorliegt, sollte 2W verwendet werden.
