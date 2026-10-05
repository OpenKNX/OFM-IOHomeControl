# Rohwerte / Alter / Gültigkeit

Zeigt die zuletzt gelesenen Diagnosewerte und ihre Einordnung:

- Rohwert: unverarbeitete Daten des Aktors, häufig eine Hex-Kennung. Erst Profil, FP-Index und Produktdefinition geben ihm eine Bedeutung.
- Alter: Zeit seit dem Empfang im Gateway; kein Datum und keine Änderungszeit des Aktors.
- Gültigkeit: ob der Wert zur aktuellen Gerätezuordnung gehört und nutzbar ist. Ein alter oder ungültiger Wert ist keine aktuelle KNX-Rückmeldung.

Die produktabhängige Dekodierung verlangt Werte, die höchstens 5 Sekunden alt sind. Zusammengehörige Werte wie RGB-FP10/FP11, Modus-FP15/FP16 oder Sirenenpaare müssen außerdem aus derselben Lesegruppe stammen. Ein erfolgreicher Funkempfang bestätigt nicht automatisch die Einheit oder Produktvariante.
