<!-- 
cSpell:words knxprod Rollladen Lamellenposition Garagentor Vorhangschiene Lüftungsposition Fernbedienungs
cSpell:words Abfrageintervall Szenensteuerung Langzeitbetrieb Fensterkontakt Betriebsmodus Anwesenheit
cSpell:words Sollwert Rückmeldung Szenenaktion Szenenposition Gerätetyp Batterielevel Signalstärke
cSpell:words Sonnenschutz unidirektional bidirektional Pairing Modulstatus Beobachtete Szenenlamelle
cSpell:words Szenenmodus Szenentemperatur Szenenzustand
-->

# Applikationsbeschreibung io-homecontrol

Die io-homecontrol-Applikation dient der Einbindung von Velux-, Somfy- und weiteren io-homecontrol-Geräten in KNX, ohne dass ein separates Herstellergateway erforderlich ist. Die Funkkommunikation erfolgt über ein 868-MHz-Funkmodul des Typs SX1276 oder SX1262 in Verbindung mit einem ESP32.

Es können bis zu 16 unabhängige io-homecontrol-Kanäle konfiguriert werden. Die tatsächlich verfügbare Anzahl richtet sich nach der eingesetzten ETS-Applikation, in die das io-homecontrol-Modul eingebunden ist.
Eine Übersicht der verfügbaren Konfigurationsseiten sowie Verweise auf die jeweiligen Detailbeschreibungen enthält der Abschnitt [ETS Konfiguration](#ets-konfiguration).

## Inhalte

* [Einleitung](#einleitung)
* Grundlegende Konzepte
  * [Gerätetypen](#gerätetypen)
  * [Pairing (Geräte anlernen)](#pairing-geräte-anlernen)
  * [Protokoll-Modi (1W / 2W)](#protokoll-modi-1w--2w)
  * [Positionssteuerung](#positionssteuerung)
  * [Sicherheitsfunktionen](#sicherheitsfunktionen)
* [ETS Konfiguration](#ets-konfiguration) (Übersicht aller Konfigurationsseiten und Links zu Detailbeschreibung)
* [Serielle Konsole](#serielle-konsole)
* [Unterstützte Hardware](#unterstützte-hardware)
* [Kommunikationsobjekte](#kommunikationsobjekte)

### ETS Konfiguration

* **+ [Allgemein](#allgemein)**
  * [Verfügbare Kanäle](#verfügbare-kanäle)
  * [Pairing-Übersicht](#pairing-übersicht)
  * [Fernbedienungs-Beobachtung](#fernbedienungs-beobachtung)
  * [Discovery und Netzwerk-Scan](#discovery-und-netzwerk-scan)
* **+ io-homecontrol Kanäle**
  * [**+ Kanal n: ...** (n=1 bis 16)](#kanal-n)
    * [Kanalkonfiguration](#kanalkonfiguration)
    * [Gerätetyp](#gerätetyp)
    * [Status-Abfrageintervall](#status-abfrageintervall)
    * [Fahrzeit](#fahrzeit)
    * [Richtung invertieren](#richtung-invertieren)
    * [Verhalten nach Neustart](#verhalten-nach-neustart)
    * [Protokoll-Modus](#protokoll-modus)
    * [Szenen](#szenen)
    * [Pairing](#pairing)
    * [Thermostat (Cozy)](#thermostat-cozy)


## **Einleitung**

<!-- DOC HelpContext="Dokumentation" -->

<!-- DOCCONTENT
Eine vollständige Applikationsbeschreibung ist unter folgendem Link verfügbar: https://github.com/OpenKNX/OFM-IO-Homecontrol/blob/v1/doc/Applikationsbeschreibung-IoHomecontrol.md

Weitere Produktinformationen sind in unserem Wiki verfügbar: https://github.com/OpenKNX/OpenKNX/wiki
DOCCONTENT -->

Das io-homecontrol-Modul stellt folgende Funktionen bereit:

Gerätesteuerung

* Positionsbefehle 0-100% mit Rückmeldung
* Auf/Ab/Stopp nach KNX-Standard (DPT 1.008 / DPT 1.001)
* Lamellensteuerung für Jalousien und Rollläden (DPT 5.001)
* Favorit-Position (geräteseitig gespeicherte Vorzugsposition)
* Lüftungsposition für Fenster
* Langzeitbetrieb / Step-Stop für Jalousien

Unterstützte Gerätetypen

* Jalousie / Rollladen (Velux SSL, Somfy RS100 io, ...)
* Fenster (Velux Integra KMX/KSX, ...)
* Markise (Somfy Sunea io, ...)
* Garagentor (Somfy Dexxo io, ...)
* Thermostat (Atlantic Cozy io)
* Licht, Schalter, Schloss
* Sonnenschutz horizontal, Vorhangschiene, Lüftung, Tor

Statusüberwachung

* Automatische Status-Abfrage mit konfigurierbarem Intervall im 2W-Modus
* Positionsrückmeldung vom Gerät im 2W-Modus
* Batterielevel für solarbetriebene Geräte (z.B. Velux Solar) im 2W-Modus
* Signalstärke (RSSI) pro Kanal im 2W-Modus
* Fehlerstatus (Kommunikationsfehler, Duty-Cycle, Pairing verloren, Funkstörung)
* Bewegungsstatus (fährt / steht)

Szenensteuerung

* Bis zu 10 Szenen pro Kanal, konfigurierbar in der ETS
* Szenenaktionen: Position, Favorit oder Lüftung
* Für Thermostate: Temperatur und Betriebsmodus pro Szene
* Für Licht, Schalter und Schloss: Ein/Aus pro Szene
* Szenenaufruf über DPT 17.001, Szenensteuerung über DPT 18.001

Sicherheit und Kommunikation

* AES-128 verschlüsselte bidirektionale Kommunikation
* Challenge-Response-Authentifizierung
* Cyril-kompatible 1W-Controllerprofile mit persistentem Sequenzzähler und Low-Power-Flag
* 3-Kanal Frequency-Hopping (868.25 / 868.95 / 869.85 MHz)
* EU Duty-Cycle-Compliance mit Sub-Band-Tracking
* Automatische Wiederholversuche (bis zu 3 Versuche, Frequenzwechsel)
* Wind-/Regenalarm als Sicherheitseingang

Pairing und Diagnose

* Geräte-Pairing direkt aus der ETS über interaktive Bedienelemente
* Persistente ETS-Statusfelder pro Kanal für letztes Ergebnis, aktuelle Node-ID, Protokoll / Ziel und Diagnose
* Globale Pairing-Übersicht in der Seite "Allgemein" mit Sammel-Refresh und Zeitstempel der letzten Aktualisierung
* Pairing über die serielle Konsole
* Discovery-Scan zum Auffinden von Geräten
* Netzwerk-Scan zum passiven Beobachten des Funkverkehrs
* Fernbedienungs-Beobachtung zur Erfassung erkannter io-homecontrol-Fernbedienungen
* Diagnose über die serielle Konsole

Weitere Features

* Kanal sperren / entsperren (z.B. Kindersicherung, Wartung)
* Richtung invertieren pro Kanal
* Getrennte Öffnungs- und Schließzeiten pro Kanal für lineare Positionsschätzung während der Fahrt
* Verhalten nach Neustart konfigurierbar (Nichts tun / Status abfragen / Letzte Position anfahren)
* 2W-Pairing-Daten sowie vollständige 1W-Controllerprofile persistent im Flash gespeichert



## **Gerätetypen**

<!-- DOC Skip="1" -->
Das io-homecontrol-Modul unterstützt verschiedene Gerätetypen. Der Gerätetyp wird je Kanal in der ETS festgelegt und bestimmt, welche Kommunikationsobjekte und Parameter für den jeweiligen Kanal sichtbar sind.
<!-- DOCEND -->

| Gerätetyp | Beschreibung | Besondere KOs |
|-----------|-------------|---------------|
| Generisch | Beliebiges io-homecontrol-Gerät | Position, Auf/Ab, Stopp |
| Jalousie / Rollladen | Rollläden, Raffstores, Außenjalousien | Lamellenposition, Langzeitbetrieb |
| Fenster | Dachfenster, Fensteröffner | Lüftungsposition |
| Markise | Horizontale und vertikale Markisen | Position, Auf/Ab, Stopp |
| Garagentor | Garagentorantriebe | Position, Auf/Ab, Stopp |
| Thermostat | Atlantic Cozy io | Temperatur, Betriebsmodus, Anwesenheit, Fensterkontakt |
| Licht | io-homecontrol Dimmer und Schalter | Ein/Aus (statt Auf/Ab) |
| Tor | Schiebetore, Drehtorantriebe | Position, Auf/Ab, Stopp |
| Schloss | io-homecontrol Türschlösser | Status, Kanal sperren |
| Sonnenschutz horizontal | Horizontale Sonnenschutzsysteme | Lamellenposition |
| Vorhangschiene | Somfy Glydea io | Position, Auf/Ab, Stopp |
| Lüftung | io-homecontrol Lüftungseinheiten | Position, Auf/Ab, Stopp |
| Schalter | io-homecontrol Schalter | Ein/Aus (statt Auf/Ab) |

> Bei den Gerätetypen Licht und Schalter wird das KO "Auf/Ab" als "Ein/Aus" und das KO "Bewegungsstatus" als "Status" dargestellt. Die Funktion bleibt unverändert; es ändert sich ausschließlich die Bezeichnung. Beim Gerätetyp Schloss sind derzeit nur "Status" sowie das generische KO "Sperren" sichtbar.



## **Pairing (Geräte anlernen)**

Vor der Steuerung eines io-homecontrol-Geräts muss dieses mit dem Modul gepairt werden. Im 2W-Modus wird dabei der globale Systemschlüssel des Moduls mit dem Gerät ausgetauscht. Im 1W-Modus emuliert der Kanal eine Fernbedienung und überträgt deren Controller-Adresse, Schlüssel und Hersteller im Cyril-kompatiblen Lernablauf.

### **Pairing über die ETS**

Auf der Kanalseite jedes Kanals befindet sich ein Pairing-Bereich mit einem Moduswahlschalter, einer manuellen Statusabfrage und vier persistenten Diagnosefeldern.

Die ETS-Schaltflächen sind:

* **Pairing-Status auslesen**: Liest den aktuellen Pairing-Zustand vom Gerät und aktualisiert die ETS-Felder.
* **Pairing starten**: Startet den Pairing-Vorgang im aktuell gewählten Modus.
* **Pairing entfernen**: Entfernt die Zuordnung für den betreffenden Kanal.

Die angezeigten ETS-Felder sind:

* **Letztes Pairing-Ergebnis**: Zeigt den letzten von ETS ausgelösten oder gelesenen Status an.
* **Aktuell gepaarte Node-ID**: Zeigt die aktuell im Gerät hinterlegte Ziel-Node-ID bzw. `nicht angelernt`.
* **Protokoll / Ziel**: Zeigt im 2W-Modus `2W (bidirektional)` und im 1W-Modus Broadcast-Typ, wirksamen Profilkanal, Controller-Adresse, Hersteller und Sequenzzähler.
* **Letzte Pairing-Diagnose**: Zeigt Zusatzinformationen wie aktiven Controller-Zustand oder die Ursache einer Start-Ablehnung.

Zusätzlich gibt es auf der globalen Seite **Allgemein** eine **Pairing-Übersicht**, die diese vier Werte für alle sichtbaren Kanäle tabellarisch darstellt und per Sammel-Button aktualisiert.

* **Anlernen**: Startet den Pairing-Vorgang. Das Gerät muss sich dabei im Pairing-Modus befinden; die Aktivierung ist herstellerabhängig.
* **Entfernen**: Entfernt das Pairing für diesen Kanal. Das Gerät muss anschließend bei Bedarf erneut angelernt werden.

> Alle ETS-Pairing-Aktionen erfordern eine aktive ETS-Onlineverbindung zum Gerät. Nach dem Entfernen des Pairings ist das Gerät über diesen Kanal nicht mehr steuerbar, bis ein erneutes Pairing durchgeführt wurde.

### **Pairing über die serielle Konsole**

Alternativ kann das Pairing über die serielle Konsole durchgeführt werden.

* `iohc pair NN` — Startet das Pairing für Kanal NN (1-16)
* `iohc pair NN AABBCC` — Startet 1W-Pairing mit bekannter Node-ID (Hex)
* `iohc pair cancel` — Bricht einen laufenden Pairing-Vorgang ab
* `iohc unpair NN` — Entfernt das Pairing für Kanal NN
* `iohc 1wctrl status` — Zeigt die wirksamen 1W-Controllerprofile
* `iohc 1wctrl NN status` — Zeigt das wirksame Profil eines einzelnen 1W-Kanals
* `iohc 1wqr NN QRHEX` — Importiert Adresse und Schlüssel einer Situo-QR-Identität in das wirksame Profil
* `iohc 1wnew NN` — Erzeugt für einen ungepaarten Kanal ein neues eigenes 1W-Profil

### **Pairing-Status**

Es gibt kein eigenes Kommunikationsobjekt für den Pairing-Status.

Ein fehlendes oder verlorenes Pairing wird über den Fehlerstatus des Kanals gemeldet:

* `0 = OK`
* `3 = nicht gepairt / Pairing verloren`

Detaillierte Pairing-Informationen stehen in ETS über die Pairing-Diagnosefelder sowie über die serielle Konsole zur Verfügung.



## **Protokoll-Modi (1W / 2W)**

Das Modul unterstützt zwei Protokollmodi pro Kanal:

### **2W (bidirektional) - Standard**

Im bidirektionalen Modus sendet das Modul einen Befehl und erwartet eine Bestätigung beziehungsweise Rückmeldung des Geräts. Dieser Modus bietet:

* Zuverlässige Befehlsausführung mit Bestätigung
* Positionsrückmeldung direkt vom Gerät
* Batterielevel und Signalstärke
* AES-128 verschlüsselte Kommunikation

Dieser Modus ist für alle Geräte zu bevorzugen, die 2W unterstützen.

### **1W (unidirektional)**

Im unidirektionalen Modus sendet das Modul Befehle, ohne auf eine Antwort zu warten. Dieser Modus ist für Geräte vorgesehen, die ausschließlich Einweg-Kommunikation unterstützen oder deren 2W-Handshake nicht zuverlässig nutzbar ist.

Bei Auswahl von 1W erscheinen zusätzliche Felder:
* **1W Aktor-Node-ID**: Die dezimale Node-ID des Zielgeräts (0 = nicht gesetzt). Diese muss für Pairing und Kanalzuordnung bekannt sein, z.B. durch vorheriges Beobachten mit dem Netzwerk-Scan. Normale 1W-Befehle werden anschließend an die typabhängige Broadcast-Adresse gesendet.
* **1W Broadcast-Typ**: Bestimmt die typabhängige Broadcast-Adresse. Automatisch verwendet Typ 3 für Markisen und horizontalen Sonnenschutz sowie Typ 2 für andere Gerätetypen.
* **1W Controller-Hersteller**: Herstellerkennung des eigenen Controllerprofils. Bei einem geteilten Profil gilt der Hersteller des Profilkanals.
* **1W Profil teilen mit Kanal**: `0` verwendet ein eigenes Profil. Eine Kanalnummer lässt mehrere Kanäle dieselbe 1W-Fernbedienungsidentität und denselben Sequenzzähler verwenden.

Die globale Controller-Adresse und der globale Systemschlüssel gehören ausschließlich zur 2W-Identität. Jeder 1W-Kanal erhält stattdessen standardmäßig ein eigenes persistentes Controllerprofil aus zufälliger Adresse, 16-Byte-Schlüssel, Sequenzzähler und Hersteller. Dieses Verhalten entspricht dem Profilmodell der Cyril-Referenzimplementierung.

Der Sequenzzähler wird vor jedem neuen 1W-Telegramm erhöht und danach sofort persistent gespeichert. Das erste Telegramm einer neu erzeugten oder neu importierten Identität verwendet Sequenz `1`. Alle 1W-Telegramme setzen das Low-Power-Flag und verwenden den konfigurierten Broadcast-Typ für Pairing und normale Befehle.

Mehrere Kanäle können ein Profil explizit teilen. Sie verwenden dann dieselbe Adresse, denselben Schlüssel, denselben Hersteller und denselben Sequenzzähler. Ungültige oder zyklische Profilverweise fallen auf das eigene Profil zurück; identische importierte Profile werden intern auf einen gemeinsamen Sequenzzähler zusammengeführt.

Eine vorhandene Somfy-Situo-QR-Controlleridentität kann über `iohc 1wqr NN QRHEX` importiert werden. Erwartet werden mindestens 20 Hex-Bytes im Aufbau `Typ + Adresse[3] + Schlüssel[16]`; die drei Adressbytes werden für die Funkadresse umgekehrt. Der QR-Code liefert damit Controller-Adresse und Schlüssel. Der Hersteller bleibt ein lokaler Profilwert und wird über ETS oder Konsole festgelegt; neue automatisch erzeugte Profile verwenden ohne ETS-Override zunächst Somfy. Die Online-Aktion und die Servicekonsole ändern keine Profilidentität, solange ein gepaarter Kanal das Profil verwendet. Eine geänderte ETS-Profilzuordnung oder Profilidentität erfordert anschließend ein erneutes Pairing der betroffenen Aktoren.

> Im 1W-Modus stehen keine Positionsrückmeldung, kein Batterielevel und keine Signalstärke vom Gerät zur Verfügung. Die Positionsschätzung erfolgt ausschließlich anhand der konfigurierten Fahrzeiten.



## **Positionssteuerung**

### **Positionsbefehle**

Position wird als Prozentwert (0-100%) über DPT 5.001 gesteuert:
* **0%** = vollständig geöffnet / oben / eingefahren
* **100%** = vollständig geschlossen / unten / ausgefahren

Zusätzlich stehen Auf/Ab (DPT 1.008) und Stopp (DPT 1.001) als Befehle zur Verfügung.

### **Positionsrückmeldung**

Im 2W-Modus wird die aktuelle Position direkt vom Gerät abgefragt. Das Abfrageintervall ist konfigurierbar (30 Sekunden bis 30 Minuten oder deaktiviert).

Während einer Fahrt schätzt das Modul die Position linear basierend auf den konfigurierten Öffnungs- und Schließzeiten.

### **Lamellensteuerung**

Für Jalousien und horizontale Sonnenschutzsysteme steht zusätzlich eine Lamellenposition (DPT 5.001) zur Verfügung. Diese ist nur bei den Gerätetypen "Jalousie / Rollladen" und "Sonnenschutz horizontal" sichtbar.

### **Spezielle Positionen**

* **Favorit-Position** (KO Kn+10): Löst die im Gerät gespeicherte Vorzugsposition aus.
* **Lüftungsposition** (KO Kn+11): Fährt ein Fenster oder einen Lüftungskanal in die Lüftungsstellung.



## **Sicherheitsfunktionen**

### **Wind-/Regenalarm**

Über das KO Wind-/Regenalarm (DPT 1.005) kann ein Sicherheitsalarm ausgelöst werden. Bei aktivem Alarm werden Markisen eingefahren und Fenster geschlossen. Das io-homecontrol-Protokoll unterstützt Prioritätsstufen, sodass Sicherheitsbefehle Vorrang vor Benutzerbefehlen erhalten.

### **Kanal sperren**

Über das KO "Sperren" (DPT 1.001) kann ein Kanal vorübergehend deaktiviert werden. Solange die Sperre aktiv ist, werden Positions- und Fahrbefehle ignoriert. Dies ist insbesondere für Kindersicherung oder Wartungsarbeiten vorgesehen.

----

<!-- DOC HelpContext="IOHC-Allgemein" -->
## **Allgemein**

Hier werden Einstellungen vorgenommen, die für das gesamte io-homecontrol-Modul und sämtliche Kanäle gelten.

> Hinweis: Screenshots der ETS-Oberfläche werden in einer zukünftigen Version ergänzt.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Verfuegbare-Kanaele" -->
### **Verfügbare Kanäle**

Hier wird festgelegt, wie viele io-homecontrol-Kanäle in der Applikation verfügbar und editierbar sind. Die maximale Anzahl hängt von der Firmware des Geräts ab, das das io-homecontrol-Modul verwendet, und beträgt höchstens 16.

Eine geringere Anzahl sichtbarer Kanäle erhöht die Übersichtlichkeit und reduziert unnötige Darstellungen in ETS.

### **Pairing-Übersicht**

Die globale Seite **Allgemein** enthält eine read-only **Pairing-Übersicht** für alle aktuell sichtbaren io-homecontrol-Kanäle.

Dort werden pro Kanal dieselben vier Werte wie auf der jeweiligen Kanalseite angezeigt:

* **Ergebnis**
* **Node-ID**
* **Protokoll / Ziel**
* **Diagnose**

Über die Schaltfläche **Pairing-Übersicht aktualisieren** werden diese Werte für alle sichtbaren Kanäle nacheinander direkt vom Gerät gelesen. Das Feld **Zuletzt aktualisiert** zeigt, wann dieser Sammel-Refresh zuletzt erfolgreich abgeschlossen wurde.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Fernbedienungs-Beobachtung" -->
### **Fernbedienungs-Beobachtung**

Bei aktivierter Fernbedienungs-Beobachtung lauscht das Modul passiv auf io-homecontrol-Funkverkehr und meldet erkannte Fernbedienungsadressen über das globale KO "Beobachtete Fernbedienung" (DPT 12.001).

Die Funktion dient der Identifikation vorhandener io-homecontrol-Fernbedienungen sowie der Erfassung ihrer Adressen für die weitere Konfiguration.

### **Discovery und Netzwerk-Scan**

Discovery und Netzwerk-Scan werden über Kommunikationsobjekte gesteuert. Die zugehörigen KOs befinden sich in der Objekttabelle des globalen Kanals.

<!-- DOC Skip="1" -->
#### **Discovery Start/Stopp**

Wird eine 1 auf das KO "Discovery Start/Stopp" (KO 21) gesendet, beginnt das Modul einen aktiven Broadcast-Scan, um io-homecontrol-Geräte in der Umgebung zu erkennen. Eine 0 stoppt die laufende Discovery. Der Status wird über das KO "Discovery aktiv" (KO 22) zurückgemeldet.
<!-- DOCEND -->

<!-- DOC Skip="1" -->
#### **Netzwerk-Scan**

Wird eine 1 auf das KO "Netzwerk-Scan" (KO 23) gesendet, beginnt das Modul einen passiven Scan. Dabei wird auf allen drei Frequenzen nach io-homecontrol-Paketen gelauscht, ohne selbst zu senden. Der Scan dient Diagnosezwecken und der Ermittlung von Node-IDs. Der Status wird über das KO "Netzwerk-Scan aktiv" (KO 24) zurückgemeldet.
<!-- DOCEND -->

----

<!-- DOC HelpContext="IOHC-Kanal" -->
## **Kanal n**

Jeder io-homecontrol-Kanal repräsentiert ein einzelnes io-homecontrol-Gerät. Die Parametrierung erfolgt auf einer eigenen ETS-Seite.

### **Kanalkonfiguration**

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Beschreibung" -->
#### **Beschreibung**

Ein Freitextfeld mit bis zu 40 Zeichen zur Benennung des Kanals. Der Text wird in ETS als Kanalname sowie in den Kommunikationsobjekten angezeigt.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Kanal-aktiv" -->
#### **Kanal aktiv**

Schaltet den Kanal ein oder aus. Nur aktive Kanäle werden von der Firmware verarbeitet und blenden ihre weiteren Konfigurationsparameter ein.

* **Aus**: Kanal ist deaktiviert
* **Ein** (Standard): Kanal ist aktiv

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Geraetetyp" -->
### **Gerätetyp**

Erscheint nur, wenn "Kanal aktiv" auf "Ein" steht.

Bestimmt den Typ des angeschlossenen io-homecontrol-Geräts. Die Auswahl beeinflusst, welche Kommunikationsobjekte und Parameter für diesen Kanal sichtbar sind.

Mögliche Werte:
* Generisch (0)
* Jalousie / Rollladen — Standard
* Fenster (2)
* Markise (3)
* Garagentor (4)
* Thermostat
* Licht (6)
* Tor (7)
* Schloss
* Sonnenschutz horizontal (9)
* Vorhangschiene (10)
* Lüftung (11)
* Schalter (12)

> Bei Gerätetypen mit Positionssteuerung (0-4, 7, 9-11) werden zusätzlich die Parameter Öffnungszeit, Schließzeit und Richtung invertieren angezeigt. Beim Gerätetyp Thermostat erscheinen die Thermostat-spezifischen KOs.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Status-Abfrageintervall" -->
### **Status-Abfrageintervall**

Erscheint nur, wenn "Kanal aktiv" auf "Ein" steht.

Legt fest, in welchem Intervall das Modul den Gerätestatus abfragt. Dieses Intervall dient als Fallback; bei Geräten mit eigenständigen Statusmeldungen wird es lediglich ergänzend verwendet.

Mögliche Werte:
* Deaktiviert (0)
* 30 Sekunden
* 1 Minute
* 5 Minuten (Standard)
* 15 Minuten
* 30 Minuten

> Ein kürzeres Intervall erhöht die Duty-Cycle-Auslastung des 868-MHz-Bandes. Die Wahl des Intervalls sollte daher an die Anforderungen der jeweiligen Anlage angepasst werden.

> Im 1W-Modus gibt es keine Rückmeldung vom Gerät. Die Statusabfrage ist daher nur im 2W-Modus sinnvoll; 1W-Positionen werden anhand der Fahrzeiten geschätzt.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Fahrzeit" -->
### **Fahrzeit**

Erscheint nur für Gerätetypen mit Positionssteuerung (Generisch, Jalousie, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung).

#### **Öffnungszeit (Sekunden)**

Die Zeit in Sekunden, die das Gerät für eine vollständige Fahrt von geschlossen (100%) nach offen (0%) benötigt. Wertebereich: 0,1 bis 300 Sekunden, Standard: 30,0.

#### **Schließzeit (Sekunden)**

Die Zeit in Sekunden, die das Gerät für eine vollständige Fahrt von offen (0%) nach geschlossen (100%) benötigt. Wertebereich: 0,1 bis 300 Sekunden, Standard: 30,0.

> Die Fahrzeiten werden für die Positionsschätzung während der Fahrt verwendet. Im 2W-Modus wird die tatsächliche Position nach Abschluss der Fahrt vom Gerät abgefragt.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Richtung-invertieren" -->
### **Richtung invertieren**

Erscheint nur für Gerätetypen mit Positionssteuerung.

Vertauscht die Bedeutung von 0 % und 100 %. Dies ist sinnvoll, wenn Geräte in entgegengesetzter Orientierung montiert sind.

* **Aus** (Standard): 0% = offen, 100% = geschlossen
* **Ein**: 0% = geschlossen, 100% = offen

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Verhalten-nach-Neustart" -->
### **Verhalten nach Neustart**

Erscheint nur, wenn "Kanal aktiv" auf "Ein" steht.

Bestimmt, was das Modul nach einem Neustart (z.B. Stromausfall) für diesen Kanal tut.

Mögliche Werte:
* **Nichts tun** (0): Kein Befehl wird gesendet
* **Status abfragen** (1) — Standard: Das Modul fragt die aktuelle Position vom Gerät ab
* **Letzte Position anfahren** (2): Das Modul sendet nach dem Neustart den zuletzt gespeicherten Zustand erneut. Bei Gerätetypen mit Positionssteuerung wird die letzte Positionsrückmeldung verwendet, bei Licht-, Schalter- und Schloss-Kanälen der letzte Ein/Aus-Zustand.

> Im 1W-Modus kann **Status abfragen** keine Rückmeldung liefern. Dort stehen nur lokal gespeicherte bzw. geschätzte Zustände zur Verfügung.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Protokoll-Modus" -->
### **Protokoll-Modus**

Erscheint nur, wenn "Kanal aktiv" auf "Ein" steht.

Wählt den Kommunikationsmodus für diesen Kanal.

* **2W (bidirektional)** (Standard): Vollständige Zwei-Wege-Kommunikation mit Bestätigung und Statusrückmeldung
* **1W (unidirektional)**: Nur Senden, keine Antwort erwartet. Für ältere Geräte ohne 2W-Unterstützung.

Bei Auswahl von 1W erscheint zusätzlich:

#### **1W Aktor-Node-ID**

Die dezimale Node-ID des Zielgeräts. Sie muss bekannt sein und kann beispielsweise über den Netzwerk-Scan ermittelt werden. Wertebereich: 0 bis 16777215 (24 Bit). 0 bedeutet "nicht gesetzt".

Die Node-ID wird für Pairing und Kanalzuordnung benötigt. Normale 1W-Befehle werden an die aus dem Broadcast-Typ gebildete Gruppenadresse gesendet.

#### **1W Broadcast-Typ**

Bestimmt die Broadcast-Adresse für Pairing und Befehle. Die automatische Auswahl verwendet Typ 3 für Markisen und horizontalen Sonnenschutz sowie Typ 2 für die übrigen Gerätetypen. Für Diagnose und Sondergeräte können Typ 0, Typ 2 oder Typ 3 explizit gewählt werden.

#### **1W Controller-Hersteller**

Bestimmt die Herstellerkennung des eigenen Controllerprofils. Die Kennung wird beim 1W-Key-Transfer übertragen. Die Standardeinstellung übernimmt den persistent gespeicherten Wert; ein QR-Import ändert den Hersteller nicht. Neue automatisch erzeugte Profile verwenden ohne ETS-Override zunächst Somfy. Nach einer Änderung müssen die betroffenen Aktoren erneut angelernt werden, damit sie den geänderten Key-Transfer erhalten.

#### **1W Profil teilen mit Kanal**

Mit `0` besitzt der Kanal eine eigene virtuelle Fernbedienungsidentität. Durch Angabe eines anderen, als 1W konfigurierten Kanals teilen beide Kanäle Controller-Adresse, Schlüssel, Hersteller und Sequenzzähler. Dies ist für Aktoren gedacht, die als gemeinsame 1W-Gruppe mit derselben Fernbedienung angelernt wurden. Ungültige oder zyklische Verweise fallen auf das eigene Profil zurück.

Über die ETS-Online-Aktion **Neues eigenes 1W-Controllerprofil erzeugen** kann eine neue zufällige Identität erzeugt werden. Die Aktion ist nur für ein eigenes Profil möglich und wird abgelehnt, solange irgendein gepaarter Kanal dieses Profil verwendet. Das erste danach gesendete Telegramm verwendet Sequenz `1`.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Anzahl-Szenen" -->
### **Szenen**

Erscheint nur, wenn "Kanal aktiv" auf "Ein" steht.

#### **Anzahl Szenen**

Bestimmt, wie viele Szenen für diesen Kanal konfiguriert werden. Mögliche Werte: Keine, 1-10 Szenen.

Die Szenen-KOs erscheinen, sobald hier mindestens `1` Szene konfiguriert ist. Einen separaten Parameter `Szenensteuerung aktivieren` gibt es nicht.

Für jede aktive Szene erscheinen abhängig vom Gerätetyp unterschiedliche Parameter:

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenenaktion" -->
**Für positionsbasierte Geräte (Generisch, Jalousie, Fenster, Markise, etc.):**

* **Szene n Aktion**: Position / Favorit / Lüftung — Bestimmt, welcher Befehl bei Szenenaufruf gesendet wird.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenenposition" -->
* **Szene n Position** (nur bei Aktion = Position): Zielposition 0-100%.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenenlamelle" -->
* **Szene n Lamellenposition** (nur bei Jalousie / Sonnenschutz horizontal): Lamellenwinkel 0-100%.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenentemperatur" -->
**Für Thermostate:**

* **Szene n Temperatur**: Zieltemperatur 7-28 °C.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenenmodus" -->
* **Szene n Modus**: Auto / Manuell / Programm / Aus.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenenzustand" -->
**Für Licht, Schalter und Schloss:**

* **Szene n Zustand**: Ein / Aus.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Szenensteuerung" -->
Szenen werden über die Kommunikationsobjekte "Szene" (DPT 17.001, Szenennummer 1-10) und "Szenensteuerung" (DPT 18.001, Lernen/Abrufen) aufgerufen.

<!-- DOC -->
<!-- DOC HelpContext="IOHC-Pairing-Modus" -->
### **Pairing**

Auf jeder Kanalseite befindet sich ein Pairing-Bereich zur Verwaltung der Gerätekopplung.

#### **Pairing-Modus**

* **Anlernen**: Bereitet den Kanal für ein neues Pairing vor. Über den ETS-Button **Pairing starten** wird der Vorgang ausgelöst.
* **Entfernen**: Über den ETS-Button **Pairing entfernen** wird die Kopplung für diesen Kanal gelöscht.

Zusätzlich steht der ETS-Button **Pairing-Status auslesen** zur Verfügung. Damit können die Diagnosefelder manuell aktualisiert werden, ohne ein neues Pairing zu starten.

Bei 1W-Kanälen mit eigenem Profil steht außerdem **Neues eigenes 1W-Controllerprofil erzeugen** zur Verfügung. Eine neue Identität erfordert anschließend ein erneutes Pairing.

Unterhalb der Schaltflächen zeigt die ETS vier read-only Felder an:

* **Letztes Pairing-Ergebnis**
* **Aktuell gepaarte Node-ID**
* **Protokoll / Ziel**
* **Letzte Pairing-Diagnose**

> Das Pairing erfordert eine aktive ETS-Onlineverbindung zum Gerät. Das io-homecontrol-Gerät muss sich dabei im Pairing-Modus befinden.

### **Thermostat (Cozy)**

Erscheint nur bei Gerätetyp "Thermostat" (5).

Für Atlantic Cozy io Thermostate stehen zusätzliche Kommunikationsobjekte zur Verfügung:

* **Temperatur Sollwert** (KO Kn+20, DPT 9.001): Setzt die Zieltemperatur
* **Temperatur Rückmeldung** (KO Kn+21, DPT 9.001): Aktuelle Temperatur vom Gerät
* **Betriebsmodus** (KO Kn+22, DPT 20.102): HVAC-Betriebsmodus
* **Anwesenheit** (KO Kn+23, DPT 1.018): Anwesenheitsstatus
* **Fensterkontakt** (KO Kn+24, DPT 1.019): Fenster offen/geschlossen

----

## **Serielle Konsole**

Das Modul stellt eine serielle Konsole für Diagnose- und Pairing-Funktionen bereit. Alle Befehle beginnen mit dem Präfix `iohc`.

Die folgenden Tabellen enthalten die Befehle für reguläre Inbetriebnahme und Service. `iohc help` zeigt zusätzlich hardwareabhängige Radio-, Gateway- und Bench-Diagnosebefehle und ist die verbindliche Liste für die jeweils laufende Firmware.

### **Allgemeine Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc help` | Zeigt verfügbare Befehle |
| `iohc status` | Zeigt Pairing-Status aller Kanäle |
| `iohc status NN` | Zeigt Details für Kanal NN |

### **Pairing-Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc pair NN` | Startet Pairing für Kanal NN |
| `iohc pair NN AABBCC` | Startet 1W-Pairing mit bekannter Node-ID (Hex) |
| `iohc pair cancel` | Bricht laufenden Pairing-Vorgang ab |
| `iohc unpair NN` | Entfernt Pairing für Kanal NN |

### **Steuerungsbefehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc send NN PP` | Sendet Position PP% an Kanal NN |
| `iohc discover` | Startet Broadcast-Discovery-Scan |
| `iohc set1w NN` | Setzt Kanal auf 1W-Modus |
| `iohc set2w NN` | Setzt Kanal auf 2W-Modus |
| `iohc 1wctrl status` | Zeigt alle wirksamen 1W-Controllerprofile |
| `iohc 1wctrl NN status` | Zeigt das wirksame Profil eines einzelnen 1W-Kanals |
| `iohc 1wctrl NN ADDR HEX32 [MFG]` | Setzt Adresse, Schlüssel und optional Hersteller des wirksamen Profils |
| `iohc 1wqr NN QRHEX` | Importiert Adresse und Schlüssel einer Situo-QR-Controlleridentität in das wirksame Profil |
| `iohc 1wnew NN` | Erzeugt ein neues eigenes Profil, sofern es von keinem gepaarten Kanal verwendet wird |
| `iohc 1wmfg NN ID` | Setzt den Hersteller des wirksamen Profils |
| `iohc 1wtype TYPE` | Setzt den globalen 1W-Broadcast-Fallback; übliche Werte sind 0, 2 und 3 |
| `iohc pair1w-type NN ADDR TYPE` | Startet 1W-Pairing mit explizitem Broadcast-Typ |
| `iohc send1w-type NN open\|close\|stop\|vent\|force [TYPE]` | Sendet einen 1W-Befehl mit explizitem Broadcast-Typ |

`iohc set1w` und `iohc set2w` ändern den Modus nur zur Laufzeit für Diagnosezwecke. Nach einem Neustart gilt wieder die ETS-Konfiguration.

### **1W-Diagnosebefehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc send1wbtn NN up\|down\|stop\|my\|prog\|release\|stop2` | Sendet einen bekannten 1W-Fernbedienungstastencode |
| `iohc raw1w NN HEX` | Sendet einen rohen 1W-Tastencode |
| `iohc execraw NN HEX` | Sendet einen exakten 1W-Execute-Payload; Sequenz und HMAC werden ergänzt |
| `iohc pairdiag on\|off\|status` | Aktiviert oder zeigt ausführliche Pairing-Diagnose |

### **Thermostat-Befehle (Atlantic Cozy)**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc cozy temp NN TT` | Setzt Temperatur (in Zehntel, z.B. 215 = 21,5°C) |
| `iohc cozy mode NN MM` | Setzt Betriebsmodus |
| `iohc cozy presence NN 0/1` | Setzt Anwesenheit |
| `iohc cozy window NN 0/1` | Setzt Fensterkontakt |
| `iohc cozy poweron NN` | Sendet Einschalt-Befehl |
| `iohc cozy midnight NN` | Sendet Mitternachts-Zeitsync |

### **Fernbedienungs-Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc remote list` | Zeigt getrackte Fernbedienungen |
| `iohc remote add ADDR NAME` | Fügt Fernbedienung hinzu (Hex-Adresse) |
| `iohc remote del ADDR` | Entfernt Fernbedienung |
| `iohc remote link ADDR DEV` | Verknüpft Gerät mit Fernbedienung |
| `iohc remote unlink ADDR DEV` | Löst Verknüpfung |
| `iohc remote observed` | Zeigt kürzlich beobachtete Adressen |

### **Netzwerk-Scan-Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc scan start` | Startet passiven Netzwerk-Scan |
| `iohc scan stop` | Stoppt Netzwerk-Scan |
| `iohc scan dump` | Zeigt empfangene Pakete |
| `iohc scan stats` | Zeigt Statistiken pro Node |

----

## **Unterstützte Hardware**

Für den Betrieb des io-homecontrol-Moduls ist folgende Hardware erforderlich:

**Mikrocontroller:**
* ESP32 oder ESP32-S3

**Funkmodul (868 MHz):**
* SX1276-basiert: RFM95W, HopeRF RFM96W, Heltec LoRa32 v2, TTGO LoRa32 v2
* SX1262-basiert: Heltec LoRa32 v3, Waveshare, E22-868T

**KNX-Interface:**
* NCN5120 oder NCN5130 über OpenKNX-Hardware

**Verdrahtung:**
* SPI-Bus (SCK, MISO, MOSI, CS)
* Radio-Pins: RST und DIO0 sowie optional DIO4 für SX1276 bzw. DIO1 und BUSY für SX1262

----

## **Kommunikationsobjekte**

Die absoluten KO-Nummern hängen von der einbettenden OAM-Applikation ab. Die Tabellen verwenden daher relative Offsets:

* `G` = erster globaler KO-Offset des io-homecontrol-Moduls
* `Kn` = erster KO des Kanals `n`

Die tatsächlich sichtbaren Kommunikationsobjekte hängen vom Gerätetyp, von der Szenenanzahl und von der OAM-Applikation ab. Die Tabellen verwenden die Gerätetyp-Namen aus dem Abschnitt [Gerätetypen](#gerätetypen). KOs mit Klammern sind optional bzw. nur unter der angegebenen Bedingung sichtbar.

### Modul-Basis-KOs

| KO | Bereich / Sichtbarkeit | DPT | Bezeichnung | Erklärung |
|----|------------------------|-----|-------------|-----------|
| G+0 | Global | 1.011 | Modulstatus | `1 = Radio initialisiert`, `0 = nicht bereit` |
| G+1 | Global / Diagnose | 1.010 | Discovery Start/Stopp | `1 = Broadcast-Discovery starten`, `0 = Discovery stoppen` |
| G+2 | Global / Diagnose | 1.011 | Discovery aktiv | `1 = Discovery läuft`, `0 = inaktiv` |
| G+3 | Global / Diagnose | 1.010 | Netzwerk-Scan | `1 = passiven Scan starten`, `0 = stoppen` |
| G+4 | Global / Diagnose | 1.011 | Netzwerk-Scan aktiv | `1 = Scan läuft`, `0 = inaktiv` |
| G+5 | Global / Diagnose | 12.001 | Beobachtete Fernbedienung | Zuletzt beobachtete Fernbedienungs-Adresse |

### Kanal-KOs

| KO | Bereich / Sichtbarkeit | DPT | Bezeichnung | Erklärung |
|----|------------------------|-----|-------------|-----------|
| Kn+0 | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 5.001 | Position setzen | Zielposition 0-100% |
| Kn+1 | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 5.001 | Position Rückmeldung | Aktuelle Position 0-100% |
| Kn+2 | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 1.008 | Auf/Ab | `0 = Auf`, `1 = Ab` |
| Kn+3 | Licht, Schalter | 1.001 | Ein/Aus | Schalten von Licht bzw. Schalter |
| Kn+4 | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 1.017 | Stopp | Trigger zum Stoppen einer laufenden Fahrt |
| Kn+5 | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 1.011 | Bewegungsstatus | `1 = fährt / aktiv`, `0 = steht / inaktiv` |
| Kn+6 | Licht, Schalter | 1.001 | Status | Schaltzustand |
| Kn+7 | Schloss | 1.011 | Status | Statusrückmeldung des Schlosses |
| (Kn+8) | Jalousie / Rollladen, Sonnenschutz horizontal | 5.001 | Lamellenposition | Lamellenwinkel 0-100%, sofern Lamellensteuerung unterstützt wird |
| (Kn+9) | Jalousie / Rollladen, Sonnenschutz horizontal | 5.001 | Lamelle Rückmeldung | Aktuelle Lamellenposition 0-100% |
| (Kn+10) | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 1.017 | Favorit-Position | Trigger: gespeicherte Favorit-Position anfahren |
| (Kn+11) | Fenster, Lüftung | 1.017 | Lüftungsposition | Trigger für Lüftungsstellung |
| (Kn+12) | Diagnose / Status | 5.001 | Batterielevel | 0-100%, insbesondere für solar- oder batteriebetriebene Geräte |
| (Kn+13) | Diagnose / Status | 5.001 | Signalstärke | Normierte Signalstärke 0-100% |
| (Kn+14) | Alle aktiven Kanäle | 1.003 | Sperren | `1 = Kanal sperren`, `0 = entsperren` |
| (Kn+15) | Diagnose / Status | — | Fehlerstatus | Proprietärer 1-Byte-Diagnosecode: `0=OK`, `1=Kommunikationsfehler`, `2=Duty-Cycle`, `3=nicht gepairt / Pairing verloren`, `4=Funkstörung` |
| (Kn+16) | Szenenanzahl >= 1 | 17.001 | Szene | Szene aufrufen |
| (Kn+17) | Szenenanzahl >= 1 | 18.001 | Szenensteuerung | Szene lernen / abrufen |
| (Kn+18) | Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung | 1.005 | Wind-/Regenalarm | `1 = Alarm`, `0 = Entwarnung` |
| (Kn+19) | Jalousie / Rollladen | 1.008 | Langzeitbetrieb | `0 = Auf`, `1 = Ab` |
| Kn+20 | Thermostat | 9.001 | Temperatur Sollwert | Zieltemperatur setzen |
| Kn+21 | Thermostat | 9.001 | Temperatur Rückmeldung | Aktuelle Temperatur |
| Kn+22 | Thermostat | 20.102 | Betriebsmodus | HVAC-Modus setzen |
| Kn+23 | Thermostat | 1.018 | Anwesenheit | `1 = anwesend`, `0 = nicht anwesend` |
| Kn+24 | Thermostat | 1.019 | Fensterkontakt | `1 = Fenster offen`, `0 = Fenster geschlossen` |

Hinweise:

* `Auf/Ab` und `Ein/Aus` sind getrennte Kommunikationsobjekte mit eigenen DPTs. Dadurch erzeugt ETS bei direkter GA-Erstellung den passenden DPT für den jeweiligen Gerätetyp.
* `Bewegungsstatus`, `Status` für Licht/Schalter und `Status` für Schloss sind ebenfalls getrennte Kommunikationsobjekte.
* Beim Gerätetyp Schloss sind derzeit `Status` sowie das KO `Sperren` sichtbar.
* Thermostat-KOs müssen nicht disjunkt zu den KOs anderer Gerätetypen sein; die Sichtbarkeit wird über den Gerätetyp gesteuert.
* Für den regulären Betrieb wird ein fehlendes oder verlorenes Pairing über `Fehlerstatus = 3` gemeldet. Die ETS-Diagnosefelder und die serielle Konsole liefern bei Bedarf detailliertere Pairing-Informationen.
* `Gerätename` und `Gerätetyp-Code` werden nicht als reguläre Kommunikationsobjekte bereitgestellt. Diese Informationen sind nach Pairing/Konfiguration in der Regel konstant und gehören daher in ETS-Diagnosefelder, die serielle Konsole oder ein allgemeines Diagnose-/Servicekonzept.
