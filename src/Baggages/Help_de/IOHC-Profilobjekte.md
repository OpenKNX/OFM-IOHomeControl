# Separate Profilfunktionen

Zeigt für das erkannte oder manuell vorgegebene Profil eigene Kommunikationsobjekte je Funktion. Beispiel Außenjalousie: Position (MP), Fahrgeschwindigkeit (FP1), Lamellen-Drehgeschwindigkeit (FP2) und Lamellenposition (FP3). Jede Funktion lässt sich unabhängig bedienen und rückmelden. Die Belegung steht in der Hilfe zu Gerätetyp und MP/FP-Index.

Nach Pairing „Status / Erkennung lesen“, danach die Applikation programmieren. Ein manuelles Profil hat Vorrang. Passt das programmierte Objektprofil nicht zum wirksamen Geräteprofil, bleiben diese Objekte gesperrt. Deaktivierte und 1W-Kanäle zeigen keine separaten Profilobjekte.

- „setzen“: Prozentwerte mit DPT 5.001; Ein/Aus und Verriegelung mit DPT 1.001. MP steuert die Hauptfunktion, FP die benannte Zusatzfunktion.
- „Rückmeldung“: Wert aus einer passenden Geräteantwort. Ein Schreibtelegramm erzeugt keine bestätigte Rückmeldung.
- „gültig“: Aus bis zum ersten nutzbaren Empfang. Wird bei geänderter Gerätezuordnung, nicht nutzbarem Rohwert oder veraltetem Wert wieder Aus. Die Frist beträgt mindestens 30 Sekunden, sonst drei Status-Abfrageintervalle.
- „Profilwerte lesen“: Ein-Telegramm fragt MP und die zugeordneten FP gemeinsam ab. Normale Statusabfragen lesen diese Werte ebenfalls.

Eine über das Prozent-KO gewählte Fahrgeschwindigkeit gilt auch für folgende Positions- und Favoritfahrten. Schreiben auf „Fahrmodus“ ersetzt diese Vorgabe durch Normal / Silent / Fast. Auch ein gewählter Dimm- oder Heizleistungsübergang bleibt bei folgenden Hauptwertbefehlen erhalten. Nach Neustart ist die Prozentvorgabe gelöscht; es gilt der Standard-Fahrmodus. Nicht jeder Motor unterstützt jede Geschwindigkeit.

Ausstellwert (FP9 bei Rollladen mit Ausstellfunktion) ist nur als rohes 16-Bit-Ergebnis lesbar; keine Grad-Umrechnung oder Schreibfunktion ist bestätigt. Fenster-Sicherheitsmodus (FP1 bei Fensterschloss) ist nur lesbar: 0 = daylocked, 1 = homesecure, 2 = secured. Unbekannte Werte ergeben keine gültige Rückmeldung.

Die Objekte ergänzen bestehende Kanalobjekte; deren Nummern bleiben erhalten. Der einzelne MP/FP-Index dient weiterhin der Diagnose. Erweiterte Produktdefinitionen, etwa RGB, Warmwasser oder Wärmepumpen, haben eigene Kodierungen und werden durch diese Standardprofilobjekte nicht freigeschaltet.
