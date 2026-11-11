# Security

[🇩🇪 Deutsch](../docs_de/sicherheit.md) · 🇬🇧 English

The ESP sits on the home network without TLS. What the firmware does about it, and what it does not.

## Protection

- **Device password (optional, recommended):** set under *Wi-Fi & settings*. After that every changing request (settings, delete, end workout, firmware update) needs the password; the browser asks once (any user name). Only a salted hash is stored. Forgotten: reflash with *Erase Flash* (see [Flashing](flashing.md)); settings and workouts are lost.
- **Hotspot password:** also changeable there. The default `waterrower` is public knowledge; change it if strangers are within range.
- **Other websites:** requests are only accepted when the ESP is addressed by IP or a local name (`waterrower.local`, `waterrower.lan` …) (DNS rebinding) and changing requests come from its own page (CSRF). Behind another name (e.g. `waterrower.fritz.box`) the API answers 403; use the IP instead.
- **Arena token:** never output, only sent to the address it was entered for, and must be re-entered when the address changes.
- **Firmware update:** only images of this project, not during a recording; an image that does not boot cleanly is rolled back on the next boot.

## Deliberately open

- Read access (live values, workouts, export) is possible on the Wi-Fi without a password.
- No TLS on the device: device password and Wi-Fi input travel in clear text on the local network.
- Wi-Fi, MQTT and arena credentials are stored unencrypted in flash (no flash encryption, no secure boot); whoever has the device can read them and flash arbitrary firmware.
- MQTT without `mqtts://` sends credentials in clear text.
