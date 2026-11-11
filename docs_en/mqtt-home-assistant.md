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

## Topics

All under the prefix (default `waterrower/`):

| Topic | Content |
|---|---|
| `distance` | distance in m |
| `speed` | speed in m/s |
| `stroke_rate` | stroke rate |
| `strokes` | stroke count |
| `ratio` | pull/recovery ratio |
| `duration` | duration in s |
| `duration_formatted` | duration as `hh:mm:ss` |
| `watts` | watts |
| `total_distance` | lifetime distance in m |
| `split_500m` | 500 m split as `m:ss` |
| `session_active` | `ON` / `OFF` |
| `session_id` | ID of the running workout |
| `live` | all values as JSON, 1×/s during a workout |
| `session/last` | summary of the last workout (retained) |
| `s4_connected` | `ON` / `OFF`: the S4 is on and enumerated on USB (retained) |
| `usb_mode` | `ON` / `OFF`: the S4 is in USB mode and streaming (retained) |
| `firmware_version` | firmware version (retained) |
| `cmd/end_session` | **to** the ESP: end the running workout; payload `reset` also resets the monitor |
| `cmd/session_timeout` | **to** the ESP: session timeout in minutes (1–60) |
| `status` | `online` / `offline` (last will) |

The topics match the earlier ESPHome variant. One exception: `split_500m_s` (500 m split in seconds) no longer exists.

## Home Assistant dashboard

[`homeassistant/dashboard.yaml`](../homeassistant/dashboard.yaml) is a ready-made dashboard: live tiles, workout summary, a 24 h history graph and device controls (session timeout, end-and-reset button, versions).

Import: *Settings → Dashboards → Add dashboard → New dashboard from scratch*, open it, three-dot menu → *Edit* → menu → *Raw configuration editor*, paste the file. The entity IDs assume the device is called "WaterRower" in Home Assistant (`sensor.waterrower_…`). If Home Assistant gave it another prefix, replace `waterrower_` in the file first.

## Status

The discovery messages are tested for valid JSON (`firmware/test/host/mqtt_test.c`), but not against a real broker or Home Assistant.
