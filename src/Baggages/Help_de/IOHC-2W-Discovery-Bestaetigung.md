# 2W Discovery-Bestätigung

Nach einer erfolgreichen Discovery-Antwort (`0x29`) sendet die normale
2W-Kopplung eine Discovery-Bestätigung (`0x2C`) an das gefundene Gerät.

- **Senden** wartet nach jeder Bestätigung tolerant auf eine optionale Antwort.
- **Senden + ACK anfordern** setzt bei Geräten ohne Energiesparmodus zusätzlich
  das ACK-Bit. Bei LOW_POWER-Geräten bleibt das ACK-Bit aus.
- **Überspringen** lässt diesen Schritt zu Diagnosezwecken aus.

Bleibt die optionale Antwort (`0x2D`) aus, wird die Bestätigung bis zu dreimal
wiederholt. Ein Timeout oder ein passender Fehler (`0xFE`) beendet die Kopplung
nicht; anschließend wird mit dem Schlüsselaustausch fortgefahren. Fremde
Antworten werden ignoriert.
