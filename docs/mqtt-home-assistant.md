# MQTT und Home Assistant

🇩🇪 Deutsch · [🇬🇧 English](en/mqtt-home-assistant.md)

Optional und standardmäßig **aus**. In der Weboberfläche gibt es eine Karte *MQTT* mit Schalter, Broker-Adresse, Zugangsdaten und Topic-Präfix (Standard `waterrower`). Aktiviert veröffentlicht der ESP Discovery-Nachrichten unter `homeassistant/…`; das Gerät „WaterRower“ erscheint in Home Assistant von selbst.

Last Will: `<präfix>/status` meldet `offline`, wenn der ESP wegfällt; alle Entitäten werden dann „nicht verfügbar“. `mqtts://` prüft gegen öffentliche Zertifizierungsstellen, selbstsignierte Zertifikate werden nicht akzeptiert.

## Entitäten (23)

| Typ | Entität |
|---|---|
| Sensor | `distance`, `speed`, `split_500m`, `stroke_rate`, `strokes`, `ratio`, `pull_time`, `duration`, `duration_formatted`, `watts`, `total_distance`, `session_avg_speed`, `session_avg_stroke_rate`, `session_id`, `last_session` |
| Binärsensor | `session_active`, `s4_connected`, `usb_mode` |
| Diagnose | `s4_status`, `firmware_version` |
| Zahl | `session_timeout` (1–60 Min., Befehl `<präfix>/cmd/session_timeout`) |
| Button | *Reset* und *End Session* (Befehl `<präfix>/cmd/end_session`, Payload `reset` bzw. `end`) |

`last_session` kommt auf `<präfix>/session/last`, Live-Werte auf `<präfix>/live` und den Einzel-Topics (`waterrower/distance` usw.).

Die Topics entsprechen der früheren ESPHome-Variante. Ein fertiges Dashboard gibt es nicht; das baut man sich aus den Entitäten.

## Stand

Die Discovery-Nachrichten sind auf gültiges JSON getestet (`firmware/test/host/mqtt_test.c`), aber nicht gegen einen echten Broker oder Home Assistant.
