# MQTT und Home Assistant

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/mqtt-home-assistant.md)

Optional und standardmäßig **aus**. In der Weboberfläche gibt es die Karte *MQTT / Home Assistant* mit den Schaltern *MQTT aktiv* und *Home-Assistant-Discovery senden*, Broker-Adresse, Zugangsdaten und Topic-Präfix (Standard `waterrower`). Aktiviert veröffentlicht der ESP Discovery-Nachrichten unter `homeassistant/…`; das Gerät „WaterRower“ erscheint in Home Assistant von selbst.

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

## Topics

Alle unter dem Präfix (Standard `waterrower/`):

| Topic | Inhalt |
|---|---|
| `distance` | Distanz in m |
| `speed` | Geschwindigkeit in m/s |
| `stroke_rate` | Schlagfrequenz |
| `strokes` | Schlagzahl |
| `ratio` | Verhältnis Zug/Erholung |
| `duration` | Dauer in s |
| `duration_formatted` | Dauer als `hh:mm:ss` |
| `watts` | Watt |
| `total_distance` | Gesamtdistanz in m |
| `split_500m` | 500-m-Zeit als `m:ss` |
| `session_active` | `ON` / `OFF` |
| `session_id` | ID der laufenden Einheit |
| `live` | alle Werte als JSON, 1×/s während einer Einheit |
| `session/last` | Zusammenfassung der letzten Einheit (retained) |
| `s4_connected` | `ON` / `OFF`: S4 ist an und per USB erkannt (retained) |
| `usb_mode` | `ON` / `OFF`: S4 im USB-Modus und sendet (retained) |
| `firmware_version` | Firmware-Version (retained) |
| `cmd/end_session` | **an** den ESP: laufende Einheit beenden; Payload `reset` setzt zusätzlich den Monitor zurück |
| `cmd/session_timeout` | **an** den ESP: Sitzungs-Timeout in Minuten (1–60) |
| `status` | `online` / `offline` (Last Will) |

Die Topics entsprechen der früheren ESPHome-Variante. Eine Ausnahme: `split_500m_s` (500-m-Zeit in Sekunden) gibt es nicht mehr.

## Home-Assistant-Dashboard

[`homeassistant/dashboard.yaml`](../homeassistant/dashboard.yaml) ist ein fertiges Dashboard: Live-Kacheln, Einheiten-Zusammenfassung, 24-h-Verlauf und Gerätesteuerung (Sitzungs-Timeout, Beenden-und-zurücksetzen, Versionen).

Import: *Einstellungen → Dashboards → Dashboard hinzufügen → Neues Dashboard von Grund auf*, öffnen, Menü (drei Punkte) → *Bearbeiten* → Menü → *Raw-Konfigurationseditor*, Datei einfügen. Die Entitäts-IDs gehen davon aus, dass das Gerät in Home Assistant „WaterRower“ heißt (`sensor.waterrower_…`). Hat Home Assistant ein anderes Präfix vergeben, vorher `waterrower_` in der Datei ersetzen.

## Stand

Die Discovery-Nachrichten sind auf gültiges JSON getestet (`firmware/test/host/mqtt_test.c`), aber nicht gegen einen echten Broker oder Home Assistant.
