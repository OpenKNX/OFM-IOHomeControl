### 1W Anmeldung mit MAC-Anhang (0x30)

Standardmäßig sendet die 1W-Anmeldung ein `0x30 SendKey1W` mit 29 deklarierten Bytes. Einige Original-Fernbedienungen senden zusätzlich einen sechs Byte langen MAC-Anhang außerhalb der in CTRL0 deklarierten Länge.

Diese Option aktiviert die 35-Byte-Variante nur für das eigene wirksame 1W-Controllerprofil. Der Anhang wird mit dem Controller-Schlüssel und dem Sequenzzähler erzeugt. Aktiviere ihn nur, wenn der Aktor oder eine aufgezeichnete Original-Fernbedienung diese Form verwendet.
