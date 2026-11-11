# Arena (Mehrspieler)

🇩🇪 Deutsch · [🇬🇧 English](../docs_en/arena.md)

Der ESP kann Einheiten live an einen Arena-Server schicken, um gemeinsam zu rudern. Den Server gibt es im eigenen Repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena) (Docker; Betrieb, Protokoll und API dort). Nur wer eine Arena **betreibt**, braucht Docker.

## Verbinden

1. Der Betreiber legt einen Athleten an und erzeugt unter *Konto → Tracker-Verbindung* einen Token.
2. Im ESP in den Einstellungen die Karte *Arena – gemeinsam rudern* öffnen: **Server** (z. B. `https://arena.example.com`) und **Token** eintragen, *Daten an die Arena senden* anhaken, speichern.
3. Der Chip oben zeigt „Arena: <Name>“, sobald die Verbindung steht.

Der ESP baut die Verbindung selbst aus (`wss://`), es braucht keine Portfreigabe zum ESP. Verpasste Einheiten werden nachgesendet, Rennstatus wird live angezeigt. Das Token wird nie ausgegeben und nur an die eingetragene Adresse gesendet.

Protokoll: [protokoll.md](https://github.com/MojoOne1/waterrower-arena/blob/main/docs/protokoll.md); Firmware-Seite: `firmware/src/uplink.c`.
