# S4-Protokoll

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/s4-protocol.md)

Grundlage ist WaterRowers Dokument **„Water Rower S4 & S5 USB Protocol, Issue 1.04“**. Es liegt hier nicht bei, weil die Weitergabebedingungen unklar sind; die Suche nach dem genauen Titel findet es schnell. Zusammenfassung:

- Serielle CDC-Verbindung, 19200 Baud, ASCII, Zeilenende `\r\n`
- `USB` öffnet die Verbindung, Antwort `_WR_`
- `IRS xxx` / `IRD xxx` / `IRT xxx` lesen 1 / 2 / 3 Bytes ab Adresse `xxx`
- Antworten `IDS` / `IDD` / `IDT` liefern die Bytes **höchstes Byte zuerst**
- `SS` / `SE` = Schlag Anfang / Ende, `Pxx` = Paddelimpulse je 25 ms, `PING` jede Sekunde im Leerlauf
- Höchstens ein Paket pro 25 ms senden

Die Umsetzung steht in `firmware/src/rower.c`.

## Verwendete Adressen

| Adresse | Register laut Spezifikation | Bedeutung | Stand |
|---|---|---|---|
| `057`/`058` | distance_low/hi | angezeigte Distanz (m) | bestätigt |
| `14A`/`14B` | m_s_low/hi_average | Geschwindigkeit (cm/s), entspricht der Anzeige | bestätigt |
| `1A9` | zone_sr_val | Schlagfrequenz | bestätigt |
| `1E1`–`1E3` | display_sec/min/hr | Zeit, **BCD-codiert** | bestätigt |
| `140`/`141` | strokes_cnt_low/hi | Schlagzahl | plausibel |
| `142`/`143` | stroke_average / stroke_pull | Schlagzeiten in 25-ms-Schritten | ungeprüft |
| `081`/`082` | total_dis_low/hi | Gesamtdistanz | ungeprüft |
| `088`/`089` | kcal_watts_low/hi | Watt | **Skalierung ungeprüft** |
| `1A0` | zone_hr_val | Herzfrequenz (nur mit Brustgurt) | nicht verwendet |

Der Stand stammt aus der früheren ESPHome-Variante, die an echter Hardware geprüft wurde. Die neue Firmware liest dieselben Register, ist aber selbst noch nicht an der S4 erprobt.

Stolperfallen, die Zeit gekostet haben:

- `148`/`149` (`m_s_*_total`) liefert nicht den Anzeigewert, `14A`/`14B` schon.
- `14C` ist ein Zähler, keine Geschwindigkeit.
- Die Zeitregister sind BCD: `0x13` heißt 13, nicht 19.

## Verhalten der Firmware

- Beim Erscheinen der S4 sendet der ESP `USB` (ohne bleibt sie stumm) und wiederholt den Handshake, wenn 10 s keine Pakete kommen. „Ergometer verbunden“ und `usb_mode` melden den Zustand.
- Während einer Einheit wird jede Sekunde abgefragt. Im Leerlauf sendet der ESP nichts, denn jedes Paket setzt die Abschaltuhr der S4 zurück. (Über USB-Strom schaltet sich die S4 ohnehin nie aus, siehe [Einrichtung](einrichtung.md#hilfe).)
- Der Einheitenstart wird am ersten Schlag erkannt. Sie endet per Knopf (*Einheit beenden*, MQTT `cmd/end_session`) oder nach dem Sitzungs-Timeout ohne Schlag- oder Paddelpakete (Standard 5 Minuten; das Geschwindigkeitsregister taugt dafür nicht, es behält nach dem Stopp seinen letzten Wert).
- Geschwindigkeit, Schlagfrequenz und Watt gelten als 0, sobald die S4 kein Rudern meldet (`PING`); der Monitor selbst zeigt weiter die Werte des letzten Schlags.
