### Pairing-Modus

Dieser Parameter legt fest, welche Funktion die darunterliegende Schaltfläche ausführt.

Die Option "Anlernen" wird für neue oder neu zuzuordnende Geräte verwendet.
Die Option "Entfernen" hebt die bestehende Zuordnung des Kanals auf; das Gerät muss anschließend bei Bedarf erneut angelernt werden.

Pro Pairing-Versuch nur eine PROG-/Registrierungsgeste am Gerät oder an der vorhandenen Fernbedienung auslösen. PROG nicht sofort nochmals drücken, solange das geöffnete Anlernfenster noch aktiv ist; eine zweite Geste kann das Fenster schließen oder die Add/Remove-Funktion umschalten.

Die Schaltfläche steht nur zur Verfügung, wenn ETS online mit dem Gerät verbunden ist.
Ein eigenes Kommunikationsobjekt "Pairing-Status" gibt es nicht. Der aktuelle Zustand kann über **Pairing-Status auslesen** in die ETS-Diagnosefelder übernommen werden; ein fehlendes oder verlorenes Pairing wird zusätzlich über den Fehlerstatus des Kanals gemeldet.
