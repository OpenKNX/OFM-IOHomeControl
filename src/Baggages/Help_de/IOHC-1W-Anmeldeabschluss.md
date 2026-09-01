# 1W Anmeldeabschluss

VELUX KLI-kompatible Controller schließen die Übertragung des Controller-Schlüssels mit `STOP` und anschließend `AB` innerhalb von drei Sekunden ab.

- **Automatisch** aktiviert `STOP + AB` nur für ein wirksames VELUX-Controllerprofil. Unbekannte und Somfy-Profile senden keinen Abschluss.
- **Keiner** beendet das Anlernen nach `0x30 ADD_CONTROLLER`.
- **STOP + AB** erzwingt den VELUX-Abschluss unabhängig vom Herstellerprofil.

Der Abschluss wird als Teil derselben serialisierten Anmeldeoperation gesendet. Jede logische Nachricht verwendet eine neue Sequenznummer; alle Wiederholungen einer Nachricht behalten deren Sequenznummer.
