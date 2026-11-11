# WaterRower ESP

🇩🇪 Deutsch · [🇬🇧 English](README.en.md)

Firmware für einen **ESP32-S3**, der zwischen WaterRower S4 und WLAN sitzt: Er liest die Rudermaschine per USB, zeichnet jede Einheit im eigenen Flash auf und liefert die Weboberfläche gleich mit. Kein Server, kein Container, kein Cloud-Konto.

![Live-Ansicht](screenshots/live.png)

```
WaterRower S4 ──USB──► ESP32-S3 ──WLAN──► Browser (Handy/PC)  http://waterrower.local
                        │  Aufzeichnung (Flash), Diagramme, CSV/Excel
                        ├── MQTT / Home Assistant (optional)
                        └── Arena-Uplink (optional)
```

> Die Screenshots stammen aus einem Mock-Server mit **erzeugten Beispieldaten**, nicht von einer echten Rudereinheit.

## Funktionen

- **Live-Anzeige** mit Distanz, Tempo, Split/500 m, Schlagfrequenz, Watt und Verlaufsdiagramm
- **Aufzeichnung im internen Flash** (~10 MB, älteste Einheiten werden bei Platzmangel gelöscht), Wiederherstellung nach Stromausfall
- **Verlauf und Vergleich** von Einheiten, Export als **CSV** und **Excel** (wird im Browser erzeugt)
- **WLAN ohne Neuflashen:** Ohne Zugangsdaten öffnet der ESP einen Hotspot mit Einrichtungsseite
- Hell/Dunkel, Deutsch/Englisch, handytauglich
- Firmware-Update über die Weboberfläche (OTA)
- **Optional:** MQTT mit Home-Assistant-Discovery, Arena-Uplink für gemeinsames Rudern

## Schnellstart

1. Fertiges Image aus [`firmware/release/`](firmware/release/) an Adresse `0x0` flashen (Browser-Flasher oder `esptool`).
2. Mit dem Hotspot `WaterRower-XXXX` verbinden (Passwort `waterrower`), WLAN auswählen.
3. `http://waterrower.local` öffnen.

Details in der [Einrichtung](docs_de/einrichtung.md).

## Dokumentation

| Thema | Datei |
|---|---|
| Hardware, Flashen, WLAN, Umzug | [docs_de/einrichtung.md](docs_de/einrichtung.md) |
| Bedienung der Weboberfläche | [docs_de/bedienung.md](docs_de/bedienung.md) |
| MQTT und Home Assistant | [docs_de/mqtt-home-assistant.md](docs_de/mqtt-home-assistant.md) |
| Arena (Mehrspieler) | [docs_de/arena.md](docs_de/arena.md) |
| Sicherheit | [docs_de/sicherheit.md](docs_de/sicherheit.md) |
| Bauen, Tests, Aufbau des Codes | [docs_de/entwicklung.md](docs_de/entwicklung.md) |
| Gehäuse zum Drucken | [case/](case/) |

## Stand und Grenzen

- Die Firmware **kompiliert** (ESP-IDF 6.1 via PlatformIO), die Speicherlogik ist auf dem PC getestet. **Auf echter Hardware noch nicht getestet:** USB-Anbindung der S4, Captive Portal, TLS zur Arena und OTA sollten zuerst einzeln geprüft werden.
- MQTT-Discovery ist auf gültiges JSON geprüft, aber nicht gegen einen echten Broker oder Home Assistant.
- Excel-Export enthält Tabellen, aber kein Diagramm.
- Maximal 4 gleichzeitige Live-Ansichten.
