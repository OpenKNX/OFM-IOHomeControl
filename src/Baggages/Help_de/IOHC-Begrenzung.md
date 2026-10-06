# Begrenzungsstatus

Zeigt „Begrenzung aktiv“ für einen gepaarten 2W-Aktor: 1 = eine Einschränkung wurde gemeldet oder aus Regenstatus erkannt, 0 = ein frisch bestätigter uneingeschränkter Mindest-/Höchstbereich. Es werden keine Grenzen verändert.

Die direkte Grenzabfrage hat Vorrang. Fehlt sie, werden eindeutige Begrenzungsfehler und bei Positionsgeräten Regenmeldungen ausgewertet. Eine abweichende Endposition zählt nur als Regenbegrenzung, wenn zuvor Regen gemeldet wurde. Die sofortige Fahrbefehlsbestätigung zählt nicht als Regenstatus.

Standard: aus. Bei Aktivierung wird nach Start/Pairing einmal gelesen, danach im gewählten Intervall (Standard 5 Minuten). „Aus“ beim Intervall beendet nur zyklische Abfragen. Regenstatus löst keine zusätzlichen Abfragen aus.

Direkte Grenzen müssen aus derselben Abfrage innerhalb von 5 Sekunden eintreffen und gelten höchstens 5 Minuten. Regenhinweise werden vorläufig höchstens 2 Stunden berücksichtigt; eine bestätigte unbeschränkte Fahrt löscht sie. Geräte-, Profil- oder Schlüsselwechsel verwerfen alte Hinweise. Fehlende oder alte Daten bedeuten unbekannt und senden keine falsche 0. KNX-Leseanfragen werden nur mit einem gültigen Zustand beantwortet.

Feld- und Timerdeutung sowie die Regenableitung sind vorläufig; reale Funk-/Regentests und herstellerübergreifende Bestätigung stehen noch aus. „iohc limitation CH status“ zeigt die Quelle und Rohdaten. Der Diagnosebefehl „iohc probe CH status_mp_fp“ protokolliert eine KLF-Statusantwort ohne ihre Werte zu übernehmen.
