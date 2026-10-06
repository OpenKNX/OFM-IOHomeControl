# Batterieüberwachung

**Aus:** Keine Batterieabfragen oder Batteriemeldungen.

**Nur Status:** Wertet vorhandene, vertrauenswürdige Statusantworten aus. Zusätzliche Funkabfragen werden nicht gestartet.

**Erweiterte Diagnose:** Erlaubt manuelle Objekt- und Batterieabfragen über die Konsole. Es gibt keine automatische oder zyklische Batterieabfrage; schlafende Geräte werden dadurch nicht regelmäßig geweckt.

**Batterie schwach (DPT 1.005):** 1 bedeutet schwach, 0 bedeutet nachweislich normal/voll. Ohne gültigen Messzustand sendet und beantwortet das Objekt nichts. Unbekannte Antworten löschen keinen vorher bestätigten Zustand. Der bestätigte Zustand gilt bis zur nächsten bekannten Rückmeldung; er wird bei einem Neustart oder einer Änderung der Gerätezuordnung verworfen.

Ein bestätigter A601-Batteriezustand hat Vorrang vor dem allgemeinen Status. Widersprüche und das Alter beider Quellen sind in der Konsole sichtbar. Die Batterie eines angelernten 1W-Controllers gehört zum Controller und verändert die Batterieanzeige des Antriebs nicht.

Der bisherige Batterie-Prozentwert bleibt unbekannt: Batterieklassen, Rohspannungen und unbestätigte Zahlen werden nicht in Prozent umgerechnet. Diagnosewerte mit „RAW / UNIT UNKNOWN“ haben noch keine belegte Einheit oder Skala.
