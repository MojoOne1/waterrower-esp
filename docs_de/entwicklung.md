# Entwicklung

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/development.md)

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

## Historie

Die frühere Variante (ESPHome-Firmware plus Docker-Tracker, MQTT/Home Assistant als einziger Weg) ist nicht mehr im aktuellen Stand, aber nichts ist verloren: Sie bleibt in diesem Repository. Der Branch `legacy` enthält den letzten Stand des alten Repositorys, die komplette Historie ist Teil von `main`.

```bash
git checkout legacy     # dann esphome/waterrower.yaml, docker/, docs/ ansehen
```

Alte Releases (Commit auschecken, z. B. `git checkout <commit>`):

| Release | Commit |
|---|---|
| v1.0.0 | `cba4121` |
| v1.1.0 | `5ddcb9f` |
| v1.2.0 | `4a3f2d3` |
| v1.2.1 | `565b1a9` |
| v1.3.0 | `b3a8def` |
| v1.4.0 | `e148ae1` |
| v1.5.0 | `d29b220` |
| v1.6.0 | `6c31b3c` |
| v1.6.1 | `a07cffe` |
| v2.0.0 | `0b97434` |

Der alte Entwicklungszweig der Arena endet bei Commit `768664a`. Die Historie des Arena-Servers vor dem Umzug liegt in der Historie von [waterrower-arena](https://github.com/MojoOne1/waterrower-arena).

## Aufbau

Das Protokoll der S4 und die verwendeten Register: [S4-Protokoll](s4-protokoll.md).

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
python3 firmware/test/ui/screenshots.py --out screenshots --chrome /usr/bin/google-chrome
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
