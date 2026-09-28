### Gerätetyp

Dieser Parameter legt fest, welcher Gerätetyp dem Kanal zugeordnet ist.

**Deaktiviert** ist der Standardwert für jeden Kanal. Erst die Auswahl eines Gerätetyps aktiviert den Kanal und blendet seine Konfigurationsseite ein.

Die Auswahl beeinflusst die in ETS verfügbaren Funktionen sowie die interne Behandlung einzelner Befehle.
Daher sollte nach Möglichkeit der tatsächlich verwendete Gerätetyp gewählt werden.

**Automatisch (Discovery)** lässt das Gerät sein Funkprofil selbst auswerten, zeigt
in ETS aber keine Aktor-Befehlsobjekte an: ETS kann das im Gerät gespeicherte
Profil nicht selbst dynamisch auslesen. Nach normalem Pairing den passenden
Gerätetyp in ETS wählen. Der ETS-Schlüsselimport wählt ihn anhand von Profil
und Subprofil automatisch und zeigt die importierten Rohwerte an.

Die Auswahl **Cozy-Thermostat** gilt nur für Atlantic-Cozy-Privatbefehle.
Die Profile für **Heizung Stellwert** und **Heizung Ein/Aus** verwenden stattdessen
ihre normalen Profilparameter; sie zeigen keine Cozy-Befehlsobjekte.

Bei Jalousie/Rollladen sind Lamellenobjekte nur für bestätigte
Orientierungsprofile sinnvoll. Bei manuell eingerichteten Kanälen müssen sie
ausdrücklich aktiviert werden.

Im 1W-Modus bestimmt der Gerätetyp außerdem die automatische Wahl der io-homecontrol-Broadcast-Klasse. Die ETS-Auswahl wird dabei in die jeweilige Protokollklasse übersetzt; nur der generische Gerätetyp verwendet Typ 0 für alle Geräte.
