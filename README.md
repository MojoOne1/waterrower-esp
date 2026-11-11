# WaterRower ESP

🇬🇧 English · 🇩🇪 Die Dokumentation gibt es auch auf Deutsch: [docs_de/](docs_de/)

Firmware for an **ESP32-S3** that sits between the WaterRower S4 and your Wi-Fi: it reads the rowing machine over USB, records every workout to its own flash and serves the web UI itself. No server, no container, no cloud account.

![Live view](screenshots/live.png)

```
WaterRower S4 ──USB──► ESP32-S3 ──Wi-Fi──► browser (phone/PC)  http://waterrower.local
                        │  recording (flash), charts, CSV/Excel
                        ├── MQTT / Home Assistant (optional)
                        └── arena uplink (optional)
```

> The screenshots come from a mock server with **generated sample data**, not from a real workout. The UI is shown in German; it can be switched to English with the DE/EN toggle.

## Features

- **Live view** with four monitor-style tiles (stroke rate, speed, duration, distance) and the 500 m split
- **Recording to internal flash** (~10 MB, oldest workouts are deleted when space runs out), recovery after power loss
- **History with charts and comparison** of workouts, export as **CSV** and **Excel** (generated in the browser)
- **Wi-Fi setup without reflashing:** without credentials the ESP opens a hotspot with a setup page
- Light/dark theme, German/English, phone-friendly
- Firmware update from the web UI (OTA)
- **Optional:** MQTT with Home Assistant discovery (plus a ready-made dashboard), arena uplink for rowing together

## Quick start

1. Flash the prebuilt image from [`firmware/release/`](firmware/release/) to address `0x0` with a browser flasher, step by step in [Flashing](docs_en/flashing.md) (Chrome or Edge, nothing to install).
2. Connect to the hotspot `WaterRower-XXXX` (password `waterrower`) and pick your Wi-Fi.
3. Open `http://waterrower.local`.

Beginner-friendly walkthrough, including cabling: [Setup](docs_en/setup.md).

## Documentation

Everything is available in English (`docs_en/`) and German (`docs_de/`).

| Topic | English | Deutsch |
|---|---|---|
| **Start here:** hardware, Wi-Fi, first workout | [setup.md](docs_en/setup.md) | [einrichtung.md](docs_de/einrichtung.md) |
| **Flashing with the browser flasher** | [flashing.md](docs_en/flashing.md) | [flashen.md](docs_de/flashen.md) |
| Using the web UI | [usage.md](docs_en/usage.md) | [bedienung.md](docs_de/bedienung.md) |
| S4 protocol and register addresses | [s4-protocol.md](docs_en/s4-protocol.md) | [s4-protokoll.md](docs_de/s4-protokoll.md) |
| MQTT and Home Assistant | [mqtt-home-assistant.md](docs_en/mqtt-home-assistant.md) | [mqtt-home-assistant.md](docs_de/mqtt-home-assistant.md) |
| Arena (multiplayer) | [arena.md](docs_en/arena.md) | [arena.md](docs_de/arena.md) |
| Security | [security.md](docs_en/security.md) | [sicherheit.md](docs_de/sicherheit.md) |
| Building, tests, code layout | [development.md](docs_en/development.md) | [entwicklung.md](docs_de/entwicklung.md) |
| Printable case | [case/](case/) | [case/](case/) |

## Status and limits

- The firmware **compiles** (ESP-IDF 6.1 via PlatformIO) and the storage logic is tested on a PC. **Not yet tested on real hardware:** the S4 USB link, captive portal, TLS to the arena and OTA should be verified one by one first.
- MQTT discovery is checked for valid JSON, but not against a real broker or Home Assistant. A ready-made Home Assistant dashboard is in [`homeassistant/`](homeassistant/dashboard.yaml).
- The watts and ratio registers of the S4 are not verified against the display (see [S4 protocol](docs_en/s4-protocol.md)).
- The Excel export contains tables but no chart.
- At most 4 simultaneous live views.

The arena server lives in its own repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena).
