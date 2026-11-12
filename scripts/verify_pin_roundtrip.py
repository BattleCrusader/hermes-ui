#!/ usr / bin / env python3
"""Pin round-trip verification against the live gateway.

Gets a session ticket over the control socket the same way the native app
does, then PATCHes a session's pinned flag and reads it back.
"""
import json
import os
import socket
import urllib.request
from pathlib import Path

home = Path.home() / ".hermes"
sock_path = home / "gateway.sock"


def control(payload):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.settimeout(5)
    s.connect(str(sock_path))
    s.sendall(json.dumps(payload).encode() + b"\n")
    buf = b""
    while b"\n" not in buf:
        chunk = s.recv(65536)
        if not chunk:
            break
        buf += chunk
    s.close()
    return json.loads(buf.split(b"\n")[0])


ident = control({"protocol": 1, "verb": "identify", "id": 1})
endpoint = ident["result"]
origin = endpoint["api_origin"]
def mint():
    return control({
        "protocol": 1,
        "verb": "session-ticket",
        "id": 2,
        "params": {
            "profile_id": str(home),
            "instance_id": endpoint["instance_id"],
            "purpose": "native-http",
        },
    })["result"]["ticket"]


print("origin", origin, "ticket", mint()[:12] + "...")


def call(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(origin + path, data=data, method=method)
    req.add_header("X-Hermes-Gateway-Ticket", mint())
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read() or b"{}")
    except urllib.error.HTTPError as exc:
        print("HTTP", exc.code, exc.read()[:400])
        raise


sessions = call("GET", "/api/sessions?limit=5&order=recent&exclude_sources=cron")
rows = sessions.get("sessions", [])
if not rows:
    raise SystemExit("no sessions to pin")
target = rows[-1]
sid = target["id"]
print("target", sid[:12], "pinned_before", target.get("pinned"))

call("PATCH", f"/api/sessions/{sid}", {"pinned": True, "profile": "default"})
after = call("GET", "/api/sessions?limit=5&order=recent&exclude_sources=cron")
pinned = next(s.get("pinned") for s in after["sessions"] if s["id"] == sid)
print("pinned_after_patch", pinned)

call("PATCH", f"/api/sessions/{sid}", {"pinned": False, "profile": "default"})
after2 = call("GET", "/api/sessions?limit=5&order=recent&exclude_sources=cron")
pinned2 = next(s.get("pinned") for s in after2["sessions"] if s["id"] == sid)
print("pinned_after_unpin", pinned2)
assert pinned is True and pinned2 is False, "pin round-trip failed"
print("PIN ROUND-TRIP OK")
