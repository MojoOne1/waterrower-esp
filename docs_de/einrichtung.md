# Einrichtung

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/einrichtung.md)

Von der leeren Platine bis zur ersten Einheit. Vorkenntnisse sind nicht nötig.

## Überblick

1. [Hardware besorgen und verbinden](#hardware)
2. [Firmware flashen](flashen.md) (einmalig, am Computer)
3. [WLAN einrichten](#wlan-einrichten) (am Handy)
4. [Seite öffnen und rudern](#seite-öffnen)

## Hardware

- **ESP32-S3-DevKitC-1, Variante N16R8** (16 MB Flash, 8 MB PSRAM). Andere Varianten haben zu wenig Speicher.
- Ein USB-C-**Datenkabel** zum Flashen.
- Ein **USB-C-Netzteil** (5 V) für den Dauerbetrieb.
- Eine Verbindung vom **USB**-Port des ESP zur Mini-USB-Buchse der WaterRower S4. Der ESP arbeitet dort als USB-Host. Üblich ist ein USB-OTG-Adapter (USB-C-Stecker auf USB-A-Buchse) plus ein normales USB-A-auf-Mini-USB-Kabel. *Diese Verkabelung ist noch nicht auf echter Hardware erprobt.*
- Die kleine **USB-OTG-Lötbrücke** auf dem Board muss geschlossen sein (Beschriftung „USB-OTG“ nahe der USB-Buchse).
- Optional ein [Gehäuse zum Drucken](../case/).

Die zwei Buchsen des Boards: **COM** (zum Flashen und für Strom) und **USB** (zur S4). Nicht verwechseln.

## Flashen

Ausführlich mit Bildern der Schritte und Fehlerhilfe: **[Flashen mit dem Browser-Flasher](flashen.md)**. Kurz: [`firmware/release/waterrower-esp-3.0.0-merged.bin`](../firmware/release/) an Adresse **`0x0`** schreiben.

Wer selbst bauen will: [Entwicklung](entwicklung.md).

## WLAN einrichten

Ohne gespeicherte Zugangsdaten öffnet der ESP sofort den Hotspot **`WaterRower-XXXX`** (Passwort `waterrower`). Kennt er ein Netz, das nicht erreichbar ist, geschieht das nach ca. 20 Sekunden.

![Einrichtungsportal](../screenshots/setup-portal.png)

1. Am Handy in den WLAN-Einstellungen **`WaterRower-XXXX`** wählen, Passwort `waterrower`. Das Handy meldet „kein Internet“: **verbunden bleiben**. Ggf. „Mobile Daten“ kurz ausschalten, sonst wechselt das Handy zurück.
2. Die Einrichtungsseite öffnet sich von selbst. Sonst im Browser `http://192.168.4.1/setup` eingeben.
3. **Netze suchen**, das eigene Netz antippen, Passwort eingeben, **Verbinden**.
4. Der ESP startet die Verbindung, der Hotspot verschwindet. Das Handy wieder mit dem Heim-WLAN verbinden.

## Seite öffnen

Im Heim-WLAN im Browser **`http://waterrower.local`** eingeben (`http://`, nicht `https://`).

Öffnet sich nichts (manche Android-Geräte kennen `.local` nicht): in der Geräteliste des Routers nach **„waterrower“** suchen und dessen IP-Adresse eingeben, z. B. `http://192.168.1.57`. Ein anderer Name wie `waterrower.fritz.box` wird aus Sicherheitsgründen abgelehnt, dann die IP nehmen.

Ohne WLAN geht es auch: Mit dem Hotspot verbinden und `http://192.168.4.1` öffnen.

Der Chip oben zeigt **„Ergometer verbunden“**, sobald die S4 am ESP hängt. Mit dem ersten Zug beginnt eine Einheit. Alles Weitere: [Bedienung](bedienung.md).

## Einstellungen

![Einstellungen](../screenshots/settings.png)

Karte **WLAN & Einstellungen** (weiter unten auf der Seite):

| Feld | Bedeutung |
|---|---|
| WLAN-Name / -Passwort | Heimnetz; leeres Passwortfeld lässt das gespeicherte unverändert. *Netze suchen* listet Netze in Reichweite. |
| Zeitzone | POSIX-String, Standard `CET-1CEST,M3.5.0,M10.5.0/3` (Mitteleuropa mit Sommerzeit) |
| Sitzungs-Timeout | Minuten ohne Zug, bis eine Einheit automatisch endet (1–60, Standard 5) |
| Hotspot-Passwort | ersetzt `waterrower` (mind. 8 Zeichen), siehe [Sicherheit](sicherheit.md) |
| Gerätepasswort | schützt Änderungen, siehe [Sicherheit](sicherheit.md) |

Darunter: Karten für [Arena](arena.md), [MQTT / Home Assistant](mqtt-home-assistant.md) und *Firmware-Update*.

## Umzug oder neues WLAN

ESP einstecken. Findet er das alte Netz nicht, öffnet er nach ca. 20 s den Hotspot; dort das neue WLAN eintragen.

## Uhrzeit

Per NTP, sobald Internet da ist. Sonst übernimmt der ESP die Uhrzeit vom Browser, der die Seite öffnet.

## Hilfe

| Problem | Lösung |
|---|---|
| Hotspot erscheint nicht | 30 s warten, ESP aus- und einstecken. Weiter: [Flashen, Fehlerhilfe](flashen.md#wenn-etwas-nicht-klappt) |
| Einrichtungsseite öffnet sich nicht | `http://192.168.4.1/setup` von Hand eingeben, Mobile Daten aus |
| Seite lädt nicht im Heim-WLAN | IP statt `waterrower.local` nehmen |
| „Ergometer verbunden“ erscheint nicht | Kabel und OTG-Adapter prüfen und S4-Monitor einschalten |
| Gerätepasswort vergessen | Neu flashen mit *Erase Flash* ([Flashen](flashen.md)); dabei gehen Einstellungen und Einheiten verloren |
