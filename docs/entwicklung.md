# Entwicklung

🇩🇪 Deutsch · [🇬🇧 English](en/entwicklung.md)

## Bauen

```bash
pip install platformio
cd firmware
pio run                 # ESP-IDF 6.1, Ziel esp32s3
pio run -t upload
pio device monitor
```

Die Weboberfläche (`firmware/web/`) wird von `gen_assets.py` gzip-komprimiert in die Firmware eingebettet; Änderungen an HTML/CSS/JS brauchen also einen neuen Build.

Partitionen (16 MB): 2× 3 MB App (OTA), ~10 MB LittleFS für Einheiten (`firmware/partitions.csv`).

## Tests

```bash
sh firmware/test/host/run.sh    # Speicher, Eingabeprüfung, MQTT-Discovery, ohne Hardware
```

Läuft auch in GitHub Actions (`.github/workflows/firmware.yml`), danach wird gebaut.

## Aufbau

| Pfad | Inhalt |
|---|---|
| `firmware/src/rower.c` | S4-USB-Link, Protokoll-Parser, Sitzungslogik |
| `firmware/src/store.c` | Sitzungsspeicher (LittleFS), Zusammenfassungen, Wiederherstellung |
| `firmware/src/web.c` | HTTP-API, Live-Stream (SSE), OTA |
| `firmware/src/live.c` | Live-Werte und Verteilung an Clients |
| `firmware/src/wifi_mgr.c` | WLAN, Fallback-Hotspot, Captive-DNS, mDNS, NTP |
| `firmware/src/mqtt.c` | MQTT und Home-Assistant-Discovery |
| `firmware/src/uplink.c` | WebSocket-Client zur Arena inkl. Nachsenden |
| `firmware/src/settings.c` | Einstellungen im NVS |
| `firmware/web/` | Oberfläche |
| `firmware/test/host/` | PC-Tests |
| `firmware/test/ui/` | Mock-Server und Screenshot-Skript |
| `firmware/release/` | Komplett-Image für das erste Flashen |

## Screenshots neu erzeugen

`firmware/test/ui/mock_server.py` liefert die API mit erzeugten Beispieldaten, `screenshots.py` fotografiert die Oberfläche mit Chrome und Playwright:

```bash
pip install playwright pillow
python3 firmware/test/ui/screenshots.py --out docs/screenshots --chrome /usr/bin/google-chrome
```

Mock-Server allein: `python3 firmware/test/ui/mock_server.py --state live` (Port 8099).

## Release-Image

```bash
cd firmware && pio run
B=.pio/build/esp32s3
~/.platformio/packages/tool-esptool/esptool --chip esp32s3 merge_bin -o release/waterrower-esp-3.0.0-merged.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xf000 $B/ota_data_initial.bin 0x20000 $B/firmware.bin
(cd release && sha256sum *.bin > SHA256SUMS)
```
