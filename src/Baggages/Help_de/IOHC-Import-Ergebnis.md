# Letzte 2W-Übernahme

Ergebnis der zuletzt abgefragten Schlüsselextraktion bzw. Geräteübernahme. Gefundene Funkadressen allein bedeuten noch keine abgeschlossene Einrichtung. Zuordnung prüfen, Ergebnis in ETS übernehmen und anschließend die Applikation programmieren.

Die Meldung unterscheidet fehlende Metadaten, nicht automatisch zuordenbare Profile,
nicht verifizierte oder nicht ausgewählte Kandidaten und tatsächlich fehlende freie
Kanäle. „Gefunden“ bedeutet nicht „zugeordnet“. Der erste Klick zeigt die Vorschau,
der zweite bestätigt die dort gezeigte Zuordnung.

Bei fehlenden Metadaten kann ein erneuter Extraktions-/Suchlauf helfen. Werden
dabei passende Metadaten erkannt, wird das Gerät bei „alle unterstützten“ zur
Zuordnung angeboten; bereits zugeordnete Geräte werden nicht doppelt angelegt.
Der Suchlauf garantiert keine Antwort eines schlafenden Geräts.

Alternativ den Kandidaten ausdrücklich wählen, einem freien Zielkanal zuordnen
und den Gerätetyp manuell setzen. `iohc metadata refresh NODE` gilt nur für bereits
zugeordnete 2W-Geräte. Anschließend in ETS „Erkannten Gerätetyp übernehmen“ ausführen.
Ein nicht zugeordneter Kandidat wird durch diesen Konsolenbefehl nicht automatisch
angelegt. Nach ETS-Änderungen die Applikation programmieren.

Die Übernahme aktiviert auch die Kanalauswahl in ETS. Bei automatischer Erkennung
erscheinen die zum erkannten Profil passenden Kommunikationsobjekte. Fehlen nach
einer älteren Übernahme die Fahr-Objekte, unter „Kanalauswahl“ den passenden
Gerätetyp wählen, Gruppenadressen zuordnen und die Applikation erneut programmieren.
Eine erneute Schlüsselextraktion ist dafür nicht nötig.
