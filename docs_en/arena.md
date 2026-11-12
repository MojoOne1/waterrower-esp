# Arena (multiplayer)

[🇩🇪 Deutsch](../docs_de/arena.md) · 🇬🇧 English

The ESP can send workouts live to an arena server to row together. The server has its own repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena) (Docker; operation, protocol and API are documented there). Only whoever **runs** an arena needs Docker.

## Connecting

1. The operator creates an athlete and generates a token under *Account → Tracker connection*.
2. In the ESP settings open the *Arena – rowing together* card: enter **server** (e.g. `https://arena.example.com`) and **token**, tick *Send data to the arena* and save.
3. The chip at the top shows "Arena: <name>" once the connection is up.

The ESP opens the connection itself (`wss://`); no port forwarding to the ESP is needed. Missed workouts are re-sent later, race status is shown live. The token is never displayed and is only sent to the address it was entered for.

## If the ESP does not connect

The status next to the *Arena* card (and the serial console, 115200 baud) tells you why:

| Message | Meaning and fix |
|---|---|
| `waiting for time sync (NTP)` | The device has no clock yet. TLS needs it to check the certificate. Allow UDP 123 (NTP) and DNS for the ESP's network; the footer of the web UI shows the device time. |
| `connection failed (TLS/DNS/firewall)` | Nothing reached the arena. Check that the ESP's network may resolve the name and reach TCP 443 on the internet, and that no firewall inspects or replaces the TLS certificate. The serial console names the exact step. |
| `closed by arena (4403)` | The token is unknown: wrong, deleted or copied incompletely. Create a new one. |
| `closed by arena (4429)` | Too many failed attempts from this address; the arena blocks it for 15 minutes (a restart of the arena lifts it). |

Nothing in the arena's log (`/ws/uplink`) means the request never got there: the problem lies between ESP and Cloudflare, not at the token. The *Send data to the arena* box must be ticked; without it the ESP does not try.

Firmware 3.0.0 accepts the cross-signed certificate chains Cloudflare sends. An older image reports `No matching trusted root certificate found`: update it with `waterrower-esp-3.0.0-ota.bin` (web UI, *Firmware update*).

Protocol: [protocol.md](https://github.com/MojoOne1/waterrower-arena/blob/main/docs_en/protocol.md); firmware side: `firmware/src/uplink.c`.
