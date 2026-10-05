### Szenen-Aktion

Dieser Parameter legt fest, welche Aktion beim Aufruf der Szene ausgeführt wird.

Bei der Aktion "Position" fährt der Kanal auf die hinterlegte Position.
Bei Jalousien kann zusätzlich eine Lamellenposition übertragen werden.

Bei der Aktion "Favorit" ruft das Gerät seine intern gespeicherte Favoritenposition auf.

Bei der Aktion "Lüftung" fährt das Gerät die io-homecontrol-Lüftungsposition an.
Diese Einstellung ist insbesondere für Fenster relevant.

Nur Szenen des Typs "Position" lassen sich über das KNX-Szenenlernen aus dem aktuellen Gerätestatus heraus speichern.

Die Laufzeit prüft das beobachtete oder ausdrücklich manuell gewählte Profil.
Eine Lamellenposition wird nur für ein Profil mit Orientierungsfähigkeit verwendet.
Die Lüftungsaktion ist nur für ein bestätigtes 1W-Profil mit gesicherter
Lüftungsfunktion verfügbar. Für 2W fehlt weiterhin die Qualifikation.
Produktdiagnosen und MP/FP-Codecs schalten keine Szenen-Schreibrechte frei.
