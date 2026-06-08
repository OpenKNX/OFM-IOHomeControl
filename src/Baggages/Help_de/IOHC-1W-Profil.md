### 1W Controllerprofil

Jeder 1W-Kanal besitzt standardmäßig eine eigene emulierte Fernbedienungsidentität. Das Profil enthält Controller-Adresse, 16-Byte-Schlüssel, Sequenzzähler und Hersteller.

Der gemeinsame Sequenzzähler wird nach jedem 1W-Befehl erhöht und persistent gespeichert. Dadurch werden nach einem Neustart keine alten Sequenzen wiederverwendet.

Mit **0 = eigenes Profil** steuert der Kanal eine unabhängige 1W-Gruppe. Soll ein weiterer Kanal dieselbe bereits angelernte Fernbedienung bzw. Gruppe verwenden, kann hier ein anderer als 1W konfigurierter Profilkanal eingetragen werden. Beide Kanäle verwenden dann dieselbe Identität, denselben Hersteller und denselben Sequenzzähler.

Ungültige oder zyklische Verweise fallen auf das eigene Profil zurück. Identische importierte Profile werden intern auf einen gemeinsamen Sequenzzähler zusammengeführt. Die Online-Aktion und die Servicekonsole ändern keine Profilidentität, solange ein damit verbundener Kanal gepairt ist. Nach einer geänderten ETS-Profilzuordnung oder einem Profilwechsel müssen die betroffenen Aktoren erneut angelernt werden.

Die ETS-Online-Aktion **Neues eigenes 1W-Controllerprofil erzeugen** erstellt eine neue zufällige Identität. Adresse und Schlüssel einer vorhandenen Somfy-Situo-Fernbedienung können über die Servicekonsole aus deren QR-Daten importiert werden. Der QR-Import ändert den lokalen Hersteller nicht und setzt die Sequenz so zurück, dass das erste Telegramm der neuen Identität Sequenz 1 verwendet.
