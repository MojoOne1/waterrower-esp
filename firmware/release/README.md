# Fertiges Flash-Image

`waterrower-esp-3.0.0-merged.bin` enthält Bootloader, Partitionstabelle, OTA-Daten und App
(ESP32-S3, 16 MB Flash). Es wird an **Adresse 0x0** geflasht, z. B.:

- Browser (Chrome/Edge): https://web.esphome.io oder https://espressif.github.io/esptool-js/ (Offset `0x0`)
- Kommandozeile: `esptool --chip esp32s3 write_flash 0x0 waterrower-esp-3.0.0-merged.bin`

Das Image löscht Einstellungen und gespeicherte Einheiten nicht gezielt, legt aber beim ersten Start
die LittleFS-Partition selbst an. Spätere Updates gehen über die Weboberfläche (OTA) mit `firmware.bin`
aus `pio run`, nicht mit diesem Image.

Prüfsumme: siehe `SHA256SUMS`. Das Image ist ein Build aus dem jeweiligen Commit, kein signiertes Release.
