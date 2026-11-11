# Development

[🇩🇪 Deutsch](../docs_de/entwicklung.md) · 🇬🇧 English

## Building

```bash
pip install platformio
cd firmware
pio run                 # ESP-IDF 6.1, target esp32s3
pio run -t upload
pio device monitor
```

The web UI (`firmware/web/`) is gzip-compressed and embedded into the firmware by `gen_assets.py`; changes to HTML/CSS/JS therefore need a new build.

Partitions (16 MB): 2× 3 MB app (OTA), ~10 MB LittleFS for workouts (`firmware/partitions.csv`).

## Tests

```bash
sh firmware/test/host/run.sh    # storage, input validation, MQTT discovery; no hardware
```

Also runs in GitHub Actions (`.github/workflows/firmware.yml`), followed by the build.

## History

The earlier variant (ESPHome firmware plus a Docker tracker, MQTT/Home Assistant as the only way in) is no longer in the current tree, but nothing is lost: it stays in this repository. The branch `legacy` holds the last state of the old repository, and the full history is part of `main`.

```bash
git checkout legacy     # then see esphome/waterrower.yaml, docker/, docs/
```

Old releases (check out the commit, e.g. `git checkout <commit>`):

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

The old arena development branch ends at commit `768664a`. The arena server's history before it moved lives in the history of [waterrower-arena](https://github.com/MojoOne1/waterrower-arena).

## Code layout

The S4's protocol and the registers used: [S4 protocol](s4-protocol.md).

| Path | Content |
|---|---|
| `firmware/src/rower.c` | S4 USB link, protocol parser, session logic |
| `firmware/src/store.c` | Workout storage (LittleFS), summaries, recovery |
| `firmware/src/web.c` | HTTP API, live stream (SSE), OTA |
| `firmware/src/live.c` | Live values and distribution to clients |
| `firmware/src/wifi_mgr.c` | Wi-Fi, fallback hotspot, captive DNS, mDNS, NTP |
| `firmware/src/mqtt.c` | MQTT and Home Assistant discovery |
| `firmware/src/uplink.c` | WebSocket client to the arena incl. re-sending |
| `firmware/src/settings.c` | Settings in NVS |
| `firmware/web/` | Web UI |
| `firmware/test/host/` | PC tests |
| `firmware/test/ui/` | Mock server and screenshot script |
| `firmware/release/` | Complete image for the first flash |

## Regenerating screenshots

`firmware/test/ui/mock_server.py` serves the API with generated sample data; `screenshots.py` photographs the UI with Chrome and Playwright:

```bash
pip install playwright pillow
python3 firmware/test/ui/screenshots.py --out screenshots --chrome /usr/bin/google-chrome
```

Mock server alone: `python3 firmware/test/ui/mock_server.py --state live` (port 8099).

## Release image

```bash
cd firmware && pio run
B=.pio/build/esp32s3
~/.platformio/packages/tool-esptool/esptool --chip esp32s3 merge_bin -o release/waterrower-esp-3.0.0-merged.bin \
  --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 $B/bootloader.bin 0x8000 $B/partitions.bin 0xf000 $B/ota_data_initial.bin 0x20000 $B/firmware.bin
(cd release && sha256sum *.bin > SHA256SUMS)
```
