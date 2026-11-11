"""The uplink endpoint: one WebSocket per tracker.

A tracker authenticates once with its device token, then streams the same
samples it writes into its own database. Nothing here trusts the client's
clock beyond the session's own elapsed seconds; the race clock is the
server's (see race.py).

Messages from the tracker

    hello     {token, agent, firmware}
    sample    {session, started_at, t, distance_m, speed_ms, stroke_rate,
               strokes, watts}
    summary   {session, distance_m, duration_s, strokes, avg_speed_ms, avg_spm}
    backfill  {session, started_at, ended_at, summary, samples: [...]}
    sync      {}                      asks which sessions the arena already has
    pong      {}

Messages to the tracker

    welcome   {athlete, server_time, known}
    cmd       {cmd: "reset", race_id}  close the session and zero the
                                       monitor, before the gun
              {cmd: "end", race_id}    close the session, leave the display
                                       alone, at the finish
    ack       {session, stored}
    ping      {}
"""

import asyncio
import logging
import time

from fastapi import WebSocket, WebSocketDisconnect

import achievements
import auth
import records
from hub import Uplink

log = logging.getLogger("ingest")

PING_S = 20          # Cloudflare drops an idle tunnel; this keeps it warm
MAX_BACKFILL_ROWS = 20000


def _num(value, lo: float = 0.0, hi: float = 1e7):
    """A finite number inside a sane range, or None.

    Everything a tracker sends ends up in the database and from there in
    other people's browsers. A device token only proves who the tracker
    belongs to, not that it is well behaved - so text, booleans, NaN and
    absurd figures stop here instead of being stored as they came.
    """
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if value != value or not lo <= value <= hi:
        return None
    return value


_FIELDS = ("distance_m", "speed_ms", "stroke_rate", "strokes", "watts")


def _clean(row) -> dict:
    """The numeric fields of a sample or summary, and nothing else."""
    if not isinstance(row, dict):
        return {}
    out = {k: _num(row.get(k)) for k in _FIELDS}
    for k in ("t", "duration_s"):
        if k in row:
            out[k] = int(_num(row.get(k), 0, 7 * 86400) or 0)
    for k in ("avg_speed_ms", "avg_spm"):
        if k in row:
            out[k] = _num(row.get(k), 0, 1000) or 0
    for k in ("distance_m", "strokes"):
        if out[k] is not None:
            out[k] = int(out[k])
    return out


def _when(value, fallback: float) -> float:
    """A timestamp from the tracker, or the fallback when it is not one."""
    v = _num(value, 0, 4102444800)          # up to the year 2100
    return float(v) if v else fallback


async def uplink_endpoint(websocket: WebSocket, db, hub) -> None:
    ip = websocket.client.host if websocket.client else "?"
    # Guessing a 256-bit token is hopeless, so this is not about guessing:
    # without it, anyone can open sockets here all day for free, each one
    # holding a task for fifteen seconds waiting on a hello.
    wait = auth.UPLINK_IP.retry_after(ip)
    if wait:
        await websocket.close(code=4429, reason="too many attempts")
        return

    await websocket.accept()
    athlete = None
    uplink = None
    pinger = None
    try:
        hello = await asyncio.wait_for(websocket.receive_json(), timeout=15)
        if hello.get("type") != "hello" or not hello.get("token"):
            auth.UPLINK_IP.record(ip)
            await websocket.close(code=4401, reason="hello expected")
            return
        owner = await asyncio.to_thread(db.token_owner, auth.token_hash(hello["token"]))
        if not owner:
            auth.UPLINK_IP.record(ip)
            await asyncio.to_thread(db.record_failure, "uplink", "", ip)
            await websocket.close(code=4403, reason="unknown token")
            log.warning("Uplink rejected from %s: unknown token", ip)
            return
        auth.UPLINK_IP.clear(ip)

        athlete = owner
        await asyncio.to_thread(db.touch_token, owner["token_id"])
        uplink = Uplink(athlete["id"], websocket, str(hello.get("agent", ""))[:80])
        hub.uplinks[athlete["id"]] = uplink
        hub.set_online(athlete["id"], True, uplink.agent)
        known = await asyncio.to_thread(db.known_remote_ids, athlete["id"])
        await websocket.send_json({
            "type": "welcome",
            "athlete": auth.public_athlete(athlete),
            "server_time": time.time(),
            "known": known,
        })
        log.info("Uplink up: %s (%s)", athlete["name"], uplink.agent)

        pinger = asyncio.create_task(_ping(websocket))
        # remote session ID -> surrogate key. Without this every single
        # sample would cost a database round trip just to learn the id it
        # had a second ago.
        seen: dict[str, int] = {}
        while True:
            msg = await websocket.receive_json()
            uplink.last_seen = time.time()
            await _handle(msg, athlete, db, hub, websocket, seen)

    except (WebSocketDisconnect, asyncio.TimeoutError):
        pass
    except Exception as e:
        log.info("Uplink error (%s): %s", athlete["name"] if athlete else "?", e)
    finally:
        if pinger:
            pinger.cancel()
        if athlete:
            if hub.uplinks.get(athlete["id"]) is uplink:
                hub.uplinks.pop(athlete["id"], None)
                hub.set_online(athlete["id"], False)
            log.info("Uplink down: %s", athlete["name"])


async def _award(db, hub, athlete: dict, ctx: dict) -> None:
    earned = await asyncio.to_thread(achievements.check, db, athlete["id"], ctx)
    for badge in earned:
        hub.broadcast({"type": "achievement", "athlete_id": athlete["id"],
                       "display_name": athlete["display_name"],
                       "name": badge["name"], "icon": badge["icon"],
                       "note": badge["note"]})


async def _ping(websocket: WebSocket) -> None:
    try:
        while True:
            await asyncio.sleep(PING_S)
            await websocket.send_json({"type": "ping"})
    except (asyncio.CancelledError, Exception):
        return


async def _session_key(db, athlete: dict, remote_id: str, started_at: float,
                       seen: dict[str, int]) -> int:
    if remote_id not in seen:
        seen[remote_id] = await asyncio.to_thread(
            db.session_id_for, athlete["id"], remote_id, started_at)
    return seen[remote_id]


def _remote_id(msg: dict) -> str:
    """The tracker's own name for a session: short printable text, or nothing."""
    rid = msg.get("session")
    if not isinstance(rid, str) or not 0 < len(rid) <= 64 or not rid.isprintable():
        return ""
    return rid


async def _handle(msg: dict, athlete: dict, db, hub, websocket: WebSocket,
                  seen: dict[str, int]) -> None:
    kind = msg.get("type")

    if not isinstance(msg, dict):
        return

    if kind == "sample":
        remote_id = _remote_id(msg)
        if not remote_id:
            return
        data = _clean(msg)
        t = data.get("t", 0)
        started_at = _when(msg.get("started_at"), time.time() - t)
        session_id = await _session_key(db, athlete, remote_id, started_at, seen)
        await asyncio.to_thread(db.add_sample, session_id, t, time.time(), data)
        hub.on_sample(athlete["id"], remote_id, session_id, t, data)

    elif kind == "summary":
        remote_id = _remote_id(msg)
        if not remote_id:
            return
        started_at = _when(msg.get("started_at"), time.time())
        session_id = await _session_key(db, athlete, remote_id, started_at, seen)
        await asyncio.to_thread(db.close_session, session_id, time.time(), _clean(msg))
        await asyncio.to_thread(records.index_session, db, session_id)
        hub.on_session_end(athlete["id"], session_id)
        stored = await asyncio.to_thread(db.session, session_id)
        hub.broadcast({"type": "session", "session": stored})
        await _award(db, hub, athlete, {"session": stored})
        log.info("Session closed: %s / %s", athlete["name"], remote_id)

    elif kind == "backfill":
        remote_id = _remote_id(msg)
        rows = msg.get("samples")
        if not remote_id or not isinstance(rows, list) or len(rows) > MAX_BACKFILL_ROWS:
            await websocket.send_json({"type": "ack", "session": remote_id, "stored": False})
            return
        rows = [_clean(r) for r in rows if isinstance(r, dict)]
        started_at = _when(msg.get("started_at"), time.time())
        session_id = await _session_key(db, athlete, remote_id, started_at, seen)
        await asyncio.to_thread(db.add_samples, session_id, rows)
        summary = _clean(msg.get("summary"))
        ended_at = _when(msg.get("ended_at"), started_at + float(summary.get("duration_s") or 0))
        await asyncio.to_thread(db.close_session, session_id, ended_at, summary)
        await asyncio.to_thread(records.index_session, db, session_id)
        await websocket.send_json({"type": "ack", "session": remote_id, "stored": True})
        stored = await asyncio.to_thread(db.session, session_id)
        hub.broadcast({"type": "session", "session": stored})
        # Backfilled history counts too - somebody who rowed before joining
        # should not have to row it again for the badge.
        await _award(db, hub, athlete, {"session": stored})
        log.info("Backfilled %s / %s (%d samples)", athlete["name"], remote_id, len(rows))

    elif kind == "sync":
        known = await asyncio.to_thread(db.known_remote_ids, athlete["id"])
        await websocket.send_json({"type": "known", "known": known})

    elif kind == "ping":
        await websocket.send_json({"type": "pong"})
