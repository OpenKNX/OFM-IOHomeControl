### 1W Controller-Hersteller

Legt den Hersteller fest, den die emulierte 1W-Fernbedienung beim Anlernen im Key-Transfer meldet.

Die Einstellung gehört zum wirksamen 1W-Controllerprofil des Kanals. Ein Profil enthält Controller-Adresse, Schlüssel, Sequenzzähler und Hersteller.

**Aus gespeicherter Controller-Identität** übernimmt den Hersteller aus dem persistenten Profil. Ein QR-Import übernimmt ausschließlich Controller-Adresse und Schlüssel; der Hersteller bleibt ein lokaler Profilwert.

Verwendet der Kanal das Profil eines anderen Kanals, wird auch dessen Hersteller verwendet. Die Herstellerwahl muss dann am Profilkanal vorgenommen werden.

Neue automatisch erzeugte Profile verwenden Somfy als Ausgangswert, sofern in ETS kein anderer Hersteller gewählt wurde. Eine Änderung wirkt beim nächsten Key-Transfer und erfordert deshalb ein erneutes Anlernen der betroffenen Aktoren.
