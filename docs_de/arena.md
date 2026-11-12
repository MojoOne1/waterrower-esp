# Arena (Mehrspieler)

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/arena.md)

Der ESP kann Einheiten live an einen Arena-Server schicken, um gemeinsam zu rudern. Den Server gibt es im eigenen Repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena) (Docker; Betrieb, Protokoll und API dort). Nur wer eine Arena **betreibt**, braucht Docker.

## Verbinden

1. Der Betreiber legt einen Athleten an und erzeugt unter *Konto → Tracker-Verbindung* einen Token.
2. Im ESP in den Einstellungen die Karte *Arena – gemeinsam rudern* öffnen: **Server** (z. B. `https://arena.example.com`) und **Token** eintragen, *Daten an die Arena senden* anhaken, speichern.
3. Der Chip oben zeigt „Arena: <Name>“, sobald die Verbindung steht.

Der ESP baut die Verbindung selbst aus (`wss://`), es braucht keine Portfreigabe zum ESP. Verpasste Einheiten werden nachgesendet, Rennstatus wird live angezeigt. Das Token wird nie ausgegeben und nur an die eingetragene Adresse gesendet.

## Wenn der ESP nicht verbindet

Der Status neben der Karte *Arena* (und die serielle Konsole mit 115200 Baud) sagt, woran es liegt:

| Meldung | Bedeutung und Abhilfe |
|---|---|
| `waiting for time sync (NTP)` | Das Gerät hat noch keine Uhrzeit. TLS braucht sie, um das Zertifikat zu prüfen. UDP 123 (NTP) und DNS für das Netz des ESP freigeben; die Fußzeile der Weboberfläche zeigt die Gerätezeit. |
| `connection failed (TLS/DNS/firewall)` | Bis zur Arena ist nichts durchgekommen. Prüfen, ob das Netz des ESP den Namen auflösen und TCP 443 ins Internet erreichen darf und ob keine Firewall das TLS-Zertifikat prüft oder ersetzt. Die serielle Konsole nennt den genauen Schritt. |
| `closed by arena (4403)` | Der Token ist unbekannt: falsch, gelöscht oder unvollständig kopiert. Einen neuen erzeugen. |
| `closed by arena (4429)` | Zu viele Fehlversuche von dieser Adresse; die Arena sperrt sie 15 Minuten (ein Neustart der Arena hebt das auf). |

Steht im Log der Arena nichts zu `/ws/uplink`, kam die Anfrage dort nie an: Dann liegt das Problem zwischen ESP und Cloudflare, nicht am Token. Der Haken *Daten an die Arena senden* muss gesetzt sein, sonst versucht der ESP es gar nicht.

Die Firmware 3.0.0 akzeptiert die cross-signierten Zertifikatsketten, die Cloudflare schickt. Ein älteres Image meldet `No matching trusted root certificate found`: mit `waterrower-esp-3.0.0-ota.bin` aktualisieren (Weboberfläche, *Firmware-Update*).

Protokoll: [protokoll.md](https://github.com/MojoOne1/waterrower-arena/blob/main/docs_de/protokoll.md); Firmware-Seite: `firmware/src/uplink.c`.
