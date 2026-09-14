# 1W Energieklasse des Controllerprofils

Legt die Präambel und das `LOW_POWER`-Bit für alle Telegramme fest, die mit dieser 1W-Controlleridentität gesendet werden. Die Einstellung gilt auch für Kanäle, die dieses Controllerprofil gemeinsam verwenden.

- **Automatisch** erhält das bisherige Verhalten: Die erste Kopie verwendet 1024 Symbole. VELUX-Profile verwenden für Wiederholungen 8 Symbole, andere Profile 1024.
- **Immer erreichbar** verwendet für alle vier Kopien die normale Präambel mit 32 Symbolen und lässt `LOW_POWER` aus.
- **Low Power** sendet zuerst eine Aufweckkopie mit 1024 Symbolen und gesetztem `LOW_POWER`. Die drei Wiederholungen verwenden 32 Symbole und lassen das Bit aus.

Für netzbetriebene Empfänger ist **Immer erreichbar** der passende Testwert. Für Solar- oder Batterieempfänger ist **Low Power** vorgesehen. Solange das Verhalten des konkreten Geräts nicht bekannt ist, bewahrt **Automatisch** die bisherige Konfiguration.
