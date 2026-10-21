### Pairing-Modus

Dieser Parameter legt fest, welche Funktion die darunterliegende Schaltfläche ausführt.

Die Option "Anlernen" wird für neue oder neu zuzuordnende Geräte verwendet.
Die Option "Entfernen" hebt die bestehende Zuordnung des Kanals auf; das Gerät muss anschließend bei Bedarf erneut angelernt werden.

Pro Pairing-Versuch nur eine PROG-/Registrierungsgeste am Gerät oder an der vorhandenen Fernbedienung auslösen. PROG nicht sofort nochmals drücken, solange das geöffnete Anlernfenster noch aktiv ist; eine zweite Geste kann das Fenster schließen oder die Add/Remove-Funktion umschalten.

Die Schaltfläche steht nur zur Verfügung, wenn ETS online mit dem Gerät verbunden ist.
Ein eigenes Kommunikationsobjekt "Pairing-Status" gibt es nicht. Der aktuelle Zustand kann über **Pairing-Status auslesen** in die ETS-Diagnosefelder übernommen werden; ein fehlendes oder verlorenes Pairing wird zusätzlich über den Fehlerstatus des Kanals gemeldet.

Bei einem erkannten Atlantic-Pairing-Node (Hersteller 12, Profil 1023/0) bietet normales 2W-Pairing den passenden Pull-Workflow an und sendet keinen Key-Push. **Status / Erkennung lesen** zeigt den Hinweis und CLI-Befehl in einem leeren 2W-Kanal. Das Angebot verfällt nach drei Minuten und verändert keine Kanalzuordnung oder Schlüssel. Pull und die spätere dauerhafte Zuordnung zu einem verifizierten Heizkörper müssen ausdrücklich gestartet werden.
