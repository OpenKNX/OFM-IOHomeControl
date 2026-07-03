### 1W Befehls-Priorität (ACEI)

Legt das ACEI-Byte fest, das der Kanal in jedem 1W-Execute-Befehl (Auf, Ab, Stopp, Position) sendet. Das Byte kodiert unter anderem die Befehls-Priorität und muss zu dem Aktor bzw. der geklonten Original-Fernbedienung passen.

Die Priorität bestimmt, welche Steuerungsebene einen Befehl auslöst. Höhere Schutzebenen (Priorität 0 und 1) überstimmen Benutzer- und Komfortbefehle. Die auswählbaren Stufen entsprechen der io-homecontrol-Spezifikation:

* **Priorität 0 (Personenschutz)** – höchste Ebene, dem Personenschutz vorbehalten.
* **Priorität 1 (Umgebungsschutz)** – Schutz von Sachwerten, z. B. durch lokale Sensoren.
* **Priorität 2 (Benutzer hoch)** – Benutzerbefehle mit erhöhter Priorität. Die Variante **Standard** entspricht dem bisherigen Verhalten des Moduls und ist voreingestellt.
* **Priorität 3 (Benutzer Standard)** – übliche Priorität von Fernbedienungen. Die Variante **Velux-Fernbedienung** entspricht dem Byte, das eine originale Velux-Fernbedienung sendet; die Variante **2W-Fernbedienung** dem Standard des bidirektionalen Protokolls.
* **Priorität 4 und 5 (Komfort 1 / 2)** – Komfortsteuerungen.
* **Priorität 6 (Komfort 3 / SAAC)** – eigenständige automatische Steuerungen (Stand Alone Automatic Controls).
* **Priorität 7 (Komfort 4)** – niedrigste Ebene (u. a. Standardkanal des KLF100). Die Variante **Automatik** wurde bei automatischen Steuerungen beobachtet.

Reagiert ein Aktor nach einem erfolgreichen Klonen der Original-Fernbedienung nicht auf Befehle, ist die Priorität die wahrscheinlichste Ursache. In diesem Fall die Einstellung auf den Wert der Original-Fernbedienung setzen (bei Velux üblicherweise **Priorität 3 (Velux-Fernbedienung)**).
