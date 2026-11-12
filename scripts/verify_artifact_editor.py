#!/usr/bin/env python3
"""Live verification for the artifact editor and plugin toggles.

Proves, against the running gateway, the two mutations the native pages now
issue: `/api/fs/write-text` (with the parent created by `/api/files/mkdir`) and
`/api/dashboard/agent-plugins/{name}/enable|disable`. Every change is re-read
from the backend and then restored.
"""
import json
import socket
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

home = Path.home() / ".hermes"


def control(payload):
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(str(home / "gateway.sock"))
    sock.sendall(json.dumps(payload).encode() + b"\n")
    buf = b""
    while b"\n" not in buf:
        chunk = sock.recv(65536)
        if not chunk:
            break
        buf += chunk
    sock.close()
    return json.loads(buf.split(b"\n")[0])


ident = control({"protocol": 1, "verb": "identify", "id": 1})["result"]
origin = ident["api_origin"]


def mint():
    return control(
        {
            "protocol": 1,
            "verb": "session-ticket",
            "id": 2,
            "params": {
                "profile_id": str(home),
                "instance_id": ident["instance_id"],
                "purpose": "native-http",
            },
        }
    )["result"]["ticket"]


def call(method, path, body=None):
    data = None if body is None else json.dumps(body).encode()
    request = urllib.request.Request(origin + path, data=data, method=method)
    request.add_header("X-Hermes-Gateway-Ticket", mint())
    request.add_header("Content-Type", "application/json")
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.loads(response.read() or b"{}")


def artifact_roundtrip():
    folder = "/tmp/hermes-ui-artifact-verify"
    note = folder + "/note.txt"
    call("POST", "/api/files/mkdir?profile=default", {"path": folder})
    call(
        "POST",
        "/api/fs/write-text?profile=default",
        {"path": note, "content": "first version"},
    )
    first = call(
        "GET", "/api/fs/read-text?profile=default&path=" + urllib.parse.quote(note)
    )
    assert first["text"] == "first version", first
    call(
        "POST",
        "/api/fs/write-text?profile=default",
        {"path": note, "content": "second version"},
    )
    second = call(
        "GET", "/api/fs/read-text?profile=default&path=" + urllib.parse.quote(note)
    )
    assert second["text"] == "second version", second
    call(
        "DELETE",
        "/api/files?profile=default",
        {"path": folder, "recursive": True},
    )
    listing = call(
        "GET",
        "/api/fs/list?profile=default&path="
        + urllib.parse.quote("/tmp"),
    )
    names = {entry["name"] for entry in listing["entries"]}
    assert "hermes-ui-artifact-verify" not in names, "delete left the folder"
    return {"write": True, "read_back": second["text"], "delete": True}


def plugin_toggle():
    hub = call("GET", "/api/dashboard/plugins/hub?profile=default")
    target = next(
        p for p in hub["plugins"] if p["name"] == "browser-browserbase"
    )
    before = target["runtime_status"]
    was_enabled = before == "enabled"
    wanted = "disable" if was_enabled else "enable"
    call("POST", f"/api/dashboard/agent-plugins/browser-browserbase/{wanted}")
    hub = call("GET", "/api/dashboard/plugins/hub?profile=default")
    toggled = next(
        p for p in hub["plugins"] if p["name"] == "browser-browserbase"
    )["runtime_status"]
    assert toggled != before, (before, toggled)
    restore = "enable" if was_enabled else "disable"
    call("POST", f"/api/dashboard/agent-plugins/browser-browserbase/{restore}")
    hub = call("GET", "/api/dashboard/plugins/hub?profile=default")
    restored = next(
        p for p in hub["plugins"] if p["name"] == "browser-browserbase"
    )["runtime_status"]
    assert restored == before, (before, restored)
    return {"before": before, "toggled": toggled, "restored": restored}


if __name__ == "__main__":
    print("artifact", artifact_roundtrip())
    print("plugin", plugin_toggle())
