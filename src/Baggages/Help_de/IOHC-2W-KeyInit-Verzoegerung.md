# 2W Pause vor Schlüsselaustausch

Legt die Pause zwischen der Discovery-Bestätigung (`0x2C`) und dem Start des
Schlüsselaustauschs (`0x31`) fest. Der Standardwert beträgt **300 ms**; zulässig
sind **0 bis 10000 ms**.

Die Pause läuft nicht blockierend. Das Modul verarbeitet währenddessen weiter
KNX- und Funkereignisse. Wenn die Discovery-Bestätigung auf **Überspringen**
steht, beginnt der Schlüsselaustausch ohne diese zusätzliche Pause.
