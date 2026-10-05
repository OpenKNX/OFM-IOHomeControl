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

Weitere häufige Dezimalwerte: 128 = Rollladen, 129 = Rollladen mit Lamellen, 130 = Rollladen mit Ausstellfunktion, 192 = vertikale Außenmarkise, 256 = Fenster, 257 = Fenster mit Regensensor, 320 = Garagentor, 384 = Licht, 448 = Tor, 576 = Türschloss, 577 = Fensterschloss, 640 = Innenrollo, 832 = Doppelrollladen, 960 = Schalter, 1024 = horizontale Markise, 1152 = Lamellenbehang, 1216 = Vorhangschiene, 1280 = Lüftungspunkt, 1344 = Außenheizung, 1536 = Klappladen. Die MP/FP-Belegung dieser Varianten ist in der Hilfe zu Gerätetyp und MP/FP-Index beschrieben.
