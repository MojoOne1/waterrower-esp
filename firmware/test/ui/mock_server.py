#!/usr/bin/env python3
"""A stand-in for the ESP's HTTP API, to look at the web UI without hardware.

Serves firmware/web/ and answers the /api/* calls with made-up but
realistic data in the shapes firmware/src/web.c, live.c and store.c produce.
Nothing here is recorded from a real rower: the sessions are generated
(deterministically) so the screenshots can be reproduced.

    python3 mock_server.py [--port 8099] [--state live|idle]

Writes (settings, end/reset, delete) are accepted and ignored.
"""
import argparse
import json
import math
import pathlib
import random
import time
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse
from zoneinfo import ZoneInfo

WEB = pathlib.Path(__file__).resolve().parents[2] / "web"
TZ = ZoneInfo("Europe/Berlin")


def epoch(y, mo, d, h, mi):
    return int(datetime(y, mo, d, h, mi, tzinfo=TZ).timestamp())


# (started_at, distance_m, base speed m/s, strokes per minute)
PLAN = [
    (epoch(2026, 11, 11, 6, 40), 2000, 4.05, 24),
    (epoch(2026, 11, 9, 18, 10), 5000, 3.75, 22),
    (epoch(2026, 11, 7, 7, 15), 1000, 4.30, 27),
    (epoch(2026, 11, 4, 18, 30), 6000, 3.60, 21),
    (epoch(2026, 11, 2, 6, 45), 2000, 3.95, 24),
    (epoch(2026, 10, 30, 18, 5), 4000, 3.70, 22),
    (epoch(2026, 10, 27, 7, 0), 2000, 3.85, 23),
    (epoch(2026, 10, 24, 18, 20), 5000, 3.65, 22),
]


def make_session(started, target, base, spm):
    """One sample per second until the target distance is reached."""
    rng = random.Random(started)
    sid = datetime.fromtimestamp(started, TZ).strftime("%Y%m%dT%H%M%S")
    t, dist, strokes = 0, 0.0, 0.0
    samples = []
    while dist < target:
        ramp = min(1.0, t / 25)                      # the first seconds are slow
        sprint = 0.25 if target - dist < 250 else 0  # and the end is a sprint
        wave = 0.10 * math.sin(t / 9.0)
        v = max(0.0, (base + wave + sprint) * ramp + rng.uniform(-0.07, 0.07))
        r = int(round((spm + (3 if sprint else 0) + rng.uniform(-1.2, 1.2)) * ramp))
        dist += v
        strokes += r / 60.0
        samples.append({
            "session_id": sid,
            "ts": started + t, "distance_m": int(dist), "speed_ms": round(v, 2),
            "stroke_rate": r, "strokes": int(strokes), "duration_s": t,
            "watts": int(2.8 * v ** 3),
        })
        t += 1
    n = len(samples)
    buckets = []
    for i in range(40):
        part = samples[i * n // 40:(i + 1) * n // 40] or samples[-1:]
        buckets.append(round(sum(s["speed_ms"] for s in part) / len(part) * 10) / 10)
    summary = {
        "session_id": sid, "started_at": started, "ended_at": started + n,
        "distance_m": samples[-1]["distance_m"], "duration_s": n,
        "strokes": samples[-1]["strokes"],
        "avg_speed_ms": round(samples[-1]["distance_m"] / n, 2),
        "avg_spm": round(sum(s["stroke_rate"] for s in samples) / n, 1),
    }
    return summary, samples, buckets


SESSIONS = {}
ORDER = []
for plan in PLAN:
    summary, samples, spark = make_session(*plan)
    SESSIONS[summary["session_id"]] = (summary, samples, spark)
    ORDER.append(summary["session_id"])

WIFI = {
    "ssid": "Heimnetz", "connected": True, "ip": "192.168.1.57", "rssi": -58,
    "ap_active": False, "ap_ssid": "WaterRower-3F2A", "ap_default_password": True,
    "tz": "CET-1CEST,M3.5.0,M10.5.0/3", "session_timeout_min": 5, "password_set": False,
}
SCAN = [
    {"ssid": "Heimnetz", "rssi": -58, "secure": True},
    {"ssid": "Heimnetz-Gast", "rssi": -63, "secure": True},
    {"ssid": "FRITZ!Box 7590 NK", "rssi": -74, "secure": True},
    {"ssid": "Nachbar-WLAN", "rssi": -86, "secure": True},
]
ARENA = {"url": "https://arena.example.com", "enabled": True, "token_set": True,
         "connected": True, "athlete": "Jan", "error": "", "last_sent": 2}
MQTT = {"enabled": True, "host": "192.168.1.10", "port": 1883, "username": "waterrower",
        "password": "••••••", "prefix": "waterrower", "discovery": True,
        "connected": True, "error": ""}


def live(state):
    started = epoch(2026, 11, 11, 18, 42)
    sid = datetime.fromtimestamp(started, TZ).strftime("%Y%m%dT%H%M%S")
    if state == "idle":
        values = {"distance": 0, "speed": 0.0, "stroke_rate": 0, "strokes": 0, "duration": 0,
                  "watts": 0, "total_distance": 148230, "ratio": 0.0,
                  "firmware_version": "3.0.0-esp", "s4_connected": True, "usb_mode": True}
        return {"values": values, "session_id": "", "session_active": False,
                "updated_at": started, "connected": True, "time_valid": True, "race": None}
    values = {"distance": 1236, "speed": 4.02, "stroke_rate": 24, "strokes": 247, "duration": 312,
              "watts": 181, "total_distance": 149466, "ratio": 2.1,
              "firmware_version": "3.0.0-esp", "s4_connected": True, "usb_mode": True}
    return {"values": values, "session_id": sid, "session_active": True,
            "updated_at": started + 312, "connected": True, "time_valid": True, "race": None}


class Handler(BaseHTTPRequestHandler):
    state = "live"

    def log_message(self, *args):
        pass

    def send(self, body, ctype="application/json", code=200):
        if not isinstance(body, bytes):
            body = json.dumps(body).encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_POST(self):
        n = int(self.headers.get("Content-Length") or 0)
        self.rfile.read(n)
        self.send({"ok": True})

    do_DELETE = do_POST

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/":
            return self.send((WEB / "index.html").read_bytes(), "text/html; charset=utf-8")
        if path == "/setup":
            return self.send((WEB / "setup.html").read_bytes(), "text/html; charset=utf-8")
        if path.startswith("/static/"):
            f = WEB / path.removeprefix("/static/")
            if f.is_file() and f.parent == WEB:
                ctype = "text/css" if f.suffix == ".css" else "application/javascript"
                return self.send(f.read_bytes(), ctype)
        if path == "/api/version":
            return self.send({"version": "3.0.0-esp", "commit": ""})
        if path == "/api/live":
            return self.send(live(self.state))
        if path == "/api/stream":
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            try:
                while True:
                    self.wfile.write(f"data: {json.dumps(live(self.state))}\n\n".encode())
                    self.wfile.flush()
                    time.sleep(1)
            except (BrokenPipeError, ConnectionResetError):
                return
        if path == "/api/sessions":
            return self.send([{**SESSIONS[i][0], "sparkline": SESSIONS[i][2]} for i in ORDER])
        if path.startswith("/api/sessions/"):
            sid = path.split("/")[3]
            if sid in SESSIONS:
                if path.endswith(".csv"):
                    rows = ["ts,distance_m,speed_ms,stroke_rate,strokes,watts"]
                    rows += [f"{s['ts']},{s['distance_m']},{s['speed_ms']},{s['stroke_rate']},"
                             f"{s['strokes']},{s['watts']}" for s in SESSIONS[sid][1]]
                    return self.send("\n".join(rows).encode(), "text/csv")
                return self.send({"session": SESSIONS[sid][0], "samples": SESSIONS[sid][1]})
        if path in ("/api/wifi", "/api/system"):
            return self.send(WIFI)
        if path == "/api/wifi/scan":
            return self.send(SCAN)
        if path == "/api/arena":
            return self.send(ARENA)
        if path == "/api/mqtt":
            return self.send(MQTT)
        self.send({"detail": "not found"}, code=404)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8099)
    ap.add_argument("--state", choices=("live", "idle"), default="live")
    a = ap.parse_args()
    Handler.state = a.state
    print(f"mock ESP on http://127.0.0.1:{a.port}/  (state: {a.state})")
    ThreadingHTTPServer(("127.0.0.1", a.port), Handler).serve_forever()
