# Desktop → native scoped contract harness

> **Not full feature parity.** The desktop renderer has 27 app areas, 77 settings
> sections (193 settings files), 181 route strings, 307 REST routes, 190 RPC
> method names and 460,644 production LOC. This harness measures a 12-row
> contract, not the whole app.

The desktop app at `~/.hermes/hermes-agent/apps/desktop` is the contract. This
directory keeps that contract executable and measures the native Qt client
against it.

## Run

```bash
python3 parity/check_parity.py
```

It writes `parity/report.md` and `parity/report.json` and exits non-zero while a
critical feature has no native implementation.

The checker runs:

1. **Desktop component tests** for the three controls the user names:
   `model-settings.test.tsx`, `model-pill.test.tsx`, `reasoning-pill.test.tsx`.
   These are real React component tests — 54 assertions in the current tree.
2. **Desktop source contract** for every feature row in `features.json`: the
   settings route, the model pill, the reasoning pill, command palette, sidebar
   search, appearance, new chat, stop, streaming and tool rows.
3. **Native widget inventory**:
   `build/hermes_ui --static --ui-inventory` walks the live widget tree and
   prints object/accessible names. A feature is native-present only when a real
   widget is in that tree. Custom-painted controls without a child widget (new
   chat, stop, streaming, tool rows) fall back to a named source marker, and the
   report says so.

## REST route audit

```bash
python3 parity/rest_routes.py
```

This is a separate, non-blocking audit: it reads the gateway's 307 FastAPI
routes from `hermes_cli/web_routers` and the `/api/...` literals in `src/` +
`tests/`, then writes `rest_routes.md` / `rest_routes.json`. Matching is
path-level (HTTP verbs are not inferred from C++ call sites), with `{id}` /
`%1` params and query strings normalized. `check_parity.py` includes the counts
in its report but does not fail on gaps.

Current audit: **142/307 gateway paths seen, 165 gaps, 3 native-only**.

## Desktop e2e case

`e2e/parity-preferences.spec.ts` in the desktop app is the full UI journey:
open Settings, assert Provider/Model/Reasoning, open the model catalog, open
the reasoning pill. It needs the desktop app built (`npm run build`) and the
isolated mock backend; it is slower than the component contract.

```bash
cd ~/.hermes/hermes-agent/apps/desktop
npm run build
npx playwright test e2e/parity-preferences.spec.ts --reporter=line --workers=1
```

The local isolated backend can take longer than the default 90 s test timeout
on a cold start; the spec sets a 240 s timeout for that reason. The latest
local run passed (`test-results/.last-run.json` status `passed`).

## Current result

The latest generated report is `report.md`. At the time of writing:

- Desktop contract: **54 component tests pass**; the e2e preferences journey
  passes.
- Native parity: **10 features**, including Preferences, the composer model
  picker, reasoning effort, command palette, session search, theme, new chat,
  stop, streaming and tool rows.
- Partial: clarify and approval render in a docked request bar, not as inline
  transcript cards.
- **0 critical gaps**. `build/hermes_ui_tests` covers the native
  Preferences→apply and composer model/reasoning wiring.

The parity gate is green for the current contract. The next gaps are the two
partials (inline cards) plus the remaining desktop surfaces.
