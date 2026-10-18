# MOTO 1000 io: Endpunkte

Nur für einen ausdrücklich als MOTO 1000 io identifizierten Garagentorantrieb einschalten.
Erlaubt AUF, ZU und STOP; Zwischenpositionen und Favorit werden vor dem Senden abgelehnt.
Der öffentliche Hardwaretest aus Issue #163 bestätigt diese Einschränkung für dieses Gerät.
Andere Garagentore können Teilöffnung unterstützen; dort ausgeschaltet lassen.
Die Option wirkt nur bei einem gültigen Garagentorprofil (0x0140 / 0x017A).
Ein Authentifizierungsaustausch allein bestätigt keine Bewegung.
