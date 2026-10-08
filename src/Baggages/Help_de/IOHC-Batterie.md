# Batterieüberwachung

**Aus:** Keine Batterieabfragen oder Batteriemeldungen.

**Nur Status:** Wertet vorhandene, vertrauenswürdige Statusantworten aus. Zusätzliche Funkabfragen werden nicht gestartet.

**Erweiterte Diagnose:** Erlaubt manuelle Objekt- und Batterieabfragen über die Konsole. Es gibt keine automatische oder zyklische Batterieabfrage; schlafende Geräte werden dadurch nicht regelmäßig geweckt.

**Batterie schwach (DPT 1.005):** 1 bedeutet schwach, 0 bedeutet zuletzt bestätigt normal/voll. Ohne gültigen Batteriezustand wird kein neuer Wert gesendet. Eine Leseanfrage kann weiterhin den zuletzt gespeicherten Wert liefern, auch nach einer Änderung der Gerätezuordnung oder bei ausgeschalteter Überwachung. Vor der ersten bestätigten Rückmeldung und nach einem Neustart gibt es keine Leseantwort. Unbekannte Antworten löschen keinen vorher bestätigten Zustand. Nach einer Änderung der Zuordnung wird die nächste bestätigte Rückmeldung erneut gesendet, auch wenn ihr Wert gleich geblieben ist.

Die neueste bestätigte Rückmeldung bestimmt den Batteriezustand. Bei gleichem Zeitpunkt hat A601 Vorrang vor dem allgemeinen Status; danach folgt eine ausdrückliche Batteriewarnung. Widersprüche und das Alter beider Quellen sind in der Konsole sichtbar. Die Batterie eines angelernten 1W-Controllers gehört zum Controller und verändert die Batterieanzeige des Antriebs nicht.

Der bisherige Batterie-Prozentwert bleibt unbekannt: Batterieklassen, Rohspannungen und unbestätigte Zahlen werden nicht in Prozent umgerechnet. Diagnosewerte mit „RAW / UNIT UNKNOWN“ haben noch keine belegte Einheit oder Skala.

Die sichtbare Auswahl dieses Kanals ist maßgeblich; ein interner BAT-Modulschalter
schaltet sie nicht zusätzlich ab. `iohc battery 1 status` zeigt Rohparameter,
Diagnosestufe und wirksamen Modus getrennt. Eine Änderung wirkt bei der nächsten
Prüfung; bei deaktiviertem/pausiertem Kanal, 1W oder fehlender Kopplung ist keine
Probe erlaubt. Bei belegtem Funk, Kopplung oder Metadaten wartet eine manuelle
Probe höchstens zehn Sekunden. Es wird keine laufende Sitzung unterbrochen.
Eine Probe ist ein Versuch, kein Nachweis für Unterstützung oder eine Batterie.
Auch VELUX-Geräte mit allgemeinem Profil bleiben bis zur validierten Antwort unbekannt.
