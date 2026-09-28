# Dimmbar

Aktiviert für den Gerätetyp Licht zusätzlich die Helligkeitssteuerung über das Stellwertobjekt Kn+0. Eine Helligkeitsrückmeldung auf Kn+1 wird derzeit nicht veröffentlicht.

Beim ETS-Schlüsselimport wird die Eigenschaft für das bekannte Lichtprofil
`0x0180` gesetzt und für das Ein/Aus-Lichtprofil `0x01BA` deaktiviert.
Bei manuell eingerichteten oder unbekannten Geräten nur aktivieren, wenn
Zwischenwerte für die Helligkeit tatsächlich unterstützt werden.

Bei nicht dimmbaren Lichtaktoren werden Zwischenwerte in der Regel wie einfache Ein-/Aus-Befehle behandelt oder vom Gerät ignoriert.
