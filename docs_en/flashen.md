# Flashing with the browser flasher

[🇩🇪 Deutsch](../docs_de/flashen.md) · 🇬🇧 English

**Flashing** means writing the firmware (the program) into the ESP32's memory. You only need it once; later updates go through the web UI. You do not have to install anything: a **browser flasher** is a web page that talks to the ESP directly over USB.

## What you need

- A **computer** (Windows, macOS or Linux) with **Chrome, Edge or Opera**. Firefox and Safari cannot do this, and phones cannot either (Android only with extra effort).
- A **USB data cable** (USB-C). Charge-only cables have no data wires; the browser then shows no port.
- The **ESP32-S3-DevKitC-1**. It has two USB-C sockets: one labelled **COM** (or UART), one labelled **USB**. **Always use COM for flashing.**
- The **image file** `waterrower-esp-3.0.0-merged.bin` from [`firmware/release/`](../firmware/release/). On GitHub open the file and click the download icon ("Download raw file") at the top right.

## Which file goes where?

| File | Purpose | Where |
|---|---|---|
| `waterrower-esp-3.0.0-merged.bin` | **First flash** (contains everything) | browser flasher, address **`0x0`** |
| `firmware.bin` (from `pio run`) | **Later update** | web UI, *Firmware update* card |

Do not mix them up: flashing `firmware.bin` to `0x0` leaves the device unable to boot. The image starts at `0x0` because it also contains the bootloader and partition table, which must sit there.

## Step by step

We use Espressif's flasher (Espressif makes the chip): <https://espressif.github.io/esptool-js/>. Labels may change slightly over time; the order stays the same.

1. **Connect the ESP.** Cable into the **COM** socket, other end to the computer. No other program may use the port (close serial monitors, Arduino IDE, PlatformIO).
2. **Open the page** and click **Connect**. A dialog lists the serial ports (names like "USB Serial", "CP210x" or "COM5"). Pick the right one and click *Connect*. If none shows up, see below.
3. The page's text area now shows something like **"Chip is ESP32-S3"**. Then everything is connected correctly.
4. **Recommended the first time:** click *Erase Flash* and confirm. This removes leftovers (e.g. from other firmware). Takes about half a minute.
5. Enter `0x0` as **Flash Address** and choose the `merged.bin` with **Choose File**.
6. Click **Program**. A bar runs to 100 %. Do not unplug until the page says it is done (about 1–2 minutes).
7. Click **Disconnect**, then press the **RESET** (or EN) button on the board, or unplug and replug the cable.
8. After about 20–30 seconds the Wi-Fi **`WaterRower-XXXX`** appears. That means success. Continue with [Setup](einrichtung.md#wi-fi-setup).

The first start takes a little longer because the ESP creates its storage for workouts.

## If something goes wrong

| Problem | Fix |
|---|---|
| No port in the dialog | Try another cable (data cable!), another USB port on the computer, plug into **COM** instead of **USB**. Windows: you may need the "CP210x" driver from Silicon Labs. Linux: add your user to the `dialout` group and log in again. |
| "Failed to connect" / "Wrong boot mode" | Hold the **BOOT** button, tap **RESET**, release BOOT, then click *Connect* again. |
| Aborts in the middle of writing | Pick a lower *Baudrate* (e.g. 115200) and reconnect. |
| Port "busy" | Close other programs and browser tabs using the port, briefly unplug the ESP. |
| No `WaterRower-XXXX` Wi-Fi after flashing | Did you enter `0x0` and choose the `merged.bin`? If not, *Erase Flash* and flash again. |

## Alternative: command line

If you have `esptool` installed (`pip install esptool`):

```bash
esptool --chip esp32s3 --port COM5 write_flash 0x0 waterrower-esp-3.0.0-merged.bin
```

On Linux/macOS the port is e.g. `/dev/ttyUSB0` or `/dev/cu.usbserial-…`.

## Checksum (optional)

To check that the file downloaded completely:

```bash
sha256sum waterrower-esp-3.0.0-merged.bin   # Windows: certutil -hashfile <file> SHA256
```

The value must match the one in `SHA256SUMS`.
