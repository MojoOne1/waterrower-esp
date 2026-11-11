# Setup

[🇩🇪 Deutsch](../docs_de/einrichtung.md) · 🇬🇧 English

From a bare board to your first workout. No prior knowledge needed.

## Overview

1. [Get and connect the hardware](#hardware)
2. [Flash the firmware](flashing.md) (once, on a computer)
3. [Set up Wi-Fi](#wi-fi-setup) (on your phone)
4. [Open the page and row](#open-the-page)

## Hardware

| Part | Note |
|---|---|
| ESP32-S3-DevKitC-1 (N16R8) | 16 MB flash, 8 MB PSRAM (other memory variants are not enough). Two USB-C sockets: **COM** (UART, flashing and power) and **USB** (native OTG port). Used: [ESP32-S3-WROOM-1 N16R8 DevKitC-1 (amazon.de)](https://www.amazon.de/dp/B0FYFF8CB2/); any DevKitC-1 clone with two USB-C sockets and the USB-OTG solder bridge should do |
| WaterRower S4 Performance Monitor | battery powered, Mini-USB socket |
| USB-C to Mini-USB adapter | between a USB-C cable and the S4's Mini-USB socket. Used: [4x USB-C female to Mini-USB male (amazon.de)](https://www.amazon.de/dp/B0G4BYT22N); it replaces the S4's own cable |
| USB-C cable | from the ESP's **USB** port to that adapter |
| USB power supply (5 V) | on the **COM** port, for permanent use |
| USB-C **data cable** | for flashing from a computer (a charge-only cable is not enough) |

Optionally a [printable case](../case/) for the back of the S4, if you like with a QR code to this repository in the lid (`esp32case_lid_qr.stl`).

### The USB-OTG solder bridge

On the underside of the DevKitC-1 clone there is a solder bridge labelled **USB-OTG**. It is **open** from the factory and must be **closed**. It ties the VBUS lines of both USB-C sockets together: only then does the **USB** port carry the 5 V the S4 needs to detect a host at all. The S4 draws practically no power through it; it runs from its own batteries.

**Important:** once the bridge is closed, never connect both sockets to separate power sources at the same time (so no power supply on the **USB** port while flashing from a computer).

### Wiring

```
USB power supply ──► [COM]  ESP32-S3  [USB] ──USB-C──► C-to-Mini adapter ──► S4
                                         ▲
                             (USB-OTG solder bridge closed)
```

A USB-C to USB-A adapter plus the S4's original cable works just as well; the Mini-USB adapter just saves a link.

## Flashing

In detail with step-by-step instructions and troubleshooting: **[Flashing with the browser flasher](flashing.md)**. In short: write [`firmware/release/waterrower-esp-3.0.0-merged.bin`](../firmware/release/) to address **`0x0`**.

To build it yourself: [Development](development.md).

## Wi-Fi setup

Without stored credentials the ESP opens the hotspot **`WaterRower-XXXX`** (password `waterrower`) immediately. If it knows a network that is unreachable, it does so after about 20 seconds.

![Setup portal](../screenshots/setup-portal.png)

1. On your phone, open the Wi-Fi settings and join **`WaterRower-XXXX`**, password `waterrower`. The phone warns "no internet": **stay connected**. Briefly turn off mobile data if the phone keeps switching back.
2. The setup page opens by itself. Otherwise enter `http://192.168.4.1/setup` in the browser.
3. **Scan networks**, tap your network, enter the password, **Connect**.
4. The ESP connects and the hotspot disappears. Reconnect your phone to your home Wi-Fi.

## Open the page

On the home Wi-Fi enter **`http://waterrower.local`** in the browser (`http://`, not `https://`).

If nothing opens (some Android devices do not know `.local`): find **"waterrower"** in your router's device list and enter its IP address, e.g. `http://192.168.1.57`. Another name such as `waterrower.fritz.box` is refused for security reasons; use the IP then.

It works without Wi-Fi too: join the hotspot and open `http://192.168.4.1`.

The chip at the top shows **"Ergometer connected"** as soon as the S4 is attached to the ESP. The first stroke starts a workout. Everything else: [Usage](usage.md).

## Settings

![Settings](../screenshots/settings.png)

Card **WiFi & settings** (further down the page):

| Field | Meaning |
|---|---|
| WiFi name / password | Home network; an empty password field keeps the stored one. *Scan networks* lists networks in range. |
| Time zone | POSIX string, default `CET-1CEST,M3.5.0,M10.5.0/3` (Central Europe with daylight saving) |
| Session timeout | Minutes without a stroke until a workout ends automatically (1–60, default 5) |
| Hotspot password | replaces `waterrower` (at least 8 characters), see [Security](security.md) |
| Device password | protects changes, see [Security](security.md) |

Below: cards for [Arena](arena.md), [MQTT / Home Assistant](mqtt-home-assistant.md) and *Firmware update*. The screenshots show the German UI; the DE/EN toggle at the top switches it.

## Moving house or new Wi-Fi

Plug the ESP in. If it cannot find the old network it opens the hotspot after about 20 s; enter the new Wi-Fi there.

## Clock

Via NTP as soon as internet is available. Otherwise the ESP takes the time from the browser that opens the page.

## Help

| Problem | Fix |
|---|---|
| Hotspot does not appear | Wait 30 s, unplug and replug the ESP. Further: [Flashing, troubleshooting](flashing.md#if-something-goes-wrong) |
| Setup page does not open | Enter `http://192.168.4.1/setup` by hand, turn mobile data off |
| Page does not load on the home Wi-Fi | Use the IP instead of `waterrower.local` |
| Forgot the device password | Reflash with *Erase Flash* ([Flashing](flashing.md)); settings and workouts are lost |
| Sensors stay at 0, no "Ergometer connected" | The ESP sees no device. Check the solder bridge (multimeter: about 5 V on the VBUS pin of the **USB** socket), check adapter and cable for data lines |
| The S4 never switches itself off | Normal and not fixable in firmware: as soon as it sees 5 V on VBUS and is enumerated it behaves as if attached to a PC and runs from USB power. Its 2-minute auto power-off only applies on battery. To have it off between workouts, cut the ESP's power, e.g. with a smart plug |
| Workouts have the wrong time (shifted by hours) | Check the time zone in the ESP ([Settings](#settings)) |
