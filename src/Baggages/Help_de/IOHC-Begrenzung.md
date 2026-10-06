# Begrenzungsstatus

Liest die aktuell wirksame Mindest- und Höchstgrenze des Hauptwerts (MP) eines gepaarten 2W-Aktors. Aktivieren zeigt „Begrenzung aktiv“: 1 = bestätigte Einschränkung, 0 = bestätigter uneingeschränkter Bereich. Es werden keine Grenzen verändert.

Standard: aus, da die Antwort unter aktiver Begrenzung noch mit realem Funk bestätigt werden muss. Bei Aktivierung wird nach Start/Pairing einmal gelesen, danach im gewählten Intervall (Standard 5 Minuten). „Aus“ beim Intervall beendet nur zyklische Abfragen; Start- und manuelle Abfragen bleiben möglich.

Beide Antworten müssen aus derselben Abfrage stammen und innerhalb von 5 Sekunden eintreffen. Ein bestätigter Zustand gilt höchstens 5 Minuten. Fehlende, alte oder widersprüchliche Antworten bedeuten unbekannt; sie senden keine falsche 0. Ein KNX-Lesetelegramm wird nur mit einem frischen Zustand beantwortet.

Die fünf Antwortfelder und die Timerdeutung sind vorläufig. Originator und Timer dienen nur der Diagnose. Herstellerübergreifende Unterstützung ist noch nicht physisch bestätigt.
