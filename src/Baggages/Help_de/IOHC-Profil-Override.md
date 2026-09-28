### Manuelles Profil (Override)

Legt bei Bedarf fest, welches unterstützte io-homecontrol-Profil die Firmware
für die Zuordnung von Befehlen und Statusdaten verwendet. Der Wert wird als
gepacktes Profil-/Unterprofil-Paar in **Dezimal** eingegeben:

`Wert = Profil * 64 + Unterprofil`

Der Wert **0** verwendet das automatisch erkannte Profil. Beispiele:

* **64**: innenliegende Jalousie (Profil `0x01`, Unterprofil `0x00`)
* **1088**: außenliegende Jalousie (Profil `0x11`, Unterprofil `0x00`)

Verwenden Sie nur dokumentierte, unterstützte Werte. Ein nicht unterstütztes
Profil wird ignoriert und die automatische Auswahl verwendet. Weicht ein
gültiger Override vom erkannten Profil ab, wird dies protokolliert. Die vom
Gerät erkannte Profilidentität bleibt dabei unverändert und wird weiterhin für
Diagnose und Speicherung verwendet.