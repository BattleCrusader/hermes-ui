# Desktop → native scoped contract report

> **Not full feature parity.** The desktop app has 27 app areas, 77
> settings sections (193 files), 181 route strings and 460,644 production
> LOC. This report covers only the rows below. See `TODO.md` for the
> full parity queue.

Generated: `2026-11-11T06:50:16Z`

- Desktop tests: **54 passed / 3 files** (`PASS`)
- Desktop e2e preferences journey: **passed**
- Native inventory: 112 named widgets

## Gateway REST route audit (non-blocking)

- Gateway routes: **307**
- Seen in the native client: **146**
- Not yet used: **161**
- Native-only `/api` paths: **3**

Path-level only (HTTP verbs are not inferred from C++ call sites). See `parity/rest_routes.md`.

| Feature | Critical | Desktop | Native | Status |
| --- | --- | --- | --- | --- |
| Preferences / Settings | yes | test src/app/settings/model-settings.test.tsx | inventory:settingsView | **parity** |
| Composer model picker | yes | test src/app/chat/composer/model-pill.test.tsx | inventory:modelPicker | **parity** |
| Reasoning effort control | yes | test src/app/chat/composer/reasoning-pill.test.tsx | inventory:reasoningPill | **parity** |
| Command palette |  | source markers | inventory:commandPalette | **parity** |
| Clarify requests |  | source markers | inventory:requestBar | **partial** |
| Approval requests |  | source markers | inventory:requestBar | **partial** |
| Session search |  | source markers | inventory:sessionSearch | **parity** |
| Light/dark appearance |  | source markers | source markers | **parity** |
| New chat |  | source markers | source markers | **parity** |
| Stop turn |  | source markers | source markers | **parity** |
| Streaming reply |  | source markers | source markers | **parity** |
| Tool call rows |  | source markers | source markers | **parity** |

**Summary:** 10 parity, 2 partial, 0 gaps.

## Test cases

Each row above is a parity test case. The desktop contract is the component test named in the Desktop column; native presence is the live widget inventory (or the named source marker for custom-painted controls).

- **Preferences / Settings** — Open Settings. Assert the nine config sections render with schema-driven fields and a form change writes a sparse PUT /api/config patch. Desktop also exposes provider/model/reasoning in its Model view; native exposes those in the composer pills.
- **Composer model picker** — Open the composer model pill. Assert a catalog of providers/models. Pick a model and assert config.set {key:'model', value:'<model> --provider <provider>'}.
- **Reasoning effort control** — Click the reasoning pill. Assert effort choices. Pick a level and assert config.set {key:'reasoning', value:<level>}.
- **Command palette** — Press the command-palette shortcut. Assert a filtered action list and keyboard navigation.
- **Clarify requests** — Run a turn that calls clarify. Assert the question/choices render and the chosen answer reaches the model.
- **Approval requests** — Run an approval-gated action. Assert the approval card renders and the choice reaches the model.
- **Session search** — Type in the sidebar search; assert the session rows filter.
- **Light/dark appearance** — Switch theme and assert all surfaces repaint from the light/dark tokens.
- **New chat** — Start a new chat and assert session.create opens an empty live session.
- **Stop turn** — Send a turn, press stop, and assert session.interrupt settles the run.
- **Streaming reply** — Send a turn and assert message.start/delta/complete update one assistant row.
- **Tool call rows** — Run a tool turn and assert tool.start/tool.complete render one detail row.

Run the desktop contract and the native inventory with:

```bash
python3 parity/check_parity.py
```

Native evidence comes from the real widget tree via `hermes_ui --static --ui-inventory`; source markers are used only for custom-painted controls that have no child widget (new chat, stop, streaming, tool rows).
