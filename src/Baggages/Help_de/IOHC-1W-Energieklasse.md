# 1W Energieklasse des Controllerprofils

Legt die Präambel und das `LOW_POWER`-Bit für alle Telegramme fest, die mit dieser 1W-Controlleridentität gesendet werden. Die Einstellung gilt auch für Kanäle, die dieses Controllerprofil gemeinsam verwenden.

- **Automatisch** verwendet für VELUX das mit einem netzbetriebenen KUX 110 bestätigte Profil mit vier normalen 32-Symbol-Präambeln. Für andere Hersteller bleibt das bisherige Profil erhalten.
- **Immer erreichbar** verwendet für alle vier Kopien die normale Präambel mit 32 Symbolen und lässt `LOW_POWER` aus.
- **Low Power** sendet zuerst eine Aufweckkopie mit 1024 Symbolen und gesetztem `LOW_POWER`. Die drei Wiederholungen verwenden 32 Symbole und lassen das Bit aus.

Für netzbetriebene VELUX-Empfänger wie den KUX 110 ist **Automatisch** beziehungsweise **Immer erreichbar** vorgesehen. Für Solar- oder Batterieempfänger ist **Low Power** vorgesehen; dieses sendet das von KLI-Fernbedienungen beobachtete Profil `1024/32/32/32` mit `LOW_POWER` nur auf der ersten Kopie.

Die Energieklasse beschreibt den Empfänger, nicht die Anmeldeklasse: Auch bei KLI-312-Innenjalousien muss je nach Netz-, Solar- oder Batterieversorgung passend gewählt werden.
