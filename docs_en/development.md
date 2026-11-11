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
