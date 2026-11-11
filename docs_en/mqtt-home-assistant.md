# MQTT and Home Assistant

[🇩🇪 Deutsch](../docs_de/mqtt-home-assistant.md) · 🇬🇧 English

Optional and **off** by default. The web UI has an *MQTT / Home Assistant* card with the switches *MQTT enabled* and *Send Home Assistant discovery*, broker address, credentials and topic prefix (default `waterrower`). When enabled, the ESP publishes discovery messages under `homeassistant/…`; the device "WaterRower" shows up in Home Assistant on its own.

Last will: `<prefix>/status` reports `offline` when the ESP disappears; all entities then become unavailable. `mqtts://` verifies against public certificate authorities; self-signed certificates are not accepted.

## Entities (23)

| Type | Entity |
|---|---|
| Sensor | `distance`, `speed`, `split_500m`, `stroke_rate`, `strokes`, `ratio`, `pull_time`, `duration`, `duration_formatted`, `watts`, `total_distance`, `session_avg_speed`, `session_avg_stroke_rate`, `session_id`, `last_session` |
| Binary sensor | `session_active`, `s4_connected`, `usb_mode` |
| Diagnostic | `s4_status`, `firmware_version` |
| Number | `session_timeout` (1–60 min, command `<prefix>/cmd/session_timeout`) |
| Button | *Reset* and *End Session* (command `<prefix>/cmd/end_session`, payload `reset` or `end`) |

`last_session` is published on `<prefix>/session/last`, live values on `<prefix>/live` and the single topics (`waterrower/distance` etc.).

The topics match the earlier ESPHome variant. There is no ready-made dashboard; build one from the entities.

## Status

The discovery messages are tested for valid JSON (`firmware/test/host/mqtt_test.c`), but not against a real broker or Home Assistant.
