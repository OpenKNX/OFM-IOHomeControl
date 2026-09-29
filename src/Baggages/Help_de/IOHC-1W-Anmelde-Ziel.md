# 1W Anmelde-Ziel

Legt ausschließlich das Funkziel der 1W-Anmeldung (`0x39 REMOVE`, `0x30 ADD`)
fest. Normale Fahrbefehle verwenden weiterhin **1W Befehls-Ziel**.

* **Automatisch nach Hersteller:** Somfy sendet REMOVE und ADD an alle Geräte
  (`00003F`), unabhängig von der automatisch ermittelten Geräteklasse. VELUX
  behält die KLI-Klassenfolge; unbekannte Hersteller behalten das bisherige
  typabhängige Verhalten.
* **Alle Geräte (`00003F`):** erzwingt dieses Ziel für REMOVE und ADD.
* **Typ-Broadcast:** erzwingt die unter **1W Broadcast-Typ** konfigurierte
  Klasse für REMOVE und ADD. Nur für bestätigte Sonderfälle verwenden, etwa
  wenn ein Empfänger die allgemeine Anmeldung nicht akzeptiert.

Die automatische Somfy-Adressierung ist durch den Smoove-Testkorpus und die
[Adressbeobachtung in Issue #147](https://github.com/laberning/home_io_control/issues/147)
gestützt. Ein erfolgreicher lokaler 1W-Funkversand bestätigt noch nicht, dass
der Aktor die Anmeldung angenommen hat.
