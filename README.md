# WaterRower ESP

Fork von [MojoOne1/waterrower-mqtt](https://github.com/MojoOne1/waterrower-mqtt) mit dem Ziel: **ein einziges Gerät am Ruderer – der ESP32-S3.**
Kein Tracker-Container, keine ESPHome-Firmware. MQTT/Home Assistant ist ein **optionaler Schalter** in der Weboberfläche. Nur der optionale **Arena-Server** (Mehrspieler) läuft in Docker und liegt in einem eigenen Repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena).

```
WaterRower S4 ──USB──► ESP32-S3 ──WLAN──► Browser (Handy/PC)  http://waterrower.local
                        │  Aufzeichnung (Flash), Diagramme, CSV/Excel
                        └── ausgehende WebSocket ──► Arena (Docker, optional, eigenes Repo)
```

## Was der ESP kann

- S4-Anbindung per USB (CDC-ACM-Host), Protokoll und Sitzungslogik 1:1 aus der alten ESPHome-Firmware
- Sitzungen werden **im internen Flash** gespeichert (LittleFS, ~10 MB, älteste Einheiten werden bei Platzmangel gelöscht)
- Dieselbe Web-Oberfläche wie der alte Tracker (live, Verlauf, Vergleich, CSV, **Excel – wird im Browser erzeugt**)
- **WLAN ohne Neuflashen:** keine Zugangsdaten oder keine Verbindung → der ESP öffnet den Hotspot `WaterRower-XXXX` (Passwort `waterrower`), das Handy wird auf `/setup` geleitet. Auch ohne WLAN lässt sich rudern und die Seite unter `192.168.4.1` ansehen.
- Uhrzeit per NTP; ohne Internet übernimmt der ESP die Uhrzeit vom Browser
- Arena-Uplink (`wss://`, Token) mit Nachsenden verpasster Einheiten und Rennstatus
- Firmware-Update über die Weboberfläche (OTA, `firmware.bin` hochladen)

- **Optional: MQTT mit Home-Assistant-Discovery** (Karte „MQTT“, standardmäßig aus, per Schalter an/aus). Gleiche Topics und Entitätsnamen wie die alte ESPHome-Firmware (`waterrower/distance`, `waterrower/live`, `waterrower/session/last`, Befehle auf `waterrower/cmd/end_session`). Das Gerät „WaterRower“ mit 23 Entitäten (Sensoren, Buttons *Reset*/*End Session*, Zahl *Session Timeout*) taucht in HA von selbst auf; Last Will markiert sie bei Ausfall als „nicht verfügbar“.

Bewusst **nicht** mehr dabei: ESPHome und dessen native Home-Assistant-API.

## Bauen und flashen

Hardware wie im Original: ESP32-S3-DevKitC-1 (N16R8), USB-OTG-Lötbrücke geschlossen, **COM**-Port zum PC, **USB**-Port über Adapter zur S4.

```bash
pip install platformio
cd firmware
pio run                 # baut .pio/build/esp32s3/firmware.bin
pio run -t upload       # erstes Flashen über den COM-Port (ggf. BOOT gedrückt halten)
pio device monitor      # Log
```

Danach laufen Updates über die Weboberfläche (Karte „Firmware-Update“). Die Datei: `firmware/.pio/build/esp32s3/firmware.bin`.

Partitionen (16 MB): 2× 3 MB App (OTA) + ~10 MB Speicher für Einheiten.

## Erste Einrichtung

1. Flashen, ESP an Strom. Handy mit `WaterRower-XXXX` verbinden (Passwort `waterrower`).
2. Die Einrichtungsseite öffnet sich (sonst `http://192.168.4.1/setup`): Netz wählen, Passwort eingeben.
3. Danach erreichbar unter `http://waterrower.local` (oder der angezeigten IP).
4. Zeitzone (POSIX-String, Standard `CET-1CEST,M3.5.0,M10.5.0/3`) und Sitzungs-Timeout unter dem Status-Chip oben rechts.

**Umzug in ein anderes Haus:** ESP einstecken – findet er das alte WLAN nicht, öffnet er nach ~20 s den Hotspot. Neues WLAN eintragen, fertig.

## Sicherheit

Der ESP hängt ohne TLS im Heimnetz. Was die Firmware dagegen tut – und was nicht:

- **Gerätepasswort (optional, empfohlen):** unter *WLAN & Einstellungen* setzen. Danach brauchen alle ändernden Zugriffe (Einstellungen, Löschen, Sitzung beenden, Firmware-Update) das Passwort; der Browser fragt einmal danach (Benutzername beliebig). Gespeichert wird nur ein gesalzener Hash. Vergessen? `pio run -t erase` und neu flashen.
- **Hotspot-Passwort:** ebenfalls dort änderbar. Der Standard `waterrower` ist öffentlich bekannt – ändern, wenn der Hotspot in Reichweite Fremder liegt.
- **Schutz vor fremden Webseiten:** Anfragen werden nur angenommen, wenn der ESP per IP-Adresse oder lokalem Namen (`waterrower.local`, `waterrower.lan` …) angesprochen wird (DNS-Rebinding) und ändernde Anfragen von seiner eigenen Seite kommen (CSRF). Hinter einem anderen Namen (z. B. `waterrower.fritz.box`) antwortet die API deshalb mit 403 – dann die IP nehmen.
- **Arena-Token:** wird nie ausgegeben und nur an die Adresse gesendet, für die er eingetragen wurde; beim Ändern der Adresse muss er neu eingegeben werden.
- **Firmware-Update:** nur Images dieses Projekts, nicht während einer Aufzeichnung; ein Image, das nicht sauber startet, wird beim nächsten Boot zurückgerollt.

Bewusst offen bzw. nicht abgedeckt:

- Lesende Zugriffe (Live-Werte, Einheiten, Export) sind im WLAN ohne Passwort möglich.
- Kein TLS auf dem Gerät: Gerätepasswort und WLAN-Eingaben gehen im lokalen Netz im Klartext über die Leitung.
- WLAN-, MQTT- und Arena-Zugangsdaten liegen unverschlüsselt im Flash (keine Flash-Verschlüsselung, kein Secure Boot); wer das Gerät in der Hand hat, kann sie auslesen und beliebige Firmware flashen.
- MQTT ohne `mqtts://` überträgt Zugangsdaten im Klartext; `mqtts://` prüft gegen öffentliche Zertifizierungsstellen, selbstsignierte Broker-Zertifikate werden nicht akzeptiert.

## Tests

```bash
sh firmware/test/host/run.sh          # Speicher, Eingabeprüfung, MQTT-Discovery (ohne Hardware)
```

Die Tests des Arena-Servers liegen im Arena-Repository (`python tests/run_all.py`). Die Tests der alten Tracker-Variante unter `legacy/` wurden mit dem Umzug der Arena entfernt; sie stehen in der Git-Historie (Commit `1d2cb70`).

## Arena (Mehrspieler)

Der Arena-Server wohnt in [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena) (Docker, Cloudflare Tunnel oder eigener Proxy; Betrieb, Protokoll und API sind dort dokumentiert). Nur wer die Arena **betreibt**, braucht Docker.

Mitrudern: Der Betreiber legt einen Athleten an und erzeugt unter *Konto → Tracker-Verbindung* einen Token. Im ESP (Status-Chip → Arena) **Adresse** (z. B. `https://arena.example.com`) und **Token** eintragen und aktivieren. Der ESP spricht direkt mit der Arena; das Protokoll steht in [docs/protokoll.md](https://github.com/MojoOne1/waterrower-arena/blob/main/docs/protokoll.md), die Firmware-Seite in `firmware/src/uplink.c`.

## Aufbau

| Pfad | Inhalt |
|---|---|
| `firmware/src/rower.c` | S4-USB-Link, Protokoll-Parser, Sitzungslogik |
| `firmware/src/store.c` | Sitzungsspeicher (LittleFS), Zusammenfassungen, Wiederherstellung nach Stromausfall |
| `firmware/src/web.c` | HTTP-API (kompatibel zum alten Tracker), Live-Stream (SSE), OTA |
| `firmware/src/wifi_mgr.c` | WLAN, Fallback-Hotspot, Captive-DNS, mDNS, NTP |
| `firmware/src/uplink.c` | WebSocket-Client zur Arena inkl. Backfill |
| `firmware/web/` | Oberfläche (wird beim Build gzip-komprimiert in die Firmware eingebettet) |
| `firmware/test/host/` | Tests, die ohne Hardware auf dem PC laufen |
| `firmware/release/` | Fertiges Komplett-Image zum ersten Flashen (Adresse `0x0`) |
| `legacy/` | Die alte Variante: ESPHome-Firmware und Tracker-Container (unverändert, nicht Teil des Reviews, ohne Tests und ohne Arena) |

## Stand und Grenzen

- Die Firmware **kompiliert** (ESP-IDF 6.1 via PlatformIO), die Speicherlogik ist auf dem Host getestet. **Auf echter Hardware noch nicht getestet**: USB-Anbindung der S4, Captive Portal, TLS zur Arena und OTA sollten zuerst einzeln geprüft werden.
- Ältere Daten aus dem Tracker (SQLite) werden nicht übernommen.
- MQTT: Die Discovery-Nachrichten sind auf dem Host auf gültiges JSON geprüft, aber nicht gegen einen echten Broker/HA. Ein Home-Assistant-Dashboard liefert der Fork nicht mit; das baut sich jeder selbst aus den Discovery-Entitäten.
- Excel-Export ohne Diagramm (nur Tabellen); der alte Server-Export enthielt zusätzlich ein Diagramm.
- Der ESP hält maximal 4 gleichzeitige Live-Ansichten.
