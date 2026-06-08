### Allgemein

Auf dieser Seite wird die Grundstruktur des io-homecontrol-Bereichs festgelegt.
In der Regel ist hier lediglich die Anzahl der tatsächlich benötigten Kanäle zu definieren.

Pairing, Betriebsparameter und Bedienfunktionen werden anschließend je Kanal konfiguriert.
Dadurch bleibt die Zuordnung zwischen Kanal und Antrieb bzw. Fenster eindeutig nachvollziehbar.

Die globale Systemidentität wird ausschließlich für 2W verwendet. Jeder 1W-Kanal besitzt standardmäßig ein eigenes persistentes Controllerprofil; mehrere Kanäle können ein solches Profil gezielt teilen.

Die globale Pairing-Übersicht liest den aktuellen Zustand aller sichtbaren Kanäle über eine ETS-Onlineverbindung aus. Optional kann außerdem die passive Fernbedienungs-Beobachtung aktiviert werden.
