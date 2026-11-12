#!/ usr / bin / env python3
"""Generate the desktop feature inventory used by TODO.md.

Everything here is derived from the source trees on disk (no hand-written
claims): routes, app areas, settings pages, keybind actions, backend REST
routes, gateway event methods. Re-run after pulling the desktop app.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
from pathlib import Path

HOME = Path.home()
DESKTOP = HOME / ".hermes/hermes-agent/apps/desktop"
BACKEND = HOME / ".hermes/hermes-agent"
OUT = Path(__file__).resolve().parents[1] / "docs/desktop-feature-inventory.md"


def rel(path: Path, root: Path) -> str:
    return str(path.relative_to(root))


def files(root: Path, suffix: str = ".tsx") -> list[Path]:
    return sorted(p for p in root.rglob(f"*{suffix}") if "test" not in p.name)


def section(title: str, body: str) -> str:
    return f"\n## {title}\n\n{body}\n"


def bullets(items: list[str]) -> str:
    return "\n".join(f"- {i}" for i in items) if items else "_none_"


def main() -> None:
    parts: list[str] = []
    parts.append(
        "# Desktop feature inventory (generated)\n\n"
        "Source: `~/.hermes/hermes-agent/apps/desktop` and the gateway's "
        "`hermes_cli/web_routers`. Regenerate with "
        "`python3 scripts/gen_feature_inventory.py`. Every list below is the "
        "actual file/route set on disk, so it is the parity checklist rather "
        "than a summary."
    )

# ── Routes and views ────────────────────────────────────────────────────
    routes_src = (DESKTOP / "src/app/routes.ts").read_text()
    routes = re.findall(r"path:\s*([A-Z_]+)_ROUTE|path:\s*'([^']+)'", routes_src)
    route_names = re.findall(r"\{ id: '([a-z-]+)', path:", routes_src)
    views = re.findall(r"^\s+\|\s+'([a-z-]+)'", routes_src.split("export type AppView")[1].split("export type AppRouteId")[0], re.M)
    parts.append(
        section(
            "Routes and app views",
            f"- App routes ({len(route_names)}): {', '.join(route_names)}\n"
            f"- App views ({len(views)}): {', '.join(views)}\n"
            "- Session routes are `/<session-id>`; reserved app paths are "
            "excluded from the session-id parser.",
        )
    )

# ── App areas ───────────────────────────────────────────────────────────
    app = DESKTOP / "src/app"
    areas = sorted(p.name for p in app.iterdir() if p.is_dir())
    area_rows = []
    for area in areas:
        src_files = files(app / area, ".tsx") + files(app / area, ".ts")
        area_rows.append(f"{area} ({len(src_files)} non-test files)")
    parts.append(section(f"App areas ({len(areas)})", bullets(area_rows)))

# ── Settings ────────────────────────────────────────────────────────────
    settings = app / "settings"
    settings_files = files(settings, ".tsx") + files(settings, ".ts")
    parts.append(
        section(
            f"Settings files ({len(settings_files)})",
            bullets([rel(p, DESKTOP) for p in settings_files]),
        )
    )

# ── Chat surface ────────────────────────────────────────────────────────
    chat = app / "chat"
    chat_dirs = sorted(p.name for p in chat.iterdir() if p.is_dir())
    chat_rows = []
    for name in chat_dirs:
        group = files(chat / name, ".tsx") + files(chat / name, ".ts")
        chat_rows.append(f"{name}/ ({len(group)})")
    parts.append(section("Chat modules", bullets(chat_rows)))

# ── Right sidebar / overlays / command palette ──────────────────────────
    for area in ("right-sidebar", "overlays", "command-palette", "shell", "hooks", "contrib"):
        group = files(app / area, ".tsx") + files(app / area, ".ts")
        parts.append(
            section(
                f"{area} ({len(group)} non-test files)",
                bullets([rel(p, DESKTOP) for p in group]),
            )
        )

# ── Capabilities ────────────────────────────────────────────────────────
    cap = app / "capabilities"
    cap_rows = []
    for name in sorted(p.name for p in cap.iterdir() if p.is_dir()):
        group = files(cap / name, ".tsx") + files(cap / name, ".ts")
        cap_rows.append(f"{name}/ ({len(group)})")
    parts.append(section("Capabilities subpages", bullets(cap_rows)))

# ── Keybind actions ─────────────────────────────────────────────────────
    actions = sorted(
        set(
            re.findall(
                r"id: '([a-z0-9_.]+)'",
                (DESKTOP / "src/lib/keybinds/actions.ts").read_text(),
            )
        )
    )
    parts.append(
        section(f"Keybind actions ({len(actions)})", bullets(actions))
    )

# ── Backend REST routes ─────────────────────────────────────────────────
    routers = BACKEND / "hermes_cli/web_routers"
    rest: list[str] = []
    for path in sorted(routers.glob("*.py")):
        text = path.read_text()
        for verb, route in re.findall(
            r"@\w+\.(get|post|put|patch|delete)\(\s*[\"']([^\"']+)", text
        ):
            rest.append(f"{verb.upper():6} {route}")
    rest = sorted(set(rest))
    parts.append(
        section(
            f"Gateway REST routes ({len(rest)})",
            "```\n" + "\n".join(rest) + "\n```",
        )
    )

# ── Gateway event methods ──────────────────────────────────────────────
    agent = (BACKEND / "hermes_cli/gateway_agent.py").read_text()
    handlers = re.findall(r'^\s+"([a-z][a-z0-9_.]+)":\s+GatewayAgent\.', agent, re.M)
    parts.append(
        section(
            f"Gateway event methods ({len(set(handlers))})",
            bullets(sorted(set(handlers))),
        )
    )

# ── Native parity summary(measured) ───────────────────────────────────
    repo = Path(__file__).resolve().parents[1]
    native_counts = {}
    for pattern in ("src/*.cc", "src/*.h"):
        for path in repo.glob(pattern):
            native_counts[path.name] = sum(1 for _ in path.open())
    parts.append(
        section(
            "Native source (measured)",
            "| file | lines |\n|---|---:|\n"
            + "\n".join(
                f"| {name} | {count} |"
                for name, count in sorted(native_counts.items())
            ),
        )
    )

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text("".join(parts))
    print(f"wrote {OUT} ({OUT.stat().st_size} bytes)")
    print(f"routes={len(route_names)} views={len(views)} areas={len(areas)}")
    print(f"settings_files={len(settings_files)} rest={len(rest)} actions={len(actions)}")
    print(f"events={len(set(handlers))}")


if __name__ == "__main__":
    main()
