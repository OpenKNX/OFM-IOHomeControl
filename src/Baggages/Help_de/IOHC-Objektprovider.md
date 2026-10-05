# Objektprovider

Wählt den Namensraum, in dem der Objektkey gesucht wird. Zusammen bilden Provider und Objektkey die Adresse des Metadatenobjekts. Der Provider ist weder die Funkadresse des Aktors noch ein KNX-Kommunikationsobjekt.

Genau zwei Hex-Ziffern ohne 0x eingeben:

- 00: erlaubt die Objektkeys 0000, 0001, 0002, 0003, 030A, 8100, 8103, 4300 und 4302.
- 0B: erlaubt ausschließlich C000.

Andere Kombinationen weist das Gateway ab. Eine erlaubte Kombination bedeutet noch nicht, dass der jeweilige Aktor dieses Objekt besitzt. Der genaue Aufbau der zurückgegebenen Bytes ist produktabhängig.
