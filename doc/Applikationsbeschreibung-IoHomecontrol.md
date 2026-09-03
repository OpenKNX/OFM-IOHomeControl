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
* [Konsole und Diagnose-Objekt](#konsole-und-diagnose-objekt)
* [Unterstützte Hardware](#unterstützte-hardware)
* [Kommunikationsobjekte](#kommunikationsobjekte)

### ETS Konfiguration

* **+ [Allgemein](#allgemein)**
  * [Pairing-Übersicht](#pairing-übersicht)
  * [Fernbedienungs-Beobachtung](#fernbedienungs-beobachtung)
  * [Discovery und Netzwerk-Scan](#discovery-und-netzwerk-scan)
* **+ [Kanalauswahl](#kanalauswahl)**
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

* Automatische Status-Abfrage mit konfigurierbarem Intervall
* Positionsrückmeldung vom Gerät
* Batterielevel für solarbetriebene Geräte (z.B. Velux Solar)
* Signalstärke (RSSI) pro Kanal
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
* 3-Kanal Frequency-Hopping (868.25 / 868.95 / 869.85 MHz)
* EU Duty-Cycle-Compliance mit Sub-Band-Tracking
* Automatische Wiederholversuche (bis zu 3 Versuche, Frequenzwechsel)
* Wind-/Regenalarm als Sicherheitseingang

Pairing und Diagnose

* Geräte-Pairing direkt aus der ETS über interaktive Bedienelemente
* Persistente ETS-Statusfelder pro Kanal für letztes Ergebnis, aktuelle Node-ID, Protokoll / Ziel und Diagnose
* Globale Pairing-Übersicht in der Seite "Allgemein" mit Sammel-Refresh und Zeitstempel der letzten Aktualisierung
* Pairing über das zentrale Diagnose-Objekt und die serielle Konsole
* Discovery-Scan zum Auffinden von Geräten
* Netzwerk-Scan zum passiven Beobachten des Funkverkehrs
* Fernbedienungs-Beobachtung zur Erfassung erkannter io-homecontrol-Fernbedienungen
* Diagnose über das zentrale Diagnose-Objekt und die serielle Konsole

Weitere Features

* Kanal sperren / entsperren (z.B. Kindersicherung, Wartung)
* Richtung invertieren pro Kanal
* Getrennte Öffnungs- und Schließzeiten pro Kanal für lineare Positionsschätzung während der Fahrt
* Verhalten nach Neustart konfigurierbar (Nichts tun / Status abfragen / Letzte Position anfahren)
* Pairing-Daten und Schlüssel persistent im Flash gespeichert



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
| Licht | io-homecontrol Lichtaktoren, Schalter und Dimmer | Ein/Aus, optional zusätzlich Helligkeit 0-100 % |
| Tor | Schiebetore, Drehtorantriebe | Position, Auf/Ab, Stopp |
| Schloss | io-homecontrol Türschlösser | Status, Kanal sperren |
| Sonnenschutz horizontal | Horizontale Sonnenschutzsysteme | Lamellenposition |
| Vorhangschiene | Somfy Glydea io | Position, Auf/Ab, Stopp |
| Lüftung | io-homecontrol Lüftungseinheiten | Position, Auf/Ab, Stopp |
| Schalter | io-homecontrol Schalter | Ein/Aus (statt Auf/Ab) |

> Bei den Gerätetypen Licht und Schalter ersetzen eigene Schalt-KOs die Antriebs-KOs: Statt "Auf/Ab" (DPT 1.008) erscheint "Ein/Aus" (DPT 1.001) und statt "Bewegt" (DPT 1.011) das KO "Status" (DPT 1.001). Für den Gerätetyp Licht kann zusätzlich die Eigenschaft **Dimmbar** aktiviert werden; dann bleiben die Schalt-KOs erhalten und die Positions-KOs werden zusätzlich als Helligkeitswert 0-100 % genutzt. Beim Gerätetyp Schloss sind das KO "Status" (DPT 1.011) sowie das generische KO "Sperren" sichtbar.



## **Pairing (Geräte anlernen)**

Vor der Steuerung eines io-homecontrol-Geräts muss dieses mit dem Modul gepairt werden. Im Rahmen des Pairings wird ein gemeinsamer Verschlüsselungsschlüssel zwischen Modul und Gerät ausgetauscht.

### **Pairing über die ETS**

Auf der Kanalseite jedes Kanals befindet sich ein Pairing-Bereich mit einem Moduswahlschalter, einer manuellen Statusabfrage und vier persistenten Diagnosefeldern.

Die ETS-Schaltflächen sind:

* **Pairing-Status auslesen**: Liest den aktuellen Pairing-Zustand vom Gerät und aktualisiert die ETS-Felder.
* **Pairing starten**: Startet den Pairing-Vorgang im aktuell gewählten Modus.
* **Pairing entfernen**: Entfernt die Zuordnung für den betreffenden Kanal.

Im 1W-Modus stehen zusätzlich folgende ETS-Aktionen zur Verfügung:

* **Neues eigenes 1W-Controllerprofil erzeugen**: Legt für den Kanal ein neues lokales 1W-Profil an, sofern kein anderer gepaarter Kanal dieses Profil verwendet.
* **Original-1W-Fernbedienung klonen**: Aktiviert den 1W-Klon-Empfang mit Standard-Timeout. Anschließend muss an der Original-Fernbedienung der herstellerseitige Kopiervorgang ausgelöst werden.

Die angezeigten ETS-Felder sind:

* **Letztes Pairing-Ergebnis**: Zeigt den letzten von ETS ausgelösten oder gelesenen Status an.
* **Aktuell gepaarte Node-ID**: Zeigt die aktuell im Gerät hinterlegte Ziel-Node-ID bzw. `nicht angelernt`.
* **Protokoll / Ziel**: Zeigt im 2W-Modus `2W (bidirektional)` und im 1W-Modus die Ziel- bzw. die tatsächlich gepaarte Node-ID.
* **Letzte Pairing-Diagnose**: Zeigt Zusatzinformationen wie aktiven Controller-Zustand oder die Ursache einer Start-Ablehnung.

Zusätzlich gibt es auf der globalen Seite **Allgemein** eine **Pairing-Übersicht**, die diese vier Werte für alle aktivierten Kanäle tabellarisch darstellt und per Sammel-Button aktualisiert. Suspendierte Kanäle bleiben sichtbar.

* **Anlernen**: Startet den Pairing-Vorgang. Das Gerät muss sich dabei im Pairing-Modus befinden; die Aktivierung ist herstellerabhängig.
* **Entfernen**: Entfernt das Pairing für diesen Kanal. Das Gerät muss anschließend bei Bedarf erneut angelernt werden.

> Alle ETS-Pairing-Aktionen erfordern eine aktive ETS-Onlineverbindung zum Gerät. Nach dem Entfernen des Pairings ist das Gerät über diesen Kanal nicht mehr steuerbar, bis ein erneutes Pairing durchgeführt wurde.

### **Pairing über Konsolenbefehle**

Alternativ kann das Pairing über Konsolenbefehle durchgeführt werden – sowohl über das zentrale Diagnose-Objekt als auch über die serielle Konsole.

* `iohcNN pair` — Startet das Pairing für Kanal NN (1-16)
* `iohcNN pair AABBCC` — Startet 1W-Pairing mit bekannter Node-ID (Hex)
* `iohc pair cancel` — Bricht einen laufenden Pairing-Vorgang ab
* `iohcNN unpair` — Entfernt das Pairing für Kanal NN

### **Pairing-Status**

Ein fehlendes oder verlorenes Pairing wird über das Fehlerstatus-KO des Kanals (KO Kn+15) gemeldet (Code 3 = Pairing verloren). Ergänzend stehen in der ETS die Diagnosefelder des Kanals sowie die globale Pairing-Übersicht zur Verfügung, die den zuletzt gelesenen Pairing-Zustand (Ergebnis, Node-ID, Protokoll / Ziel, Diagnose) anzeigen.

Die ETS-Diagnosefelder werden über eine aktive Onlineverbindung direkt vom Gerät gelesen, während das Fehlerstatus-KO den busrelevanten Zustand dauerhaft auf dem Bus abbildet.



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

Im unidirektionalen Modus sendet das Modul Befehle, ohne auf eine Antwort zu warten. Dieser Modus ist für Geräte vorgesehen, die ausschließlich Einweg-Kommunikation unterstützen.

Bei Auswahl von 1W erscheint ein zusätzliches Feld:
* **1W Aktor-Node-ID**: Die dezimale Node-ID des Zielgeräts (0 = nicht gesetzt). Diese muss bekannt sein, z.B. durch vorheriges Beobachten mit dem Netzwerk-Scan.

Das 1W-Anlernen ist hersteller- und gerätefamilienabhängig. Für ein generisches bzw. Somfy-Profil sendet das Modul standardmäßig `0x39 REMOVE` und `0x30 ADD_CONTROLLER`. Ein wirksames VELUX-Profil verwendet zusätzlich die KLI-kompatiblen Broadcast-Ziele und schließt die Anmeldung mit STOP und anschließend AB innerhalb von drei Sekunden ab. Der gesamte Ablauf ist serialisiert; normale Funkaufträge werden erst danach bearbeitet.

> Im 1W-Modus stehen keine Positionsrückmeldung, kein Batterielevel und keine Signalstärke vom Gerät zur Verfügung. Die Positionsschätzung erfolgt ausschließlich anhand der konfigurierten Fahrzeiten.

### **Original-Fernbedienung klonen (1W-Schlüsselübernahme)**

Manche 1W-Aktoren akzeptieren ausschließlich Fernbedienungen, die zuvor über den herstellerseitigen Kopiervorgang angelernt wurden. Eine vom Modul selbst erzeugte 1W-Identität wird von solchen Aktoren verworfen, auch wenn der Funkrahmen formal korrekt aufgebaut ist. Für diesen Fall kann das Modul eine vorhandene Original-Fernbedienung klonen, anstatt sich als neues Gerät anzulernen.

Beim Kopiervorgang sendet die Original-Fernbedienung ihren Schlüssel per Funk in einem `SendKey1W`-Rahmen (`0x30`). Dieser Schlüssel ist mit dem öffentlich bekannten io-homecontrol-Übertragungsschlüssel verschlüsselt. Das Modul empfängt diesen Rahmen, entschlüsselt ihn und übernimmt die Adresse, den Schlüssel und den Hersteller der Original-Fernbedienung in das 1W-Profil des Kanals. Anschließend sendet das Modul als exakte Kopie der Original-Fernbedienung, sodass der Aktor die Befehle annimmt.

Ablauf:

1. Den Kanal in den 1W-Modus versetzen.
2. Den Klon-Empfang aktivieren, entweder in ETS über **Original-1W-Fernbedienung klonen** oder per Konsole mit `iohcNN pair1w receive` beziehungsweise `iohcNN pair1w copy SEKUNDEN`.
3. An der Original-Fernbedienung den herstellerseitigen Kopiervorgang („Fernbedienung kopieren") auslösen, sodass diese ihren Schlüssel sendet.
4. Das Modul übernimmt Adresse und Schlüssel automatisch, speichert das Profil dauerhaft und meldet die übernommene Node-ID. Der aktuelle Zustand lässt sich jederzeit mit `iohcNN pair1w status` abfragen, ein laufender Empfang mit `iohcNN pair1w stop` beenden.

> Da der 1W-Modus keine Rückmeldung des Aktors liefert, sollte die erfolgreiche Übernahme abschließend durch einen Fahrbefehl am Aktor überprüft werden.

Reagiert der Aktor nach einem erfolgreichen Klonen trotzdem nicht auf Befehle, liegt dies meist an der **1W Befehls-Priorität (ACEI)**. Manche Aktoren akzeptieren nur die exakte Priorität ihrer Original-Fernbedienung. Über den Parameter *1W Befehls-Priorität (ACEI)* lässt sich diese je Kanal einstellen; die Voreinstellung **Priorität 3 (Velux-Fernbedienung)** entspricht dem Byte einer originalen Velux-Fernbedienung und passt zu den meisten unterstützten 1W-Aktoren. Zum Ausprobieren ohne ETS-Download kann der Wert zur Laufzeit mit `iohcNN 1wacei HH` gesetzt werden (z. B. `iohcNN 1wacei 61` für Velux); diese Laufzeit-Einstellung wird beim Neustart wieder durch den ETS-Parameter ersetzt.

### **VELUX KUX/KLI anlernen und prüfen**

1. Den KUX, Antrieb oder das Fenster gemäß Herstelleranleitung in das physische PROG-/Zuordnungsfenster versetzen.
2. Im wirksamen 1W-Profil den Controller-Hersteller **VELUX** und als ACEI normalerweise `0x61` wählen.
3. Den Parameter **1W Anmeldeabschluss** auf **Automatisch** belassen. Das Modul sendet dann bei VELUX vier `0x30`-Broadcasts mit derselben logischen Sequenz und danach STOP (`0xD200`) sowie RUNTER/DOWN/GESCHLOSSEN (`0xC800`) an `0x00003F`. STOP und RUNTER erhalten jeweils eine neue Sequenz.
4. Den MAC-Anhang nur aktivieren, wenn der Aktor oder eine Aufnahme der Originalfernbedienung ausdrücklich die 35-Byte-Form zeigt; üblich ist die 29-Byte-Form ohne Anhang.
5. Die physische Bestätigung des Ziels abwarten und anschließend AUF, STOPP und AB testen. Nach einem Neustart erneut testen, damit Schlüssel und Sequenzreserve geprüft sind.

Falls keine Bestätigung erfolgt, `iohcNN 1wctrl status` und `iohc pairdiag status` prüfen. Relevant sind Profilkanal, Controller-Quelle und -Hersteller, Broadcast-Typ, ACEI, MAC-Variante, aufgelöster Anmeldeabschluss sowie die Phasen REMOVE, ADD, STOP und DOWN mit Ziel, Sequenz, Zeit und TX-Ergebnis. Schlüsselmaterial wird nicht ausgegeben. Da 1W keine Bestätigung sendet, beweist ein erfolgreiches TX-Protokoll allein noch kein angenommenes Pairing; bei der Fehlersuche ist eine zweite Empfangseinheit hilfreich.



## **Positionssteuerung**

### **Positionsbefehle**

Position wird als Prozentwert (0-100%) über DPT 5.001 gesteuert:
* **0%** = vollständig geöffnet / oben / eingefahren
* **100%** = vollständig geschlossen / unten / ausgefahren

Zusätzlich stehen Auf/Ab (DPT 1.008) und Stopp (DPT 1.017) als Befehle zur Verfügung.

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

### **Pairing-Übersicht**

Die globale Seite **Allgemein** enthält eine read-only **Pairing-Übersicht** für alle aktivierten io-homecontrol-Kanäle. Suspendierte Kanäle bleiben Teil der Übersicht; deaktivierte Kanäle werden ausgeblendet.

Dort werden pro Kanal dieselben vier Werte wie auf der jeweiligen Kanalseite angezeigt:

* **Ergebnis**
* **Node-ID**
* **Protokoll / Ziel**
* **Diagnose**

Über die Schaltfläche **Pairing-Übersicht aktualisieren** werden diese Werte für alle aktivierten Kanäle nacheinander direkt vom Gerät gelesen. Deaktivierte Kanäle werden dabei übersprungen. Das Feld **Zuletzt aktualisiert** zeigt, wann dieser Sammel-Refresh zuletzt erfolgreich abgeschlossen wurde.

Auf derselben Seite befindet sich zusätzlich die ETS-Aktion **2W-Schlüssel extrahieren**. Sie aktiviert die temporäre 2W-Geräterolle des Moduls für die aktive Schlüsselextraktion. Nach dem Start muss am vorhandenen Fremd-Gateway der normale Vorgang zum Hinzufügen eines Geräts ausgelöst werden, damit das Gateway seinen Systemschlüssel an das Modul überträgt. Diese Funktion ist ausschließlich für eigene oder berechtigt verwaltete Fremd-Gateways vorgesehen.

<!-- DOC HelpContext="IOHC-Fernbedienungs-Beobachtung" -->
### **Fernbedienungs-Beobachtung**

Bei aktivierter Fernbedienungs-Beobachtung lauscht das Modul passiv auf io-homecontrol-Funkverkehr und meldet erkannte Fernbedienungsadressen über das globale KO "Beobachtete Fernbedienung" (DPT 12.001).

Die Funktion dient der Identifikation vorhandener io-homecontrol-Fernbedienungen sowie der Erfassung ihrer Adressen für die weitere Konfiguration.

### **Discovery und Netzwerk-Scan**

Discovery und Netzwerk-Scan werden über Kommunikationsobjekte gesteuert. Die zugehörigen KOs befinden sich in der Objekttabelle des globalen Kanals.

<!-- DOC Skip="1" -->
#### **Discovery starten**

Wird eine 1 auf das KO "Discovery starten" gesendet, beginnt das Modul einen aktiven Broadcast-Scan, um io-homecontrol-Geräte in der Umgebung zu erkennen. Der Status wird über das KO "Discovery aktiv" zurückgemeldet.
<!-- DOCEND -->

<!-- DOC Skip="1" -->
#### **Netzwerk-Scan**

Wird eine 1 auf das KO "Netzwerk-Scan" gesendet, beginnt das Modul einen passiven Scan. Dabei wird auf allen drei Frequenzen nach io-homecontrol-Paketen gelauscht, ohne selbst zu senden. Der Scan dient Diagnosezwecken und der Ermittlung von Node-IDs. Der Status wird über das KO "Netzwerk-Scan aktiv" zurückgemeldet.
<!-- DOCEND -->

----

## **Kanalauswahl**

Die Kanalauswahl steht direkt vor dem ersten Kanal. Sie listet alle io-homecontrol-Kanäle in einer Tabelle mit den Spalten **Kanal**, **Kanalaktivität** und **Beschreibung** auf.

Die Beschreibung bleibt auch bei deaktivierten Kanälen sichtbar und editierbar. Nur aktivierte Kanäle erhalten eine eigene Kanalseite mit den weiteren Einstellungen.

----

<!-- DOC HelpContext="IOHC-Kanal" -->
## **Kanal n**

Jeder io-homecontrol-Kanal repräsentiert ein einzelnes io-homecontrol-Gerät. Die Parametrierung erfolgt auf einer eigenen ETS-Seite.

### **Kanalkonfiguration**

#### **Beschreibung**

Ein Freitextfeld mit bis zu 40 Zeichen zur Benennung des Kanals. Der Text wird in ETS als Kanalname sowie in den Kommunikationsobjekten angezeigt.

<!-- DOC HelpContext="IOHC-Kanal-aktiv" -->
#### **Kanalaktivität**

Legt fest, ob der Kanal verwendet wird. Nur aktivierte Kanäle werden von der Firmware verarbeitet und blenden ihre weiteren Konfigurationsparameter ein.

* **Deaktiviert**: Kanal wird nicht verwendet (Standard für Kanal 2 bis 16)
* **Aktiviert**: Kanal ist aktiv (Standard für Kanal 1)

<!-- DOC HelpContext="IOHC-Geraetetyp" -->
### **Gerätetyp**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

Bestimmt den Typ des angeschlossenen io-homecontrol-Geräts. Die Auswahl beeinflusst, welche Kommunikationsobjekte und Parameter für diesen Kanal sichtbar sind.

Mögliche Werte:
* Generisch (0)
* Jalousie / Rollladen (1) — Standard
* Fenster (2)
* Markise (3)
* Garagentor (4)
* Thermostat (5)
* Licht (6)
* Tor (7)
* Schloss (8)
* Sonnenschutz horizontal (9)
* Vorhangschiene (10)
* Lüftung (11)
* Schalter (12)

> Bei Gerätetypen mit Positionssteuerung (0-4, 7, 9-11) werden zusätzlich die Parameter Öffnungszeit, Schließzeit und Richtung invertieren angezeigt. Beim Gerätetyp Thermostat (5) erscheinen die Thermostat-spezifischen KOs.

#### **Suspendiert**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

Setzt den Kanal vorübergehend still, ohne die Konfiguration zu verlieren. Ein suspendierter Kanal wird von der Firmware nicht verarbeitet (kein Senden, keine Statusabfrage), behält aber alle Einstellungen.

* **Nein** (Standard): Kanal ist aktiv
* **Ja**: Kanal ist suspendiert

<!-- DOC HelpContext="IOHC-Status-Abfrageintervall" -->
### **Status-Abfrageintervall**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

Legt fest, in welchem Intervall das Modul den Gerätestatus abfragt. Dieses Intervall dient als Fallback; bei Geräten mit eigenständigen Statusmeldungen wird es lediglich ergänzend verwendet.

Mögliche Werte:
* Deaktiviert (0)
* 30 Sekunden
* 1 Minute
* 5 Minuten (Standard)
* 15 Minuten
* 30 Minuten

> Ein kürzeres Intervall erhöht die Duty-Cycle-Auslastung des 868-MHz-Bandes. Die Wahl des Intervalls sollte daher an die Anforderungen der jeweiligen Anlage angepasst werden.

<!-- DOC HelpContext="IOHC-Fahrzeit" -->
### **Fahrzeit**

Erscheint nur für Gerätetypen mit Positionssteuerung (Generisch, Jalousie, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung).

#### **Öffnungszeit (Sekunden)**

Die Zeit in Sekunden, die das Gerät für eine vollständige Fahrt von geschlossen (100%) nach offen (0%) benötigt. Wertebereich: 0,1 bis 300 Sekunden, Standard: 30,0.

#### **Schließzeit (Sekunden)**

Die Zeit in Sekunden, die das Gerät für eine vollständige Fahrt von offen (0%) nach geschlossen (100%) benötigt. Wertebereich: 0,1 bis 300 Sekunden, Standard: 30,0.

> Die Fahrzeiten werden für die Positionsschätzung während der Fahrt verwendet. Im 2W-Modus wird die tatsächliche Position nach Abschluss der Fahrt vom Gerät abgefragt.

<!-- DOC HelpContext="IOHC-Richtung-invertieren" -->
### **Richtung invertieren**

Erscheint nur für Gerätetypen mit Positionssteuerung.

Vertauscht die Bedeutung von 0 % und 100 %. Dies ist sinnvoll, wenn Geräte in entgegengesetzter Orientierung montiert sind.

* **Aus** (Standard): 0% = offen, 100% = geschlossen
* **Ein**: 0% = geschlossen, 100% = offen

<!-- DOC HelpContext="IOHC-Verhalten-nach-Neustart" -->
### **Verhalten nach Neustart**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

Bestimmt, was das Modul nach einem Neustart (z.B. Stromausfall) für diesen Kanal tut.

Mögliche Werte:
* **Nichts tun** (0): Kein Befehl wird gesendet
* **Status abfragen** (1) — Standard: Das Modul fragt die aktuelle Position vom Gerät ab
* **Letzte Position anfahren** (2): Das Modul sendet nach dem Neustart den zuletzt gespeicherten Zustand erneut. Bei Gerätetypen mit Positionssteuerung wird die letzte Positionsrückmeldung verwendet. Bei Licht- und Schalter-Kanälen ohne Dimmfunktion sowie bei Schloss-Kanälen wird der letzte Ein/Aus-Zustand verwendet; dimmbare Lichtkanäle stellen die letzte Helligkeitsrückmeldung wieder her.

<!-- DOC HelpContext="IOHC-Protokoll-Modus" -->
### **Protokoll-Modus**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

Wählt den Kommunikationsmodus für diesen Kanal.

* **2W (bidirektional)** (Standard): Vollständige Zwei-Wege-Kommunikation mit Bestätigung und Statusrückmeldung
* **1W (unidirektional)**: Nur Senden, keine Antwort erwartet. Für ältere Geräte ohne 2W-Unterstützung.

Bei Auswahl von 1W erscheint zusätzlich:

#### **1W Aktor-Node-ID**

Die dezimale Node-ID des Zielgeräts. Sie muss bekannt sein und kann beispielsweise über den Netzwerk-Scan ermittelt werden. Wertebereich: 0 bis 16777215 (24 Bit). 0 bedeutet "nicht gesetzt".

#### **1W Anmeldeabschluss**

Legt den Abschluss des 1W-Anlernens fest:

* **Automatisch** (Standard): STOP + RUNTER nur für ein wirksames VELUX-Controllerprofil; für Somfy und unbekannte Hersteller kein Abschluss.
* **Keiner**: Nach `0x30 ADD_CONTROLLER` werden keine Abschlussbefehle gesendet.
* **STOP + RUNTER**: Erzwingt den VELUX-kompatiblen Abschluss unabhängig vom Herstellerprofil.

STOP und RUNTER werden als vollständige 1W-Sendebursts mit eigenen fortlaufenden Sequenzen an ALL (`0x00003F`) gesendet. RUNTER (`DOWN`, `0xC800`) beginnt deterministisch und deutlich vor Ablauf der Drei-Sekunden-Grenze. Schlägt eine Phase fehl, werden abhängige Folgephasen nicht blind ausgeführt und das Anlernen als fehlgeschlagen protokolliert.

<!-- DOC HelpContext="IOHC-Anzahl-Szenen" -->
### **Szenen**

Erscheint nur, wenn die Kanalaktivität auf "Aktiviert" steht.

#### **Anzahl Szenen**

Bestimmt, wie viele Szenen für diesen Kanal konfiguriert werden. Mögliche Werte: Keine, 1-10 Szenen.

Die Szenen-KOs erscheinen, sobald hier mindestens `1` Szene konfiguriert ist. Einen separaten Parameter `Szenensteuerung aktivieren` gibt es nicht.

Für jede aktive Szene erscheinen abhängig vom Gerätetyp unterschiedliche Parameter:

<!-- DOC HelpContext="IOHC-Szenenaktion" -->
**Für positionsbasierte Geräte (Generisch, Jalousie, Fenster, Markise, etc.):**

* **Szene n Aktion**: Position / Favorit / Lüftung — Bestimmt, welcher Befehl bei Szenenaufruf gesendet wird.

<!-- DOC HelpContext="IOHC-Szenenposition" -->
* **Szene n Position** (nur bei Aktion = Position): Zielposition 0-100%.

<!-- DOC HelpContext="IOHC-Szenenlamelle" -->
* **Szene n Lamellenposition** (nur bei Jalousie / Sonnenschutz horizontal): Lamellenwinkel 0-100%.

<!-- DOC HelpContext="IOHC-Szenentemperatur" -->
**Für Thermostate:**

* **Szene n Temperatur**: Zieltemperatur 7-28 °C.

<!-- DOC HelpContext="IOHC-Szenenmodus" -->
* **Szene n Modus**: Auto / Manuell / Programm / Aus.

<!-- DOC HelpContext="IOHC-Szenenzustand" -->
**Für Licht und Schalter ohne Dimmfunktion sowie für Schloss:**

* **Szene n Zustand**: Ein / Aus.

**Für Licht mit aktivierter Eigenschaft Dimmbar:**

* **Szene n Position**: Helligkeit 0-100 %.

<!-- DOC HelpContext="IOHC-Szenensteuerung" -->
Szenen werden über die Kommunikationsobjekte "Szene" (DPT 17.001, Szenennummer 1-10) und "Szenensteuerung" (DPT 18.001, Lernen/Abrufen) aufgerufen.

<!-- DOC HelpContext="IOHC-Pairing-Modus" -->
### **Pairing**

Auf jeder Kanalseite befindet sich ein Pairing-Bereich zur Verwaltung der Gerätekopplung.

#### **Pairing-Modus**

* **Anlernen**: Bereitet den Kanal für ein neues Pairing vor. Über den ETS-Button **Pairing starten** wird der Vorgang ausgelöst.
* **Entfernen**: Über den ETS-Button **Pairing entfernen** wird die Kopplung für diesen Kanal gelöscht.

Zusätzlich steht der ETS-Button **Pairing-Status auslesen** zur Verfügung. Damit können die Diagnosefelder manuell aktualisiert werden, ohne ein neues Pairing zu starten.

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

## **Konsole und Diagnose-Objekt**

Das Modul stellt Diagnose- und Pairing-Funktionen über Konsolenbefehle bereit. Alle Befehle beginnen mit dem Präfix `iohc` und stehen über zwei gleichwertige Wege zur Verfügung:

* **Zentrales Diagnose-Objekt**: Die Befehle können über das zentrale OpenKNX-Diagnose-Kommunikationsobjekt gesendet werden; kompakte Statusantworten werden auf das Diagnose-Objekt zurückgeschrieben. Dies ist im normalen Betrieb der empfohlene Weg, da eine serielle Verbindung dort in der Regel nicht verfügbar ist.
* **Serielle Konsole**: Während der Inbetriebnahme oder bei direktem Zugang zur Hardware können dieselben Befehle über die serielle Konsole abgesetzt werden. Ausführliche Diagnoseausgaben (z.B. Paket-Dumps) erscheinen ausschließlich in der seriellen Ausgabe.

Bei kanalbezogenen Befehlen wird die Kanalnummer direkt an das Präfix angehängt (`iohcNN`, z.B. `iohc3 status`). So bleiben die wichtigsten Befehle innerhalb der Eingabegrenze des Diagnose-Objekts.

### **Allgemeine Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohc help` | Zeigt verfügbare Befehle |
| `iohc status` | Zeigt Pairing-Status aller Kanäle |
| `iohcNN status` | Zeigt Details für Kanal NN |
| `iohc 2wdiag power auto\|always\|low` | Überschreibt die 2W-Leistungsklasse temporär im RAM (nur Diagnose) |
| `iohc 2wdiag preamble auto\|N` | Überschreibt die Präambellänge gerichteter 2W-START-Frames temporär; Fortsetzungsframes bleiben kurz |
| `iohc 2wdiag status\|reset` | Zeigt bzw. löscht die temporären 2W-Diagnose-Overrides |

### **Pairing-Befehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohcNN pair` | Startet Pairing für Kanal NN |
| `iohcNN pair AABBCC` | Startet 1W-Pairing mit bekannter Node-ID (Hex) |
| `iohc pair cancel` | Bricht laufenden Pairing-Vorgang ab |
| `iohcNN unpair` | Entfernt Pairing für Kanal NN |
| `iohcNN pair1w receive` | Klont eine Original-Fernbedienung über deren Kopiervorgang (1W-Schlüsselübernahme) |
| `iohcNN pair1w copy SEC` | Wie `receive`, jedoch mit eigener Wartezeit in Sekunden |
| `iohcNN pair1w stop` | Beendet einen laufenden Klon-Empfang |
| `iohcNN pair1w status` | Zeigt den Zustand des Klon-Empfangs (z.B. übernommene Node-ID) |
| `iohcNN 1wctrl status` | Zeigt das wirksame 1W-Profil einschließlich konfiguriertem/aufgelöstem Anmeldeabschluss und der letzten Enrollment-Phasen |

### **Steuerungsbefehle**

| Befehl | Beschreibung |
|--------|-------------|
| `iohcNN send PP` | Sendet Position PP% an Kanal NN |
| `iohc discover` | Startet Broadcast-Discovery-Scan |
| `iohcNN set1w` | Setzt Kanal auf 1W-Modus |
| `iohcNN set2w` | Setzt Kanal auf 2W-Modus |
| `iohcNN 1wacei [HH]` | Zeigt/setzt das 1W-Befehls-Priorität-Byte (ACEI), z.B. `61` für Velux |

### **Thermostat-Befehle (Atlantic Cozy)**

| Befehl | Beschreibung |
|--------|-------------|
| `iohcNN cozy temp TT` | Setzt Temperatur (in Zehntel, z.B. 215 = 21,5°C) |
| `iohcNN cozy mode MM` | Setzt Betriebsmodus |
| `iohcNN cozy presence 0/1` | Setzt Anwesenheit |
| `iohcNN cozy window 0/1` | Setzt Fensterkontakt |
| `iohcNN cozy poweron` | Sendet Einschalt-Befehl |
| `iohcNN cozy midnight` | Sendet Mitternachts-Zeitsync |

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
| `iohc scan dump` | Zeigt empfangene Pakete inkl. vollständigem Frame-Hex |
| `iohc scan stats` | Zeigt Statistiken pro Node |

Während ein Netzwerk-Scan aktiv ist, wird jedes empfangene Paket sofort als
`Scan rx:`-Logzeile mit den exakten Funkbytes (`hex=...`) ausgegeben. Dadurch
lässt sich der Tastendruck einer Original-Fernbedienung direkt mitloggen und
byteweise mit der `PairDiag: tx1w ... hex=...`-Zeile des Moduls vergleichen.

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
* Radio-Pins: RST, DIO0/DIO1, optional DIO4/BUSY

----

## **Kommunikationsobjekte**

### **Globale Kommunikationsobjekte**

Folgende Kommunikationsobjekte gelten für das gesamte io-homecontrol-Modul:

| KO | Name | DPT | Richtung | Beschreibung |
|----|------|-----|----------|-------------|
| 20 | Modulstatus | 1.011 | Lesen | 1 = Radio initialisiert, 0 = nicht bereit |
| 21 | Discovery starten | 1.010 | Schreiben | 1 = Broadcast-Discovery starten |
| 22 | Discovery aktiv | 1.011 | Lesen | 1 = Discovery läuft |
| 23 | Netzwerk-Scan | 1.010 | Schreiben | 1 = Passiven Scan starten, 0 = Stoppen |
| 24 | Netzwerk-Scan aktiv | 1.011 | Lesen | 1 = Scan läuft |
| 25 | Beobachtete Fernbedienung | 12.001 | Lesen | Zuletzt beobachtete Fernbedienungs-Adresse |

### **Kommunikationsobjekte pro Kanal**

Jeder Kanal hat bis zu 25 Kommunikationsobjekte. Die KO-Nummern sind relativ zum Kanal-Offset (Kn = erster KO des Kanals n).

Welche dieser Objekte sichtbar sind, hängt vom Gerätetyp ab. Die folgenden Tabellen sind nach Funktionsgruppe gegliedert.

#### **Antriebs- und Positions-KOs**

Sichtbar bei Gerätetypen mit Positionssteuerung (Generisch, Jalousie / Rollladen, Fenster, Markise, Garagentor, Tor, Sonnenschutz horizontal, Vorhangschiene, Lüftung).

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+0 | Position | 5.001 | Schreiben | Zielposition 0-100% |
| Kn+1 | Position Rückmeldung | 5.001 | Lesen | Aktuelle Position |
| Kn+2 | Auf/Ab | 1.008 | Schreiben | 0 = Auf, 1 = Ab |
| Kn+4 | Stopp | 1.017 | Schreiben | Laufende Fahrt stoppen |
| Kn+5 | Bewegt | 1.011 | Lesen | 1 = fährt, 0 = steht |
| Kn+8 | Lamellenposition | 5.001 | Schreiben | Lamellenwinkel (nur Jalousie / Sonnenschutz horizontal) |
| Kn+9 | Lamelle Rückmeldung | 5.001 | Lesen | Aktuelle Lamellenposition (nur Jalousie / Sonnenschutz horizontal) |
| Kn+10 | Favorit-Position | 1.017 | Schreiben | Im Gerät gespeicherte Vorzugsposition anfahren |
| Kn+11 | Lüftungsposition | 1.017 | Schreiben | Lüftungsstellung anfahren (nur Fenster / Lüftung) |
| Kn+19 | Langzeitbetrieb | 1.008 | Schreiben | Step-Stop (nur Jalousie / Rollladen) |

#### **Licht-/Schalter-KOs**

Sichtbar bei den Gerätetypen Licht und Schalter. Diese Objekte ersetzen die Antriebs-KOs durch semantisch passende Schalt-Objekte.

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+3 | Ein/Aus | 1.001 | Schreiben | 1 = Ein, 0 = Aus |
| Kn+6 | Status | 1.001 | Lesen | 1 = Ein, 0 = Aus |

Für den Gerätetyp Licht mit aktivierter Eigenschaft **Dimmbar** werden zusätzlich die Positionsobjekte Kn+0 und Kn+1 eingeblendet. Sie übertragen die Helligkeit als DPT 5.001 im Bereich 0-100 %; die Schaltobjekte Kn+3 und Kn+6 bleiben parallel sichtbar.

#### **Schloss-KO**

Sichtbar beim Gerätetyp Schloss.

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+7 | Status | 1.011 | Lesen | 1 = verriegelt, 0 = entriegelt |

#### **Allgemeine KOs (alle Gerätetypen)**

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+12 | Batterielevel | 5.001 | Lesen | 0-100% (für Solar-/Batteriegeräte) |
| Kn+13 | Signalstärke | 5.001 | Lesen | RSSI 0-100% |
| Kn+14 | Sperren | 1.003 | Schreiben | 1 = Kanal sperren, 0 = entsperren |
| Kn+15 | Fehlerstatus | — (1 Byte) | Lesen | 0=OK, 1=Kommunikationsfehler, 2=Duty-Cycle, 3=Pairing verloren, 4=Funkstörung |

> Das Fehlerstatus-KO (Kn+15) ist ein proprietäres 1-Byte-Objekt ohne standardisierten DPT.

#### **Wind-/Regenalarm-KO**

Sichtbar bei Gerätetypen mit Positionssteuerung.

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+18 | Wind-/Regenalarm | 1.005 | Schreiben | 1 = Alarm, 0 = Entwarnung |

#### **Szenen-KOs**

Sichtbar, sobald für den Kanal mindestens eine Szene konfiguriert ist (alle Gerätetypen).

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+16 | Szene | 17.001 | Schreiben | Szene aufrufen (1-10) |
| Kn+17 | Szenensteuerung | 18.001 | Schreiben | Szene lernen / abrufen |

#### **Thermostat-KOs (nur Gerätetyp Thermostat)**

| Offset | Name | DPT | Richtung | Beschreibung |
|--------|------|-----|----------|-------------|
| Kn+20 | Temperatur Sollwert | 9.001 | Schreiben | Zieltemperatur |
| Kn+21 | Temperatur Rückmeldung | 9.001 | Lesen | Aktuelle Temperatur |
| Kn+22 | Betriebsmodus | 20.102 | Schreiben | HVAC-Modus |
| Kn+23 | Anwesenheit | 1.018 | Schreiben | Anwesenheitsstatus |
| Kn+24 | Fensterkontakt | 1.019 | Schreiben | Fenster offen/geschlossen |

> Für die Gerätetypen Licht und Schalter stellt die Firmware eigene KOs mit semantisch korrektem DPT bereit ("Ein/Aus" und "Status", jeweils DPT 1.001). Sie ersetzen die Antriebs-KOs "Auf/Ab" (DPT 1.008) und "Bewegt" (DPT 1.011), sodass eine direkt am KO erstellte Gruppenadresse den passenden Datentyp erhält. Beim Gerätetyp Schloss sind das KO "Status" (DPT 1.011) sowie das generische KO "Sperren" sichtbar.
