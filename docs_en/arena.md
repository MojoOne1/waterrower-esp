# Arena (multiplayer)

[🇩🇪 Deutsch](../docs_de/arena.md) · 🇬🇧 English

The ESP can send workouts live to an arena server to row together. The server has its own repository: [MojoOne1/waterrower-arena](https://github.com/MojoOne1/waterrower-arena) (Docker; operation, protocol and API are documented there). Only whoever **runs** an arena needs Docker.

## Connecting

1. The operator creates an athlete and generates a token under *Account → Tracker connection*.
2. In the ESP settings open the *Arena* card: enter **server** (e.g. `https://arena.example.com`) and **token**, and enable it.
3. The chip at the top shows "Arena: <name>" once the connection is up.

The ESP opens the connection itself (`wss://`); no port forwarding to the ESP is needed. Missed workouts are re-sent later, race status is shown live. The token is never displayed and is only sent to the address it was entered for.

Protocol: [protokoll.md](https://github.com/MojoOne1/waterrower-arena/blob/main/docs/protokoll.md); firmware side: `firmware/src/uplink.c`.
