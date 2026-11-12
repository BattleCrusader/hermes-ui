#!/usr/bin/env python3
"""Desktop -> native parity checker.

The desktop app is the contract. This harness:
  1. runs the desktop's own component tests for preferences, the composer model
     pill and the reasoning pill;
  2. checks the desktop source for the route/control test ids;
  3. asks the native app for its live widget inventory (`--ui-inventory`);
  4. writes report.md/report.json and exits non-zero when a critical feature has
     no native implementation.

It deliberately does not claim that a source marker is behavioural coverage:
the report labels native evidence as `inventory`, `source` or `gap`.
"""

from __future__ import annotations

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DESKTOP = Path.home() / ".hermes" / "hermes-agent" / "apps" / "desktop"
HERE = Path(__file__).resolve().parent
FEATURES = json.loads((HERE / "features.json").read_text())["features"]

try:
    from rest_routes import audit as rest_route_audit
    from rest_routes import write_report as write_rest_report
except ImportError:  # pragma: no cover - module lives beside this script
    rest_route_audit = None
    write_rest_report = None


def read(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return ""


def marker_ok(relative: str, needle: str | None) -> bool:
    path = DESKTOP / relative
    if not path.exists():
        return False
    return needle is None or needle in read(path)


def native_marker_ok(relative: str, needle: str | None) -> bool:
    path = ROOT / relative
    if not path.exists():
        return False
    return needle is None or needle in read(path)


def run_desktop_tests() -> tuple[bool, int, int, str]:
    tests = sorted(
        {
            feature["desktop"]["test"]
            for feature in FEATURES
            if feature.get("desktop", {}).get("test")
        }
    )
    if not tests:
        return True, 0, 0, "no desktop tests selected"
    command = ["npx", "vitest", "run", *tests]
    try:
        result = subprocess.run(
            command,
            cwd=DESKTOP,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=600,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return False, 0, 0, f"{type(error).__name__}: {error}"
    output = result.stdout
    files = re.search(r"Test Files\s+(\d+) passed", output)
    tests_match = re.search(r"Tests\s+(\d+) passed", output)
    passed = result.returncode == 0 and files is not None and tests_match is not None
    return (
        passed,
        int(tests_match.group(1)) if tests_match else 0,
        int(files.group(1)) if files else 0,
        output.strip().splitlines()[-1] if output.strip() else "no output",
    )


def native_inventory() -> tuple[set[str], str]:
    binary = ROOT / "build" / "hermes_ui"
    if not binary.exists():
        return set(), f"missing binary {binary}"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    try:
        result = subprocess.run(
            [str(binary), "--static", "--ui-inventory"],
            cwd=ROOT,
            env=env,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=90,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return set(), f"{type(error).__name__}: {error}"
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        return set(), f"inventory JSON: {error}"
    names: set[str] = set()
    for widget in payload.get("widgets", []):
        for key in ("object", "accessible"):
            value = str(widget.get(key, "")).strip().lower()
            if value:
                names.add(value)
    return names, f"{len(payload.get('widgets', []))} named widgets"


def native_ok(feature: dict, inventory: set[str]) -> tuple[bool, str]:
    native = feature.get("native", {})
    for name in native.get("inventory", []):
        if name.lower() in inventory:
            return True, f"inventory:{name}"
    markers = native.get("markers", [])
    if markers and all(
        native_marker_ok(relative, needle) for relative, needle in markers
    ):
        return True, "source markers"
    return False, "gap"


def desktop_ok(feature: dict, tests_passed: bool) -> tuple[bool, str]:
    desktop = feature.get("desktop", {})
    markers_ok = all(
        marker_ok(relative, needle)
        for relative, needle in desktop.get("markers", [])
    )
    test = desktop.get("test")
    if test is not None and not tests_passed:
        return False, f"test failed: {test}"
    if not markers_ok:
        return False, "source markers missing"
    return True, f"test {test}" if test else "source markers"


def desktop_e2e_state() -> str:
    path = DESKTOP / "test-results" / ".last-run.json"
    try:
        payload = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return "not run"
    return "passed" if payload.get("status") == "passed" else "failed"


def main() -> int:
    tests_passed, test_count, file_count, test_summary = run_desktop_tests()
    inventory, inventory_summary = native_inventory()
    e2e_state = desktop_e2e_state()
    rest = rest_route_audit() if rest_route_audit else {"summary": {}}
    if write_rest_report and rest.get("routes"):
        write_rest_report(rest)

    rows = []
    critical_gaps = []
    parity_count = 0
    partial_count = 0
    for feature in FEATURES:
        desktop_pass, desktop_evidence = desktop_ok(feature, tests_passed)
        native_pass, native_evidence = native_ok(feature, inventory)
        partial = feature["id"] in {"clarify", "approval"} and native_pass
        if not desktop_pass:
            status = "desktop-contract-missing"
        elif not native_pass:
            status = "native-gap"
            if feature.get("critical"):
                critical_gaps.append(feature["name"])
        elif partial:
            status = "partial"
            partial_count += 1
        else:
            status = "parity"
            parity_count += 1
        rows.append(
            {
                "id": feature["id"],
                "name": feature["name"],
                "critical": feature.get("critical", False),
                "desktop": desktop_evidence,
                "native": native_evidence,
                "status": status,
                "case": feature.get("case", ""),
            }
        )

    report = {
        "generated_at": subprocess.check_output(
            ["date", "-u", "+%Y-%m-%dT%H:%M:%SZ"], text=True
        ).strip(),
        "desktop_tests": {
            "passed": tests_passed,
            "tests": test_count,
            "files": file_count,
            "summary": test_summary,
        },
        "native_inventory": inventory_summary,
        "desktop_e2e": e2e_state,
        "rest_routes": rest.get("summary", {}),
        "features": rows,
        "summary": {
            "parity": parity_count,
            "partial": partial_count,
            "gaps": len(rows) - parity_count - partial_count,
            "critical_gaps": critical_gaps,
        },
    }
    (HERE / "report.json").write_text(json.dumps(report, indent=2) + "\n")

    lines = [
        "# Desktop → native scoped contract report",
        "",
        "> **Not full feature parity.** The desktop app has 27 app areas, 77",
        "> settings sections (193 files), 181 route strings and 460,644 production",
        "> LOC. This report covers only the rows below. See `TODO.md` for the",
        "> full parity queue.",
        "",
        f"Generated: `{report['generated_at']}`",
        "",
        f"- Desktop tests: **{test_count} passed / {file_count} files** "
        f"(`{'PASS' if tests_passed else 'FAIL'}`)",
        f"- Desktop e2e preferences journey: **{e2e_state}**",
        f"- Native inventory: {inventory_summary}",
        "",
    ]
    rest_summary = rest.get("summary", {})
    if rest_summary:
        lines += [
            "## Gateway REST route audit (non-blocking)",
            "",
            f"- Gateway routes: **{rest_summary.get('backend_routes', 0)}**",
            f"- Seen in the native client: **{rest_summary.get('matched', 0)}**",
            f"- Not yet used: **{rest_summary.get('gaps', 0)}**",
            f"- Native-only `/api` paths: "
            f"**{rest_summary.get('native_only', 0)}**",
            "",
            "Path-level only (HTTP verbs are not inferred from C++ call "
            "sites). See `parity/rest_routes.md`.",
            "",
        ]
    lines += [
        "| Feature | Critical | Desktop | Native | Status |",
        "| --- | --- | --- | --- | --- |",
    ]
    for row in rows:
        critical = "yes" if row["critical"] else ""
        lines.append(
            f"| {row['name']} | {critical} | {row['desktop']} | "
            f"{row['native']} | **{row['status']}** |"
        )
    lines += [
        "",
        f"**Summary:** {parity_count} parity, {partial_count} partial, "
        f"{len(rows) - parity_count - partial_count} gaps.",
        "",
    ]
    if critical_gaps:
        lines.append("## Critical gaps")
        lines.append("")
        for name in critical_gaps:
            lines.append(f"- **{name}** — no native implementation found.")
        lines.append("")
    lines += [
        "## Test cases",
        "",
        "Each row above is a parity test case. The desktop contract is the "
        "component test named in the Desktop column; native presence is the "
        "live widget inventory (or the named source marker for custom-painted "
        "controls).",
        "",
    ]
    for feature in FEATURES:
        lines.append(f"- **{feature['name']}** — {feature.get('case', '')}")
    lines += [
        "",
        "Run the desktop contract and the native inventory with:",
        "",
        "```bash",
        "python3 parity/check_parity.py",
        "```",
        "",
        "Native evidence comes from the real widget tree via "
        "`hermes_ui --static --ui-inventory`; source markers are used only for "
        "custom-painted controls that have no child widget (new chat, stop, "
        "streaming, tool rows).",
        "",
    ]
    (HERE / "report.md").write_text("\n".join(lines))

    print("\n".join(lines))
    return 1 if critical_gaps else 0


if __name__ == "__main__":
    sys.exit(main())
