# Prebuilt flash image / Fertiges Flash-Image

`waterrower-esp-3.0.0-merged.bin` contains bootloader, partition table, OTA data and app
(ESP32-S3, 16 MB flash) and is flashed to **address `0x0`**.
Step by step with the browser flasher: [English](../../docs_en/flashing.md) · [Deutsch](../../docs_de/flashen.md)

Quick command line: `esptool --chip esp32s3 write_flash 0x0 waterrower-esp-3.0.0-merged.bin`

Later updates go through the web UI (*Firmware update* card) with `waterrower-esp-3.0.0-ota.bin` (the app only,
settings are kept), **not** with this image. Checksum: see `SHA256SUMS`. The image is a build of the respective commit,
not a signed release.

---

`waterrower-esp-3.0.0-merged.bin` enthält Bootloader, Partitionstabelle, OTA-Daten und App
(ESP32-S3, 16 MB Flash) und wird an **Adresse `0x0`** geflasht.
Schritt für Schritt mit dem Browser-Flasher: [Deutsch](../../docs_de/flashen.md) · [English](../../docs_en/flashing.md)

Kurz per Kommandozeile: `esptool --chip esp32s3 write_flash 0x0 waterrower-esp-3.0.0-merged.bin`

Spätere Updates laufen über die Weboberfläche (Karte *Firmware-Update*) mit `waterrower-esp-3.0.0-ota.bin` (nur die App,
Einstellungen bleiben erhalten), **nicht** mit diesem Image. Prüfsumme: siehe `SHA256SUMS`. Das Image ist ein Build aus dem jeweiligen Commit,
kein signiertes Release.
