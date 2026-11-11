# S4 protocol

[🇩🇪 Deutsch](../docs_de/s4-protokoll.md) · 🇬🇧 English

Based on WaterRower's document **"Water Rower S4 & S5 USB Protocol, Issue 1.04"**. It is not included here because its redistribution terms are unclear, but searching for the exact title finds it quickly. Summary:

- Serial CDC connection, 19200 baud, ASCII, lines end with `\r\n`
- `USB` opens the connection, response `_WR_`
- `IRS xxx` / `IRD xxx` / `IRT xxx` read 1 / 2 / 3 bytes starting at address `xxx`
- Responses `IDS` / `IDD` / `IDT` return the bytes **high byte first**
- `SS` / `SE` = stroke start / end, `Pxx` = paddle pulses per 25 ms, `PING` every second while idle
- Send at most one packet per 25 ms

The implementation is in `firmware/src/rower.c`.

## Addresses used

| Address | Register per spec | Meaning | Status |
|---|---|---|---|
| `057`/`058` | distance_low/hi | displayed distance (m) | confirmed |
| `14A`/`14B` | m_s_low/hi_average | speed (cm/s), matches the display | confirmed |
| `1A9` | zone_sr_val | stroke rate | confirmed |
| `1E1`–`1E3` | display_sec/min/hr | time, **BCD-encoded** | confirmed |
| `140`/`141` | strokes_cnt_low/hi | stroke count | plausible |
| `142`/`143` | stroke_average / stroke_pull | stroke timings in 25 ms steps | unverified |
| `081`/`082` | total_dis_low/hi | lifetime distance | unverified |
| `088`/`089` | kcal_watts_low/hi | watts | **scaling unverified** |
| `1A0` | zone_hr_val | heart rate (chest strap only) | not used |

The status comes from the earlier ESPHome variant, which was checked on real hardware. The new firmware reads the same registers but has not itself been tried on an S4 yet.

Pitfalls that cost time:

- `148`/`149` (`m_s_*_total`) does not return the display value, `14A`/`14B` does.
- `14C` is a counter, not a speed.
- The time registers are BCD: `0x13` means 13, not 19.

## What the firmware does

- When the S4 appears the ESP sends `USB` (without it the S4 stays silent) and repeats the handshake if packets stop for 10 s. "Ergometer connected" and `usb_mode` report the link state.
- During a workout it polls every second. While idle it sends nothing, because every packet resets the S4's auto power-off timer. (On USB power the S4 never switches itself off anyway, see [Setup](setup.md#help).)
- Workout start is detected on the first stroke. It ends via the button (*End session*, MQTT `cmd/end_session`) or after the session timeout without stroke or paddle packets (default 5 minutes; the speed register is not suitable for this, it keeps its last value after stopping).
- Speed, stroke rate and watts count as 0 as soon as the S4 reports no rowing (`PING`); the monitor itself keeps displaying the last stroke's values.
