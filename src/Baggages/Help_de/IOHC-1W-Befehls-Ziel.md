# 1W Befehls-Ziel

Legt das Funkziel normaler 1W-Befehle fest. **Automatisch** behält den bisherigen Typ-Broadcast bei. **Alle Geräte** sendet explizit an `00003F`.

Diese Auswahl wird nicht automatisch vom Hersteller abgeleitet, weil sowohl typgebundene als auch allgemeine Fernbedienungen herstellerunabhängig vorkommen.

Sie gilt nur für normale Execute-Befehle. Das Ziel von REMOVE/ADD beim
Anlernen wird separat über **1W Anmelde-Ziel** bestimmt. Somfy verwendet dort
automatisch `00003F`, auch wenn normale Befehle als Typ-Broadcast laufen.
