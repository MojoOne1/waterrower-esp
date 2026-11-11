# Flashen mit dem Browser-Flasher

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/flashen.md)

**Flashen** heißt: die Firmware (das Programm) in den Speicher des ESP32 schreiben. Das ist nur beim ersten Mal nötig, spätere Updates gehen über die Weboberfläche. Du brauchst dafür nichts zu installieren: Ein **Browser-Flasher** ist eine Webseite, die über den USB-Anschluss direkt mit dem ESP spricht.

## Was du brauchst

- Einen **Computer** (Windows, macOS oder Linux) mit **Chrome, Edge oder Opera**. Firefox und Safari können das nicht, Handys ebenfalls nicht (Android nur mit Zusatzaufwand).
- Ein **USB-Datenkabel** (USB-C). Reine Ladekabel haben keine Datenleitungen und führen dazu, dass im Browser kein Anschluss auftaucht.
- Das **ESP32-S3-DevKitC-1**. Es hat zwei USB-C-Buchsen: eine beschriftet **COM** (oder UART), eine beschriftet **USB**. **Zum Flashen immer COM verwenden.**
- Die **Image-Datei** `waterrower-esp-3.0.0-merged.bin` aus [`firmware/release/`](../firmware/release/). Auf GitHub die Datei öffnen und oben rechts auf das Download-Symbol („Download raw file“) klicken.

## Welche Datei gehört wohin?

| Datei | Wofür | Wo |
|---|---|---|
| `waterrower-esp-3.0.0-merged.bin` | **Erstes Flashen** (enthält alles) | Browser-Flasher, Adresse **`0x0`** |
| `firmware.bin` (aus `pio run`) | **Späteres Update** | Weboberfläche, Karte *Firmware-Update* |

Verwechsle die beiden nicht: `firmware.bin` an Adresse `0x0` flashen führt dazu, dass das Gerät nicht startet. Das Image fängt bei `0x0` an, weil es auch Bootloader und Partitionstabelle enthält, die dort liegen müssen.

## Schritt für Schritt

Wir verwenden den Flasher von Espressif (dem Hersteller des Chips): <https://espressif.github.io/esptool-js/>. Die Beschriftungen können sich mit der Zeit leicht ändern, die Reihenfolge bleibt gleich.

1. **ESP anschließen.** Kabel in die Buchse **COM**, anderes Ende an den Computer. Kein anderes Programm darf den Anschluss nutzen (Serieller Monitor, Arduino IDE, PlatformIO schließen).
2. **Seite öffnen** und **Connect** klicken. Es erscheint ein Auswahlfenster mit den seriellen Anschlüssen (Namen wie „USB Serial“, „CP210x“ oder „COM5“). Den passenden wählen und *Verbinden*. Taucht keiner auf, siehe unten.
3. Im Textfeld der Seite steht jetzt etwas wie **„Chip is ESP32-S3“**. Dann ist alles richtig verbunden.
4. **Beim ersten Mal empfohlen:** *Erase Flash* klicken und bestätigen. Das löscht alte Reste (z. B. von einer anderen Firmware). Dauert etwa eine halbe Minute.
5. Bei **Flash Address** `0x0` eintragen, bei **Choose File** das `merged.bin` auswählen.
6. **Program** klicken. Ein Balken läuft bis 100 %. Nicht abstecken, bis die Seite „fertig“ meldet (ca. 1–2 Minuten).
7. Auf **Disconnect** klicken, dann am Board die Taste **RESET** (oder EN) drücken, oder das Kabel kurz abziehen und wieder anstecken.
8. Nach etwa 20–30 Sekunden erscheint das WLAN **`WaterRower-XXXX`**. Das heißt: erfolgreich. Weiter mit der [Einrichtung](einrichtung.md#wlan-einrichten).

Der erste Start dauert etwas länger, weil der ESP seinen Speicher für die Einheiten anlegt.

## Wenn etwas nicht klappt

| Problem | Lösung |
|---|---|
| Im Auswahlfenster ist kein Anschluss | Anderes Kabel probieren (Datenkabel!), anderen USB-Port am Computer, Stecker in **COM** statt **USB**. Windows: ggf. den Treiber „CP210x“ von Silicon Labs installieren. Linux: Benutzer zur Gruppe `dialout` hinzufügen und neu anmelden. |
| „Failed to connect“ / „Wrong boot mode“ | Die Taste **BOOT** gedrückt halten, kurz **RESET** drücken, BOOT loslassen, dann erneut *Connect*. |
| Abbruch mitten im Schreiben | Bei *Baudrate* einen kleineren Wert wählen (z. B. 115200) und neu verbinden. |
| Anschluss „belegt“ | Andere Programme und Browser-Tabs schließen, die den Anschluss nutzen, ESP kurz abstecken. |
| Nach dem Flashen kein WLAN `WaterRower-XXXX` | Wurde `0x0` eingetragen und das `merged.bin` gewählt? Sonst *Erase Flash* und neu flashen. |

## Alternative: Kommandozeile

Wer `esptool` installiert hat (`pip install esptool`):

```bash
esptool --chip esp32s3 --port COM5 write_flash 0x0 waterrower-esp-3.0.0-merged.bin
```

Unter Linux/macOS heißt der Port z. B. `/dev/ttyUSB0` oder `/dev/cu.usbserial-…`.

## Prüfsumme (optional)

Zur Kontrolle, dass die Datei vollständig heruntergeladen wurde:

```bash
sha256sum waterrower-esp-3.0.0-merged.bin   # Windows: certutil -hashfile <datei> SHA256
```

Der Wert muss mit dem in `SHA256SUMS` übereinstimmen.
