# Hermes desktop → native parity TODO

Source of truth: `~/.hermes/hermes-agent/apps/desktop` plus the gateway's
`hermes_cli/web_routers`.

Measured on 2026-11-10 (regenerate with `python3 scripts/gen_feature_inventory.py`):

| Desktop surface | Count | Where |
|---|---:|---|
| Production renderer | 460,644 LOC | `apps/desktop/src` |
| App areas (`src/app/*`) | 27 | inventory §App areas |
| Settings source files (non-test) | 115 | inventory §Settings |
| Route strings (`APP_ROUTES` + session) | 12 + `/<session-id>` | `src/app/routes.ts` |
| App views | 13 | `routes.ts` |
| Keybind actions | 24 (+ contributed) | `src/lib/keybinds/actions.ts` |
| Gateway REST routes | 307 | `hermes_cli/web_routers/*.py` |
| Gateway event methods | 26 | `hermes_cli/gateway_agent.py` |

`docs/desktop-feature-inventory.md` is the generated, file-by-file feature
ledger (every settings file, route, keybind action, REST route and event
method). `parity/check_parity.py` remains a 12-row smoke test, **not** the
parity definition.

A feature is only “ported” when its desktop route/entry point, state source,
backend calls, keyboard path and a native test exist. A green scoped report is
not enough; only then is the row removed.

## Tranches

| # | Tranche | Scope | Status |
|---|---|---|---|
| 1 | Settings data pages | About, Sessions, Notifications, Keybinds | **done** — notifications now delivers OS alerts with per-kind policy and a live test |
| 2 | Providers, keys, vault, gateway | Provider accounts, API keys, custom endpoints, `/api/env`, credential vault, web access | **done** |
| 3 | Plugins, billing, updates | Plugin inventory, billing/plans, managed updates | **partial** — list + rescan + hub enable/disable/update/remove + lazy catalog install + manifest `settings_schema` forms; billing/plans and plugin deep-link pages missing |
| 4 | Cron, artifacts, capabilities | Cron jobs/runs/actions; artifact browser/preview; skills/toolsets/MCP | **partial** — cron jobs + runs + blueprints + delivery targets + create/edit, with wrapped 80%-width cards and an icon action row per task; artifacts browse/edit/save/mkdir/delete/upload/download/image preview, file browser rooted at the scoped project (home when unscoped) with refresh-safe list/preview scroll and docs-style Markdown preview; capability inventory + MCP add/test/toggle/OAuth/catalog/raw edit + skill enable/lock/edit/new + hub sources/search/preview/scan/install/update/uninstall + per-toolset provider/env/model config; connector accounts (rename/remove), tools (cached/refresh), catalog browse, list/connect + operation polling; generated HTML/SVG/code artifact browser with version history, copy/save and source toggle; generated artifact project binding missing |
| 5 | Chat parity | Full markdown, attachments/images, inline cards, message actions, session import/export | **partial** — full markdown (headings/lists/tasks/quotes/rules/tables/links/images/diff/math/mermaid/artifacts/footnotes/citations), reasoning blocks, attachment chips/thumbnails/clipboard paste/overflow drawer/removal/drag-drop, prompt history with view/edit/delete for queued and steered entries plus backend pending-admission edit/replace/cancel/steer plus ACK-gated “Inject now” into the live turn, context meter, `@` path completion, queue/steer, file dictation and gateway live-voice input, session export/import, edit/regenerate, the desktop hover action row on every message item (assistant: duration/branch/copy/copy-full/read-aloud/refresh/react; user: branch/copy/copy-full/read-aloud/edit/react; system/tool-only: copy/copy-full), right-click branch/rewind/react/delete, read-aloud, inline transcript cards, client-side prompt pinning, profile-scoped projects (backend-derived membership, sidebar scope filter, project-entry session selection/blank-chat creation, create/edit/delete, folder mutators, project cwd for new chats), right rail Project/Git/Terminal + Context/Agents/Todo/Timeline/Outputs accordion (collapsible/resizable shell splitter, project file tree with themed Markdown/HTML/image/code preview, one Git card per project repository with independent status/commit/push/AI message, PTY-backed embedded shell, prompt filtering, row jumps and generated-artifact versions), and a chat-header session search bar with context completion that owns Ctrl+F; project appearance/auto-project hide and model-visibility overlay, plus a transcript-derived generated-artifact browser in the right rail (HTML/SVG/code detection, versions, copy/save/browser/source) |
| 6 | Platform surfaces | Agents/teams, messaging onboarding, webhooks, voice, browser/computer use, HUD/pets/starmap, multi-window, quick entry | **partial** — messaging inventory/toggles/test + Telegram and WhatsApp QR onboarding, Bots, Webhooks, event-driven agents panel with steer/stop/handoff, read-aloud TTS with output-device selection, gateway live microphone input, starmap, quick entry, computer-use diagnostics/grant, terminal-backend picker, desktop-pet overlay, a detachable chat window, and browser/CDP automation (`browser.manage` discovery plus a native CDP client for screenshots and coordinate input, with real-profile consent); GPT-Live/WebRTC voice sessions, floating HUD/wake indicator and the remaining multi-window panes still missing |
| 7 | Remaining Settings subpages | Full appearance, 77 sections, special editors, searchable pickers, deep links | **partial** — theme/font scale + display appearance (density, width, timestamps, reasoning, fonts, translucency, animations, code scale); marketplace themes and 68 subpages/pickers missing |
| 8 | Parity audit | All routes, 307 REST paths, 26 events | **partial** — inventory + REST route audit generated (`parity/rest_routes.py`: 146/307 paths seen, 161 gaps, 3 native-only); event and route rows still enforced by hand |

## Status update — 2026-11-13 (relay follow-up)

**Only the active profile keeps a control socket, and unserved profiles stop
retrying.**

- [x] Profile switches now retire the previous relay instead of leaving one
  control socket per visited profile. Retirement is asynchronous, so a dead
  endpoint cannot freeze the switch on a ticket timeout; `Stop()` drains the
  retired threads before exit.
- [x] The profile roster is filtered by `served_profiles` from the gateway's
  `identify` reply. `/api/status.profiles` lists every profile on disk, so
  profiles the multiplexer does not serve (for example `local-llama-test`)
  were starting workers that could only answer `bootstrap binding rejected`.
  Older gateways without `served_profiles` keep the status roster.
- [x] Relay auth failures log once per backoff run instead of once per retry.
- [x] Verification: app run with saved profile `ceo` shows no
  `local-llama-test` or relay lines; full suite **9/9 passed** in 36.94 s.

## Status update — 2026-11-13 (seventy-fourth pass)

**Profile defaults can reclaim a stale chat, and Stop no longer queues behind
REST.**

- [x] **Stale per-session model recovery.** A chat can carry a model override
  that differs from the configured `model.default` (the `poolside/laguna`
  sessions are the observed case). The backend snapshots that override as the
  session "primary", so a later fallback restore announces it as primary and
  the configured DeepSeek model as the deactivated fallback. The composer's
  model menu now has **Use profile default**, backed by
  `ProfileWorker::UseDefaultModel()` and the `/api/model/options` root
  defaults. Saving `model.provider` / `model.default` in Settings also moves
  the open chat to the new values, so the save is authoritative.
- [x] **Explicit session-scoped picks.** `ProfileWorker::SetModel` appends
  `--session` to the model mutation, matching desktop's canonical picker
  mutation. A composer or settings pick can no longer be interpreted as a
  profile-default rewrite.
- [x] **Instant Stop.** The profile worker's `Interrupt` was a queued slot on
  the same thread as blocking REST calls, so a 20 s settings/model fetch could
  delay `session.interrupt` by the same amount. `InterruptRelay` owns a second
  `GatewaySocket` on its own `QThread`, started lazily for the active profile.
  Stop sends `session.interrupt` there immediately; the newest request waits at
  most 10 s for the connection to open, then expires so it cannot stop a later
  turn.
- [x] **Control sockets stay control-only.** `GatewaySocket` now accepts a
  `server_requests` flag. The relay leaves it false, so the gateway never
  routes an approval or clarify prompt to a connection that cannot answer it.
- [x] Relay-only delivery: the worker `Interrupt` path remains only as a
  fallback when no session/relay is available, avoiding the delayed worker
  reading a newer `execution_generation_` and stopping the wrong turn.
- [x] Full native suite: **9/9 passed** in 40.20 s.

## Status update — 2026-11-13 (seventy-third pass)

**Work status moved to the footer; the model pages became real editors.**

- [x] **Footer owns live work state.** The composer no longer grows a status
  line. `StatusBar` shows `Working · 2m 03s` (h/m/s formatting), queues a
  `· N queued` suffix, and keeps the elapsed timer repainting during long
  reasoning. Centre precedence is transient → voice activity → work → normal.
  Clicking a queued work state opens the queued-prompts review.
- [x] **Model settings are editable.** The Model/Main subpage now has
  provider, default-model and reasoning-effort controls backed by
  `model.provider`, `model.default` and `agent.reasoning_effort`. The provider
  is recovered from the catalog when `/api/config` normalizes the model
  section to a bare string.
- [x] **Fallbacks are structured rows.** Provider/model/reasoning-effort cards
  with add/remove, editable known options and custom text; extra keys such as
  `base_url` survive edits. The hidden `fallback_providers` field remains for
  compatibility.
- [x] **Auxiliary and MoA pages read their real routes.** Auxiliary shows every
  task with a friendly label, provider/model pickers and per-task Save to
  `/api/model/set`; MoA shows active/default presets, references and aggregator
  or an explicit empty state. Both use lazy page loads.
- [x] **Gateway, plugins and updates diagnostics.** Gateway separates process
  state from the web-app listener; plugin fetches use a 60 s (catalog 120 s)
  timeout and label every failing source; update actions surface action/process
  ids, server messages and errors; capabilities expand read-only tool lists.
- [x] **Visible models ergonomics.** Dialog widened to 520 px, 18 px
  indicators, collapsible routers with only the composer's current provider
  expanded, a **Refresh models** button that busts the gateway cache and
  reopens the dialog, and provider-header select-all retained.
- [x] **Composer model picks survive refreshes.** `ProfileWorker` pins an
  explicit pick until the backend session row or model response echoes it, and
  gates root model defaults so a poll cannot reset the composer.
- [x] Regressions: status-bar formatting/queue click, model main edits,
  auxiliary save/MoA render, provider collapse/refresh, fallback rows.
- [x] Full native suite: **9/9 passed** in 32.09 s.

## Status update — 2026-11-13 (seventy-third pass)

**Session switches always open at the latest reply.**

- [x] **Root cause:** `ChatView::SetSnapshot` only followed the bottom when the
  previous transcript was already within 6 px of its end. Switching from a
  session the user had scrolled up left the new transcript at the same stale
  offset, often halfway through the conversation.
- [x] **Fix:** track the selected session id in `ChatView`. When it changes,
  re-enable follow-bottom and scroll to the end regardless of the previous
  offset. Streaming in the newly opened session stays pinned to the bottom.
- [x] **Regression coverage:** `sessionSwitchScrollsToLatestReply` renders two
  long sessions, leaves the first at the top, switches, and asserts the
  scrollbar lands at its maximum.
- [x] Native chat suite: **31/31 passed**; full suite: **10/10 passed**.

## Status update — 2026-11-13 (seventy-second pass)

**Router headers select or clear every model in one click.**

- [x] **Provider-level checkbox.** Each router name in the Visible models
  dialog is now a tri-state checkbox. A mixed router shows a dash, all-visible
  shows a check, and all-hidden is empty.
- [x] **Predictable group action.** Clicking a partial or empty header selects
  every model under it; clicking a fully selected header clears the group.
  Child toggles immediately re-derive the header state, and **Show all** resets
  every header to checked.
- [x] **Themed partial state.** Added `resources/dash.svg` and an
  `indicator:indeterminate` rule so the mixed state stays legible on the dark
  dialog surface instead of falling back to the platform indicator.
- [x] **Regression coverage.**
  `modelVisibilityProviderToggleSelectsGroup` covers mixed/empty/checked
  headers, child-to-header updates, the second router, and **Show all**.
- [x] Full native suite: **9/9 passed** in 33.63 s.

## Status update — 2026-11-13 (seventy-third pass)

**Session switches always open at the latest reply.**

- [x] **Root cause:** `ChatView::SetSnapshot` only followed the bottom when the
  previous transcript was already within 6 px of its end. Switching from a
  session the user had scrolled up left the new transcript at the same stale
  offset, often halfway through the conversation.
- [x] **Fix:** track the selected session id in `ChatView`. When it changes,
  re-enable follow-bottom and scroll to the end regardless of the previous
  offset. Streaming in the newly opened session stays pinned to the bottom.
- [x] **Regression coverage:** `sessionSwitchScrollsToLatestReply` renders two
  long sessions, leaves the first at the top, switches, and asserts the
  scrollbar lands at its maximum.
- [x] Native chat suite: **31/31 passed**; full suite: **10/10 passed**.

## Status update — 2026-11-13 (seventy-second pass)

**Kanban follows light/dark theme switches.**

- [x] **Root cause:** `PagesView` constructs and owns `KanbanView`, but its
  `SetTheme` only restyled the pages chrome. The custom-painted board kept the
  palette from construction, so switching to dark left a bright white board
  inside the dark workspace.
- [x] **Fix:** `PagesView::SetTheme` now calls `KanbanView::SetTheme`, which
  restyles the toolbar, canvas, and any open task dialog from the active
  tokens.
- [x] **Regression coverage:** the Kanban test target now links `PagesView` and
  asserts the canvas background tracks `Theme::Light()` → `Theme::Dark()`.
- [x] **Pixel verification:** live dark screenshot samples changed from
  `#FFFEFF` (light canvas) to `#494F59` (dark column surface) on the board.
- [x] Full native suite: **10/10 passed** in 17.94 s.

## Status update — 2026-11-13 (seventy-first pass)

**Gateway restarts no longer leave the native UI on a stale instance ticket.**

- [x] **Root cause.** `PermissionError: stale runtime instance` comes from the
  gateway control socket rejecting a ticket minted for the previous gateway
  instance. A restart succeeds, but `ProfileWorker`, `GatewayHttp` and
  `GatewaySocket` kept the old `instance_id`, so the settings page refetch and
  every later request failed until the whole app was restarted.
- [x] **Endpoint rediscovery on refusal.** `GatewayHttp::MintTicket()` detects
  the stale-instance error, reruns `DiscoverEndpoint()`, and retries the ticket
  once. JSON, multipart upload and file download all use the same path, and
  each recomputes its URL after discovery in case the API origin moved.
- [x] **WebSocket reconnect too.** `GatewaySocket::Connect()` performs the same
  detect/rediscover/retry cycle before backing off, so the live event stream
  reattaches to the new gateway instance instead of retrying the old ticket
  forever.
- [x] **Regression coverage.** `staleRuntimeErrorRequiresRediscovery` covers
  the error classifier. `staleInstanceRediscoveryRetriesRequest` points a live
  `GatewayHttp` at a deliberately stale instance id and verifies a normal
  `/api/config` request still succeeds after rediscovery.
- [x] Full native suite: **9/9 passed** in 33.47 s.

## Status update — 2026-11-13 (seventy-third pass)

**Session switches always open at the latest reply.**

- [x] **Root cause:** `ChatView::SetSnapshot` only followed the bottom when the
  previous transcript was already within 6 px of its end. Switching from a
  session the user had scrolled up left the new transcript at the same stale
  offset, often halfway through the conversation.
- [x] **Fix:** track the selected session id in `ChatView`. When it changes,
  re-enable follow-bottom and scroll to the end regardless of the previous
  offset. Streaming in the newly opened session stays pinned to the bottom.
- [x] **Regression coverage:** `sessionSwitchScrollsToLatestReply` renders two
  long sessions, leaves the first at the top, switches, and asserts the
  scrollbar lands at its maximum.
- [x] Native chat suite: **31/31 passed**; full suite: **10/10 passed**.

## Status update — 2026-11-13 (seventy-second pass)

**Kanban follows light/dark theme switches.**

- [x] **Root cause:** `PagesView` constructs and owns `KanbanView`, but its
  `SetTheme` only restyled the pages chrome. The custom-painted board kept the
  palette from construction, so switching to dark left a bright white board
  inside the dark workspace.
- [x] **Fix:** `PagesView::SetTheme` now calls `KanbanView::SetTheme`, which
  restyles the toolbar, canvas, and any open task dialog from the active
  tokens.
- [x] **Regression coverage:** the Kanban test target now links `PagesView` and
  asserts the canvas background tracks `Theme::Light()` → `Theme::Dark()`.
- [x] **Pixel verification:** live dark screenshot samples changed from
  `#FFFEFF` (light canvas) to `#494F59` (dark column surface) on the board.
- [x] Full native suite: **10/10 passed** in 17.94 s.

## Status update — 2026-11-13 (seventy-first pass)

**Typing and drag-selection stay responsive on long conversations.**

- [x] **Plain keystrokes skip the shortcut converter.** The application-wide
  event filter called `QKeyEvent::shortcut()` and `QKeyCombination` for every
  key, including ordinary letters. Character input now returns before any
  shortcut conversion; modified keys still reach bindings, and a focused text
  editor keeps its own keys.
- [x] **The composer stops flattening the draft on every key.** Slash and
  mention detection now inspect only the cursor's text block and use the block
  count for the empty check, instead of `toPlainText()` / `contains('\n')` over
  the whole draft. The paste probe only calls `matches(QKeySequence::Paste)`
  when the key can actually be a paste.
- [x] **Selection repaint is clipped to the drag band.** `PaintSelection` used
  to rebuild every highlight rectangle in the transcript on each mouse move,
  even though Qt asked only for the newly dirtied band. It now binary-searches
  the vertically ordered text runs and builds only fragments intersecting the
  painter clip; the full selection bounds are computed only on the rare
  invalid-release fallback.
- [x] **Regression coverage.** Full native suite: **10/10 passed** in 19.12 s,
  including wrapped-line/URL selection, streaming-update selection retention,
  and detached-window composer sharing.

## Status update — 2026-11-13 (seventieth pass)

**The Visible models dialog now themes its scrolling surface.**

- [x] **White viewport in dark mode.** The dialog styled `QDialog` but left its
  `QScrollArea` viewport on `QPalette::Base`, which still resolved to the light
  platform base. Light checkbox labels therefore sat on a near-white strip.
- [x] **Explicit palette and stylesheet.** The dialog, scroll area, viewport and
  content widget all set `Window`/`Base` to `theme.surface`; the dialog QSS
  paints the scroll area, content, labels, checkboxes, indicators and buttons.
  The Save button uses the accent fill with white text.
- [x] **Self-contained control styling.** Checkbox indicators and buttons no
  longer depend on the application stylesheet, so the dialog remains readable
  even when built standalone in tests or another shell.
- [x] **Regression coverage.** The model-visibility test now builds a dark
  dialog and asserts the dialog/content `Window` and scroll viewport `Base`
  colors equal `Theme::Dark().surface`, plus surface/foreground QSS values.
- [x] Dark-theme screenshot OCR reads every provider, model, and button; the
  dialog is uniformly dark instead of a dark frame around a white list.
- [x] Full native suite: **9/9 passed** in 28.20 s.

## Status update — 2026-11-13 (seventy-third pass)

**Session switches always open at the latest reply.**

- [x] **Root cause:** `ChatView::SetSnapshot` only followed the bottom when the
  previous transcript was already within 6 px of its end. Switching from a
  session the user had scrolled up left the new transcript at the same stale
  offset, often halfway through the conversation.
- [x] **Fix:** track the selected session id in `ChatView`. When it changes,
  re-enable follow-bottom and scroll to the end regardless of the previous
  offset. Streaming in the newly opened session stays pinned to the bottom.
- [x] **Regression coverage:** `sessionSwitchScrollsToLatestReply` renders two
  long sessions, leaves the first at the top, switches, and asserts the
  scrollbar lands at its maximum.
- [x] Native chat suite: **31/31 passed**; full suite: **10/10 passed**.

## Status update — 2026-11-13 (seventy-second pass)

**Kanban follows light/dark theme switches.**

- [x] **Root cause:** `PagesView` constructs and owns `KanbanView`, but its
  `SetTheme` only restyled the pages chrome. The custom-painted board kept the
  palette from construction, so switching to dark left a bright white board
  inside the dark workspace.
- [x] **Fix:** `PagesView::SetTheme` now calls `KanbanView::SetTheme`, which
  restyles the toolbar, canvas, and any open task dialog from the active
  tokens.
- [x] **Regression coverage:** the Kanban test target now links `PagesView` and
  asserts the canvas background tracks `Theme::Light()` → `Theme::Dark()`.
- [x] **Pixel verification:** live dark screenshot samples changed from
  `#FFFEFF` (light canvas) to `#494F59` (dark column surface) on the board.
- [x] Full native suite: **10/10 passed** in 17.94 s.

## Status update — 2026-11-13 (seventy-first pass)

**Typing and drag-selection stay responsive on long conversations.**

- [x] **Plain keystrokes skip the shortcut converter.** The application-wide
  event filter called `QKeyEvent::shortcut()` and `QKeyCombination` for every
  key, including ordinary letters. Character input now returns before any
  shortcut conversion; modified keys still reach bindings, and a focused text
  editor keeps its own keys.
- [x] **The composer stops flattening the draft on every key.** Slash and
  mention detection now inspect only the cursor's text block and use the block
  count for the empty check, instead of `toPlainText()` / `contains('\n')` over
  the whole draft. The paste probe only calls `matches(QKeySequence::Paste)`
  when the key can actually be a paste.
- [x] **Selection repaint is clipped to the drag band.** `PaintSelection` used
  to rebuild every highlight rectangle in the transcript on each mouse move,
  even though Qt asked only for the newly dirtied band. It now binary-searches
  the vertically ordered text runs and builds only fragments intersecting the
  painter clip; the full selection bounds are computed only on the rare
  invalid-release fallback.
- [x] **Regression coverage.** Full native suite: **10/10 passed** in 19.12 s,
  including wrapped-line/URL selection, streaming-update selection retention,
  and detached-window composer sharing.

## Status update — 2026-11-13 (seventieth pass)

**Copy and highlight now share one selection model, including URLs.**

- [x] **Why a URL selection could copy the next glyph.** Copy and paint used
  separate inline range loops. Plain runs also stored the full unclipped
  string even though `drawText`/`DrawElidedText` painted only a prefix or an
  ellipsized summary. The copy range could therefore include characters that
  were never highlighted.
- [x] **Single source of truth.** `TranscriptWidget::SelectionFragments()`
  computes the clamped per-run `[from,to)` ranges; `SelectedText()` and
  `SelectionRects()` both consume it, so copy and highlight cannot diverge.
- [x] **Plain runs contain only visible glyphs.** `VisiblePlainText()` clips
  left-aligned prefixes, right-aligned suffixes and elided tool summaries to
  the painted width. `PlainTextOrigin()` is shared by hit testing and paint.
- [x] **Nearest-boundary hit testing.** `PlainOffsetAt()` now maps the pointer
  to the nearest character boundary instead of flooring, matching document
  `hitTest` behavior and avoiding one-character under/over-selection.
- [x] **Double-click domains as one token.** `ExpandUrlWord()` expands
  `pinderlog.com`, `www.` URLs, schemes and file names across punctuation while
  trimming sentence punctuation.
- [x] **Regression coverage.** `transcriptSelectionMatchesVisibleUrl` asserts
  selecting `pinderlog.com` copies exactly that string, the highlight starts
  and ends on the URL glyphs, and a double-click selects the whole domain.
  `transcriptPlainSelectionMatchesVisibleText` proves clipped header text and
  select-all cannot reach an unpainted suffix.
- [x] Full native suite: **9/9 passed** in 28.71 s.

## Status update — 2026-11-13 (sixty-ninth pass)

**Transcript selection now paints every wrapped visual line.**

- [x] **Root cause found.** `QTextLine::cursorToX()` takes a position in
  block/paragraph coordinates, not line coordinates. `PaintSelection` passed
  `offset - line.textStart()`, so every wrapped continuation line resolved both
  endpoints to `x = 0` and painted nothing. Only the first visual line of a
  wrapped paragraph had a highlight, which made the background appear
  unrelated to the selected text.
- [x] **One geometry source for paint and tests.** Selection rectangles are now
  built by `TranscriptWidget::SelectionRects()`; `PaintSelection` just fills
  them, and `ChatView::SelectionRectsForTest()` exposes the same rectangles.
- [x] **Multi-block documents included.** `cursorToX` is now fed
  `offset - blockStart`, so later paragraphs in the same `QTextDocument` are
  highlighted as well as wrapped lines in the first paragraph.
- [x] **Regression coverage.** `codeCardContainsWrappedLines` now drag-selects
  a wrapped 420-character code token through the final `int done = 1;` line and
  asserts one non-empty highlight per visual line, correct containment, and
  descending highlight tops.
- [x] Pixel probe: selection through the wrapped code block now paints 7
  highlight bands matching the 7 visual lines, instead of one band.
- [x] Full native suite: **9/9 passed** in 29.89 s.

## Status update — 2026-11-13 (sixty-eighth pass)

**Auto-discovered projects can now be saved and edited.**

- [x] **Why “Edit details” was missing.** Auto rows are derived from a folder
  and session history; they have no `projects` row, so there is nothing for
  `projects.update` to address. The ceo profile’s `SSC` row is one of these
  auto entries, even though the default profile has an explicit SSC project.
- [x] **New “Save as project…” action** on auto rows with a filesystem path.
  It opens the normal create dialog prefilled with the auto label and path, so
  name, description, folders, primary folder, and “use after creating” can be
  set in one step. The created explicit project replaces the auto row.
- [x] Adoption keeps an active SSC scope pointed at the new explicit id, and
  the status line says “Project saved” instead of “Project created”.
- [x] `ProjectDialog::SetCreatePrefill` ignores edit-mode dialogs, so an
  existing project cannot be silently retargeted. Unit coverage added.
- [x] Full native suite: **9/9 passed** in 29.56 s.

## Status update — 2026-11-13 (sixty-seventh pass)

**Startup now shows projects immediately, and project session counts match the sidebar.**

- [x] **Project rows no longer wait for the full tree.** `projects.list` is
  painted as explicit project rows while the expensive `projects.tree` scan
  (500 sessions, git probes, auto discovery) is still running. The
  authoritative tree replaces those rows when it lands.
- [x] **The 15 s startup stall is gone.** The first project RPC can beat the
  profile socket and return `Gateway is not connected`. A failed fetch no
  longer marks the project state loaded, so retries use 500 ms until the first
  success instead of jumping to the 15 s steady-state interval. The expected
  connection-race error no longer flashes “Projects unavailable”.
- [x] **Project requests start before the snapshot rebind** on profile
  activation, and the list/tree callbacks share one success definition.
- [x] **Session fetch matches the tree’s source window.** `/api/sessions` is
  paged at the API maximum of 100 up to 500 rows, excludes
  `acp,cron,kanban,oneshot,subagent,tool`, dedupes pinned back-fills, and keeps
  partial pages on later-page failures. A fresh ceo run loaded 62 sessions and
  all 15 SSC auto-project ids, fixing the “15 sessions but none listed” row.
- [x] `ProjectListNodes` has a unit test for quick rows and archived filtering.
- [x] Fresh-profile screenshots show project rows by ~2 s instead of after the
  15 s refresh interval. Full native suite: **9/9 passed** in 28.49 s.

## Status update — 2026-11-12 (sixty-sixth pass)

**Live sub-agents are visible and steerable from the right rail.**

- [x] **Live roster, not a snapshot.** The Agents accordion now renders one
      expandable card per delegated child, ordered by the delegation tree.
      Cards show status, goal, elapsed time, task/tool/token/cost metadata, and
      the latest stream line without opening Settings.
- [x] **In-place updates preserve steering.** Cards are keyed by subagent id
      and update labels/stream rows in place instead of rebuilding the card.
      A half-typed steer draft and focus survive incoming `subagent.*` events.
- [x] **Steer and stop where the work is.** Expanding a card reveals the last
      eight stream entries, an owner-scoped steer field, Steer and Stop
      buttons, and queued/stopped/error feedback. Controls disappear on
      terminal status.
- [x] **Right session scope.** `SubagentProgress::owner_session_id` records the
      dispatching session; `subagent.steer` / `subagent.interrupt` use it
      rather than the child's stored `child_session_id`. Settings → Agents uses
      the same fallback.
- [x] **New work announces itself.** A newly seen live agent auto-expands the
      Agents section (never while the rail is hidden/collapsed) and updates the
      section title to `Agents · N live`.
- [x] **Regression check.** `rightRailSteersAndStopsLiveAgents` covers live
      labels, expansion, owner/session/steer arguments, queued feedback, draft
      preservation across snapshots, stop arguments, and terminal cleanup.
      Full native suite 9/9 in 31.35 s.

## Status update — 2026-11-12 (sixty-fifth pass)

**macOS notifications now come from a real app bundle.**

- [x] **The missing piece was bundle identity, not delivery policy.** A raw
      Mach-O executable has `NSBundle.mainBundle.bundleIdentifier == nil`, so
      `NSUserNotificationCenter` is nil. Embedding `Info.plist` in
      `__TEXT,__info_plist` fixed the nil center, but Notification Center still
      rejected the process as "app not found".
- [x] **Build `hermes_ui.app`.** `hermes_ui` is now a `MACOSX_BUNDLE` target
      with `resources/Info.plist` as `MACOSX_BUNDLE_INFO_PLIST`
      (`com.nousresearch.hermes.ui`, display name "Hermes").
- [x] **Keep `./build/hermes_ui` working.** A small generated launcher at
      `build/hermes_ui` executes
      `build/hermes_ui.app/Contents/MacOS/hermes_ui`. A symlink is not enough:
      `NSBundle` does not resolve it. README now documents the layout.
- [x] **Explain the failure path.** `MacShowNotification` reports the app
      bundle when the notification center is unavailable, instead of a generic
      macOS error.
- [x] **Regression check.** New `macos_bundle_identity` CTest asserts the
      bundle exists, its `CFBundleIdentifier`/`CFBundleExecutable` are correct,
      and the launcher is present. Full suite 9/9 in 34.89 s.
- [x] **Verified end to end.** Clicking `notifyTest` on
      `settingsPage:notifications` logged `Presenting … as banner` for
      `com.nousresearch.hermes.ui`. The first post asks for permission; allow
      Hermes in System Settings → Notifications if the prompt is dismissed.

## Status update — 2026-11-12 (sixty-fourth pass)

**Entering a project keeps conversations project-scoped.**

- [x] **A selected local chat is no longer a wildcard.** The sidebar only
      inserts the selected row under a scoped project when it is tagged with
      that project (`SessionSummary::project_id`), and a real project in
      `projects.tree` wins over a stale tag. Home is the only exception: it is
      the backend's unclaimed bucket, so its temporary ownership does not hide
      a fresh project chat.
- [x] **Project entry chooses a conversation.** `ResolveProjectEntry()` keeps
      the current session when the project owns it, otherwise selects the most
      recent matching session, then the tree's id order. A project with no
      known sessions asks the main window to start a blank chat; a tree row
      that claims history but publishes no ids does not fabricate one.
- [x] **The blank chat is created in the project.** `MainWindow` passes the
      project id through `Supervisor`/`ProfileWorker`/`session.create`, so the
      local placeholder is tagged before `projects.tree` catches up. The tag is
      cached and merged across 10 s session polls.
- [x] **Repeated clicks cannot create duplicates.** Auto-create is limited to a
      real scope change and guarded by the pending project plus a 5 s throttle.
      The explicit “New chat” action uses `SetProjectScope(..., false)` and
      creates exactly one chat.
- [x] **Tests:** new sidebar test covers another project's selected chat, the
      Home bucket, and a stale tag; project tests cover select/keep/start and
      the no-ids guard; the cache round-trip asserts `project_id`. Full suite
      8/8 in 14.35 s.

## Status update — 2026-11-12 (sixty-third pass)

**Make the right-rail file browser readable and resizable.**

- [x] **Show the file columns.** The `Project` tree now displays the
      `Name`/`Size`/`Type`/`Date Modified` header instead of hiding it, with
      themed header cells and a 10 px DemiBold label font.
- [x] **Every column is user-resizable.** All sections use
      `QHeaderView::Interactive` with a 48 px minimum; the initial widths are
      240/80/110/150 and the last section no longer stretches.
- [x] **Long paths remain reachable.** The tree's horizontal scrollbar is
      `ScrollBarAsNeeded` (previously forced off), so widening a column or
      viewing more columns scrolls instead of silently clipping.
- [x] **Tests:** the right-rail project test asserts the visible header,
      interactive resize mode for every column, a changed Name width, and a
      live horizontal scrollbar range. Full suite 8/8.

## Status update — 2026-11-12 (sixty-second pass)

**Show every repository in a multi-folder project.**

- [x] **Repo roots come from `projects.tree`.** `ProjectNode` now keeps each
      repo row's `id`/`label`/`path`; `ProjectGitRepositories()` dedupes them by
      absolute path and falls back to project folders containing `.git`, then
      to the selected working directory when it is itself a worktree.
- [x] **Git is one card per repository.** The right rail builds independent
      status, commit message, Refresh, AI Generate, Commit and Commit & Push
      controls for each repo, with per-repo serial and AI-generation fences so
      stale callbacks cannot write into a rebuilt card.
- [x] **Main window passes the scoped repo list** from `projects.tree` into
      `RightRail::SetProjectContext`; the first card keeps the legacy object
      names and later cards use `_1`, `_2` suffixes.
- [x] **Tests:** new two-repo test verifies independent status refresh, commit,
      AI message context, and cross-repo isolation; project parsing asserts
      repo label/path. Full suite 8/8.

## Status update — 2026-11-12 (sixty-first pass)

**Make code cards readable on every theme.**

- [x] **Code text uses the theme foreground again.** The `<pre>` style wrapped
      `font-family` in raw double quotes inside a double-quoted `style`
      attribute. That terminated the attribute before `color`, so Qt fell back
      to black text — invisible on dark cards. The family is now quoted with
      `&quot;`, and a regression test asserts the first code fragment paints in
      `theme.code_text`.
- [x] **Code colours meet contrast minimums.** `EnsureCodeContrast()` keeps body
      text at 7:1 and comments/keywords/strings/numbers at 4.5:1 against both
      the code body and header. It runs for the built-in light/dark themes and
      every preset; `scripts/gen_theme_presets.py` emits the same call.
- [x] **Header labels stop disappearing.** The language, Preview and Copy labels
      use the contrast-checked `code_comment` tone instead of the UI tertiary
      colour, which was nearly invisible on the darker header strip.
- [x] **Tests:** chat 26 (`codeCardContainsWrappedLines` now checks foreground),
      settings 37 (all built-in/preset themes checked for body, token and header
      contrast), full suite 8/8. Formatting clean.

## Status update — 2026-11-12 (sixtieth pass)

**Keep each session's model/reasoning in the composer and stop code cards
clipping wrapped lines.**

- [x] **Session runtime survives selection and restart.** `SessionSummary`
      now carries `reasoning_effort`, the on-disk session cache round-trips it,
      and cached selections restore model/provider/effort before the first
      gateway answer. Live `session.info`, `SetModel` and `SetReasoning` also
      write back to the matching summary, so the next 10 s `/api/sessions`
      poll cannot blank the composer. Empty list fields are merged from the
      previous/cached row rather than overwriting known runtime state.
- [x] **Code cards size to the laid-out document.** Wrapped source lines used
      the raw newline count, so long tokens painted below the card and
      selection started above the text. The card now uses
      `QTextDocument::size().height()`, and `BlockTextRect` includes the
      28 px header + 7 px inset used by painting. Code bodies are clipped to
      the rounded card.
- [x] **Code uses the configured mono family.** The `<pre>` no longer pins
      `JetBrains Mono` or `12px`; it inherits the document's `MonoFont()`
      (which already carries the appearance code scale) and only overrides the
      family with `theme.mono_family`.
- [x] **Fixed-size flow rows keep their height.** `FlowLayout` now compares
      widget constraints instead of `QWidgetItem`'s stale style maximum and
      sets widget geometry directly, so fixed-height buttons no longer
      collapse/centre in a row. This also fixes the `heightForWidth()`
      regression in the preferences suite.
- [x] **Native tests.** Added `codeCardContainsWrappedLines` (chat now 26),
      extended the session-cache round-trip to assert reasoning effort (ui now
      44), and repaired `flowLayoutWrapsRowsAtNarrowWidths`.
- [x] **Suite.** Full native suite green: 8/8 targets, 28.7 s. Formatting
      clean.
- [ ] **Manual check on a real display:** switch sessions with different
      models/reasoning levels, restart, and confirm the composer matches the
      selected session; open a transcript with a long fenced line and select
      across it.

## Status update — 2026-11-12 (fifty-ninth pass)

**Fill out the native Appearance page and apply it live.**

- [x] **One appearance contract.** New header-only `src/appearance_prefs.h`
      parses `native.font_scale` plus `appearance.*` keys, clamps scales/widths/
      opacity, and applies font families, scale and user-bubble transparency to
      the base `Theme`. Clearing a font restores the preset family, and setting
      transparency back to zero restores the preset bubble color.
- [x] **Native Appearance page.** Added theme, UI scale preset + precise scale,
      chat text scale, UI/monospace font pickers, session density, window
      opacity, message width, timestamps, reasoning default, user-bubble
      transparency, animations and code scale. Every control has a stable
      `appearance*` object name, and marketplace themes are explicitly noted as
      unsupported by the native renderer rather than faked.
- [x] **Live application, including startup.** `SettingsView::on_appearance`
      reloads the helper. `MainWindow::ApplyAppearancePreferences` applies the
      window opacity, sidebar density, chat width/timestamps/reasoning/
      animations/text scale/code scale and theme, and runs after `BuildUi()` as
      well as on every preference change. Existing behaviour is the default.
- [x] **Transcript actually reflows.** `ChatView`/`TranscriptWidget` gained
      explicit appearance setters; `SetShowTimestamps` rebuilds selection runs,
      text/code scale rebuilds layout, and reasoning defaults clear per-message
      overrides. Paragraph HTML no longer hard-codes 13px, which had made chat
      text scale a no-op.
- [x] **Density is painted, not a stylesheet trick.** `Sidebar` uses 30/36/52px
      row heights and 2/4/5px gaps for compact/comfortable/detailed; detailed
      rows paint a second preview line while the whole row stays clickable.
- [x] **Native tests.** Settings tests cover page persistence, defaults and
      helper clamping/theme application (now 37); chat tests cover width,
      timestamps, reasoning, text/code scale and animation state (now 25);
      sidebar tests cover density row heights and preview containment (now 11).
- [x] **Suite.** Release-max full suite green: 8/8 targets, 16.95 s.
      Formatting clean.
- [ ] **Manual check on a real display:** change density, width, fonts,
      opacity, bubble transparency, timestamps, reasoning and code scale;
      confirm they update immediately and survive a restart before Settings is
      opened.

## Status update — 2026-11-12 (fifty-eighth pass)

**Show queued/steered prompts in the transcript and inject them now.**

- [x] **Processed admissions become chat rows once.** `PendingPromptTracker`
      treats the first pending snapshot after select/resume as a baseline and
      announces only queued rows that enter `started`/`steering`. A dropped
      queued row is retained briefly for a `message.start` fallback; canceled
      or replaced admissions are explicitly forgotten so stale snapshots cannot
      resurrect them. `AppendUserPromptLocked` closes any streaming assistant
      first, so later deltas cannot render above the inserted user bubble.
- [x] **Normal sends do not duplicate.** A normal `prompt.submit` already
      renders its user row optimistically, so its admission is claimed as
      surfaced; queued sends stay announceable and appear exactly when the
      gateway starts them. Failed steers are never replayed as a fresh prompt.
- [x] **Steer callbacks are trustworthy.** `ProfileWorker::SteerWithCallback`
      reports acceptance/failure, and `Supervisor::Steer(..., done)` marshals
      that callback onto the UI thread with a token map. The composer records
      steered history only after success and restores the draft on failure.
- [x] **Inject now, from either queue.** Local queued rows call the live steer
      path and are removed only after the ACK; steered history can be injected
      again without changing its stored copy. Server queued rows call
      `prompt.steer_queued`, then claim and append the admission through
      `RecordInjectedPrompt`. The button keeps the `queuedPromptSteer` object
      name, now labelled “Inject now”.
- [x] **The modal queue refreshes live.** A 400 ms content-signature timer
      updates the dialog while it is open, so processed or canceled rows
      disappear without rebuilding on no-op snapshots or wiping an edit.
      Cancel/replace success also calls `ForgetPendingAdmission`. The dialog
      pins the profile/session it opened on, and a local inject ACK retires the
      queue entry even if the modal was dismissed mid-flight.
- [x] **Native tests.** Added `pendingPromptTrackerAnnouncesOnlyProcessedQueueRows`
      (protocol now 18) and `promptHistoryInjectsLocalAndServerQueues` (ui now
      44), covering ACK-gated removal, failure retention, steered re-injection,
      server injection, and duplicate-free refresh.
- [x] **Suite.** Release-max full suite green: 8/8 targets (≈13–19 s).
      Formatting clean.
- [ ] **Manual check on a real display:** queue a prompt behind a running
      turn, confirm the badge/dialog row clears when it starts, the user bubble
      appears once in the transcript, and “Inject now” promotes a queued row
      immediately.

## Status update — 2026-11-12 (fifty-seventh pass)

**Give Release builds the safe maximum and leave Debug untouched.**

- [x] **Release-only optimization.** `CMakeLists.txt` now adds `-O3`,
      `-funroll-loops`, host CPU tuning (`-mcpu=native` on arm64,
      `-march=native` on x86) and thin LTO only through Release-only generator
      expressions / `CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE`.
      `HERMES_MAX_OPTIMIZATION` defaults on; pass `OFF` for portable or
      sanitizer builds.
- [x] **Debug stays debuggable.** Debug flags remain `-g` with no LTO/native
      tuning. Verified by configuring a throwaway Debug tree; its
      `CXX_FLAGS` contain none of the release flags.
- [x] **Release build and suite.** `build/` is now `Release` with
      `-O3 -DNDEBUG -flto=thin -mcpu=native -funroll-loops`; full suite green
      8/8.

## Status update — 2026-11-12 (fifty-sixth pass)

**Make terminal selection, copy and paste work like a real terminal.**

- [x] **Single-line selection no longer copies only one character.** The
      same-row branch in `SelectedText()` used the anchor column for both the
      start and end of the selection. It now uses the min/max of anchor and
      endpoint, so dragging across a command copies the whole run. This was the
      root cause of “selecting does nothing” reports on one-line output.
- [x] **Click and drag are distinct.** A press anchors the selection but does
      not create a one-cell highlight; only a drag sets the endpoint. A plain
      click clears the previous selection instead of leaving a stray character
      selected, while reverse and multi-line drags still copy correctly.
- [x] **Clipboard shortcuts and a context menu.** `Cmd/Ctrl+Shift+C` copies,
      `Cmd/Ctrl+Shift+V` and `Shift+Insert` paste, `Cmd/Ctrl+Shift+A` selects
      the visible screen, and right-click offers Copy / Paste / Select All /
      Clear. Copy is disabled without a selection; Paste is disabled when the
      clipboard is empty.
- [x] **Visible rail controls.** The Terminal action row now has Copy and Paste
      next to Clear and Restart. Both return focus to the terminal after the
      action, so pasting does not strand the keyboard on a toolbar button.
- [x] **Native tests.** `hermes_terminal_tests` now covers mouse drag selection
      and copy, plain-click clearing, Select All + keyboard copy, context-menu
      action state, and a real bracketed-capable PTY paste. The right-rail
      project/Git/terminal test also clicks the new Copy and Paste buttons and
      verifies the clipboard and PTY output.
- [x] **Suite.** Full native suite green: 8/8 targets, 12.08 s; terminal tests
      13 passed. Formatting clean.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fifty-fifth pass)

**Make timeline rows readable and wrap their tooltips.**

- [x] **Timeline rows are real wrapped rows, not one-line buttons.**
      `TimelineRowButton` keeps the existing `rightRailTimeline_<n>` click/focus
      surface but hosts a timestamp label and a word-wrapped preview label.
      The button has `heightForWidth()` and resizes to the wrapped height, so
      long prompts no longer clip, collapse into a single line, or overlap the
      timestamp.
- [x] **Focused/pressed timeline rows stay legible.** The rail QSS explicitly
      styles `:focus`, `:pressed` and `:checked` with the code background plus
      an accent border, and gives preview/time labels their own foreground/
      secondary colors. Global pressed/hover button paint can no longer turn a
      selected row into a low-contrast system highlight.
- [x] **Tooltips wrap and show the full prompt.** `TimelineEntry` now keeps the
      original simplified `full_text` alongside the 160-character preview.
      `WrappedToolTip` emits a 360 px rich-text block with escaped content and
      pre-wrapped whitespace, so hovering a row shows the whole prompt in
      multiple lines instead of one oversized tooltip line.
- [x] **Native tests.** `rightRailShowsTodoTimelineAndFiltersPrompts` now feeds
      a long prompt and asserts wrapped preview height, width containment,
      full-text wrapped tooltip rendering, focus policy and explicit
      focus/pressed QSS states.
- [x] **Suite.** Full native suite green: protocol 17, ui 43, settings 35,
      chat 24, click 15, gateway 5 (+1 skipped), sidebar 10, pet 6, terminal 8.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fifty-fourth pass)

**Contain quote blocks in their own cards and wrap long text.**

- [x] **Transcript quotes are real cards.** A `kQuote` block now owns a
      full-width rounded card with a 3 px quote bar and 18/14/10 px inner
      padding. The text document is created at the card's inner width, and
      paint, text selection and the streaming caret all use the same inner
      `BlockTextRect`; long or unbroken quoted text wraps instead of bleeding
      past the card.
- [x] **Markdown previews treat blockquotes as cards.** `StyleMarkdownPreview`
      now forces `WrapAtWordBoundaryOrAnywhere`, reduces the importer's 40 px
      side margins to 14/12 px, and gives each `BlockQuoteLevel` block a tinted
      background. The duplicate settings-page styling function was deleted so
      settings artifact previews and the right-rail project preview share one
      implementation.
- [x] **Rendered HTML artifacts keep quotes inside their box.** The artifact
      HTML shell now adds a background, padding, rounded right corners and
      `overflow-wrap:anywhere` to `blockquote` in light and dark schemes.
- [x] **Native tests.** `quoteCardWrapsAndContainsText` proves a long quote's
      text rect stays inside its card and wraps further at a narrower width.
      `artifactsPreviewRendersMarkdownHtmlAndCsv` now includes a long blockquote
      and checks its card background, reduced margins and wrapped lines.
- [x] **Suite.** Full native suite green: protocol 17, ui 43, settings 35,
      chat 24, click 15, gateway 5 (+1 skipped), sidebar 10, pet 6, terminal 8.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fifty-third pass)

**Make clarification cards readable and keep their actions inside the card.**

- [x] **Choices are stacked, full-width rows.** The clarify card no longer
      packs options into a two-column grid. Each choice owns a row and long
      labels wrap to as many lines as needed via a custom-painted
      `WrappedChoiceButton` with `heightForWidth()`.
- [x] **The card and its contents share one box.** Content margins now use the
      painted card inset plus padding on both sides, so Skip/Send and every
      other action footer land inside the rounded card instead of hanging past
      its right edge. `RequestBar` re-queries layout height on resize, so the
      card grows when choices wrap in a narrower composer.
- [x] **Native test.** `clarifyOptionsStackAndWrapInsideCard` asserts stacked
      rows, wrapped long options, full-width layout, footer bounds, and that a
      narrower chat grows the card while keeping Send inside it.
- [x] **Suite.** Full native suite green: protocol 17, ui 43, settings 35,
      chat 23, click 15, gateway 5 (+1 skipped), sidebar 10, pet 6, terminal 8.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fifty-second pass)

**Replace the command box with a real terminal and finish the right-rail controls.**

- [x] **The terminal is a VT100/xterm emulator, not a sanitized log.** A native
      `TerminalWidget` now owns a `forkpty` shell, a UTF-8 decoder, primary and
      alternate screens, scrollback, cursor/saved-cursor state, SGR colors,
      erase/insert/delete, OSC titles, bracketed paste and resize via
      `TIOCSWINSZ`. It paints its own cell grid with the theme palette, supports
      selection/copy/paste and starts lazily when the Terminal section opens.
      The old `/usr/bin/script` + OSC/CSI stripping path is gone.
- [x] **Git can draft a commit message with the one-shot LLM.** The Git pane
      gained `AI Generate`, which collects staged (or `HEAD`) diff context,
      untracked files and the last ten subjects, then calls the shell-injected
      `llm.oneshot` route with the desktop `commit_message` template at 0.8
      temperature. Results are stale-guarded by project root + request
      generation; empty/error responses are surfaced inline.
- [x] **Long right-rail content wraps instead of clipping.** A header-only
      `FlowLayout` wraps Git/Terminal/artifact action rows at the 260 px rail
      minimum, while project previews, Git status/message and the project tree
      use middle elision or word wrapping. Section hosts no longer force their
      un-wrapped minimum width through the outer scroll area.
- [x] **Tests and suite.** New `hermes_terminal_tests` cover parser/color/
      alternate-screen/title behavior and a real PTY round-trip.
      `rightRailProjectGitTerminalPanes` now drives the `TerminalWidget` and
      the AI callback; `flowLayoutWrapsRowsAtNarrowWidths` proves wrapping.
      Full native suite green: protocol 17, ui 43, settings 35, chat 22,
      click 15, gateway 5 (+1 skipped), sidebar 10, pet 6, terminal 8.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fifty-first pass)

**Turn the right rail into an accordion and move session search to chat.**

- [x] **The rail is a scrolling accordion, not tabs.** Each section header is
      a `QPushButton` with `rightRailSection_<id>` / `rightRailSectionHeader`
      and a dynamic `expanded` property, so expanding one section reveals its
      content inline and keeps its own natural height. Project opens by
      default; Git, Terminal, Context, Agents, Todo, Timeline and Outputs
      start collapsed. The collapsed 44 px icon strip reopens a section.
- [x] **Search moved to the chat header.** `SessionSearchBar` sits above
      `ChatView` with a debounced gateway search, status line, result rows,
      Escape-to-clear and click-to-open. `Ctrl+F`, `/search`, the palette
      command and the Escape path all target this bar now; the right rail no
      longer owns session search. Timeline keeps its own prompt filter.
- [x] **All search fields complete from live context.** `search_completion.h`
      attaches a `QCompleter` that rebuilds suggestions on `textEdited`: chat
      header (session titles + project names), sidebar filter (session titles
      + project labels), settings search (navigation page names) and timeline
      search (current prompt previews).
- [x] **The terminal became a live PTY shell.** This pass ran
      `/usr/bin/script -q /dev/null /bin/zsh -f -i` on macOS and stripped
      OSC/CSI before painting; the fifty-second pass replaced it with the
      native `TerminalWidget` emulator. Commands are always echoed by the PTY,
      not by the UI.
- [x] **Project previews render by file kind.** Markdown uses
      `setMarkdown` + docs typography/table styling, HTML uses `setHtml`,
      images render as `<img>` (binary files are no longer rejected), and
      code/plain text uses the theme mono font. Switching themes re-renders the
      open preview.
- [x] **Theme changes apply to the new chrome.** Rail labels carry
      foreground/secondary/accent role properties and refresh their font and
      color on `SetTheme`; section headers, timeline rows, artifact cards and
      `railAction` buttons are QSS-owned instead of frozen inline styles.
      `SessionSearchBar` derives font sizes from `Theme::UiFont` and only uses
      QSS for colors/spacing.
- [x] **Tests.** `sessionSearchBarFindsAndOpensSessions`,
      `rightRailProjectGitTerminalPanes` (project preview, Git and terminal +
      light/dark rerender), timeline/sidebar/settings completer assertions,
      and the existing right-rail accordion/scroll suite. Full native suite
      green: protocol 17, ui 42, settings 35, chat 22, click 15, gateway 5
      (+1 skipped), sidebar 10, pet 6.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; richer session-result snippets.

## Status update — 2026-11-12 (fiftieth pass)

**Give the shell resizable panes and a practical right sidebar.**

- [x] **The shell is a real three-pane splitter.** The body is now a
      `QSplitter` (`shellSplitter`) with the session sidebar, chat column and
      right rail as children. Dragging either divider resizes the pane; widths
      are clamped (sidebar 180–440, right rail 260–800, chat ≥320) and persisted
      in `native` prefs together with the collapsed/visible flags. The title
      bar follows the live sidebar width instead of the static theme width.
- [x] **Left rail collapses without losing actions.** `Sidebar` gained a
      chevron collapse control. Collapsed mode pins the rail to 46 px, hides
      search and the scrolling session list, and keeps new chat plus every
      workspace navigation icon hit-testable. The collapse/new-chat/nav
      targets are covered by `collapsedSidebarKeepsNewChatAndNavigation`.
- [x] **Right rail matches the desktop pane split.** It gained Project, Git and
      Terminal tabs ahead of the existing Context/Agents/Todo/Timeline/Outputs/
      Search tabs, and a collapsed icon rail with per-tab shortcuts. Project
      shows the active project root, a `QFileSystemModel` tree and a text
      preview (double-click opens externally). Git shows `git status
      --short --branch` with Refresh / Commit / Commit & Push actions. Terminal
      embeds a project-rooted `/bin/sh` session with Clear and Restart.
- [x] **Async processes cannot call back into a destroyed rail.** Git and shell
      jobs are child `QProcess`es; the destructor disconnects all of them before
      killing/waiting, so a `finished` signal emitted during teardown cannot
      touch destroyed preview widgets.
- [x] **Tests.** `rightRailProjectGitTerminalPanes` initializes a temporary Git
      repo, checks the project tree/preview, status, commit and terminal
      command, then collapses/expands via the right-rail controls. Full native
      suite green: protocol 17, ui 41, settings 35, chat 22, click 15, gateway 5
      (+1 skipped), sidebar 10, pet 6.
- [~] **Next scopes:** branch/diff/review controls and ship-bar parity for Git;
      artifact generation bound to a project; remaining multi-window panes.

## Status update — 2026-11-11 (forty-ninth pass)

**Make Markdown artifact previews read like docs, not Qt’s 9 pt paste.**

- [x] **Styling follows the app theme.** `QTextBrowser` was taking the
      Markdown importer’s hard-coded `Sans Serif` 9 pt body, making a long
      `TODO.md`/README cramped and easy to mistake for raw source.
      `StyleMarkdownPreview` now applies the theme UI font (13 px), a document
      margin, and restyles Markdown tables with zero outer borders, 7 px cell
      padding, full-width layout, a tinted header row and hairline row rules.
- [x] **Markdown semantics are unchanged.** Qt still parses headings, lists,
      tasks, links, code and tables; only the document font/table formats are
      rewritten, so the source tab remains the exact bytes read from
      `/api/fs/read-text`.
- [x] **Tests.** `artifactsPreviewRendersMarkdownHtmlAndCsv` now asserts the
      preview document uses `Theme::UiFont(13)`, has a non-zero margin, and
      renders a Markdown table with no outer border, 7 px padding and 100%
      width. Full suite green (protocol 17, ui 40, settings 35, chat 22,
      click 15, gateway 5 + 1 skipped, sidebar 9, pet 6).
- [~] **Next scopes:** searchable combo pickers and dependent-field rules;
      billing RPC screens, appearance and plugin deep links.

## Status update — 2026-11-11 (forty-eighth pass)

**Stop idle snapshot churn from resetting artifact scroll.**

- [x] **Identical session polls no longer bump the snapshot revision.**
      `ProfileWorker::FetchSessions` compares the parsed session list and the
      selected/model/status fields before publishing. The 10 s refresh timer
      used to republish byte-identical sessions and trigger every visible
      page rebuild.
- [x] **Artifact views skip content-identical rebuilds.** `RightRail` hashes
      the collected artifacts (selected session, home, labels, versions and
      content hashes); `SettingsView` hashes the artifacts `PageState` plus
      `artifacts_cwd`/`artifacts_preview`. A revision-only snapshot leaves the
      existing widgets and reader position untouched, while real changes still
      rebuild.
- [x] **Scroll restore survives late layout.** Both artifact scroll restorers
      keep the immediate pass and retry at 16/100/300 ms, applying later passes
      only while the target is still at zero. User scrolling is not overridden.
- [x] **Tests.** `rightRailKeepsArtifactScrollAcrossSnapshots` and
      `artifactsScrollSurvivesPageRebuild` now force a real content rebuild,
      verify restoration, then replay identical snapshots and assert the
      widgets were not replaced and scroll remained. Full suite green
      (protocol 17, ui 40, settings 35, chat 22, click 15, gateway 5 + 1
      skipped, sidebar 9, pet 6).
- [~] **Next scopes:** searchable combo pickers and dependent-field rules;
      billing RPC screens, appearance and plugin deep links.

## Status update — 2026-11-11 (forty-seventh pass)

**Refresh-safe scroll and the Home bucket.**

- [x] **Artifact panes keep their reader position.** `RightRail` captures the
      artifact list scroll and the selected preview document scroll before a
      snapshot rebuild, then restores them after the new widgets exist with a
      generation guard. `SettingsView` does the same for the outer settings
      scroll and every named scroll area inside the current page, including the
      artifacts list/preview; a section change still starts at the top.
- [x] **Sidebar scroll no longer jumps on refresh.** A snapshot or identical
      project tree refresh leaves `session_scroll_` alone; only a real project
      scope or expansion change resets it. The Projects/Sessions section now
      paints a visible vertical scrollbar (`sessionScrollBar`) synced to the
      custom-painted list, so overflow is discoverable.
- [x] **Unassigned sessions stay above the project switcher.** The backend's
      synthetic `__no_project__` bucket is pinned first and labeled “Home” with
      a home glyph, matching desktop `homeFirst()` rather than trusting backend
      ordering. Sessions remain backend-authoritative; this is display order
      only.
- [x] **Tests.** `rightRailKeepsArtifactScrollAcrossSnapshots` covers both the
      artifact pane and an HTML preview document. `artifactsScrollSurvivesPageRebuild`
      covers the outer settings scroll and the artifacts preview browser.
      `projectScrollSurvivesSnapshotsAndHomeLeads` covers Home pinning, the
      visible scrollbar, scroll stability across snapshots and reset on scope
      change. Full suite green (protocol 17, ui 40, settings 35, chat 22,
      click 15, gateway 5 + 1 skipped, sidebar 9, pet 6).
- [~] **Next scopes:** searchable combo pickers and dependent-field rules;
      billing RPC screens, appearance and plugin deep links.

## Status update — 2026-11-11 (forty-sixth pass)

**Artifacts file-browser safety + project-root default.**

- [x] **Browse no longer dereferences rebuilt widgets.** Artifact file/mkdir/
      upload/download/delete dialogs are parented to the window rather than the
      page, and every post-dialog widget access goes through `QPointer`. A
      worker snapshot that rebuilds the page while a modal picker is open can no
      longer leave a dangling `QLineEdit`/page pointer (the reported Browse
      crash).
- [x] **Artifacts starts at the scoped project directory.** `ProfileWorker`
      keeps an artifacts root pushed from `MainWindow` via
      `ArtifactsWorkingDirectory(projects, scope)`: the project's resolved
      working directory, or `QDir::homePath()` when no project is scoped. The
      old `/api/fs/default-cwd` probe is gone. Navigation inside the page keeps
      its folder; changing project scope resets to the new project root.
- [x] **Tests.** `projectsParseListTreeAndMutators` covers project path,
      home fallback and All-projects fallback. Full suite green (protocol 17,
      ui 39, settings 34, chat 22, click 15, gateway 5 + 1 skipped, sidebar 8,
      pet 6).
- [~] **Next scopes:** searchable combo pickers and dependent-field rules;
      billing RPC screens, appearance and plugin deep links.

## Status update — 2026-11-11 (forty-fifth pass)

**Transcript and artifact-preview typography.**

- [x] **Markdown surfaces use a quieter docs-style hierarchy.** Heading sizes
      now step 20/16/14px with extra space before sections, paragraphs use a
      20px line height, quotes are non-italic with a soft panel plus bar, and
      tables use neutral alpha borders/header fill instead of the old fixed
      `#888`/`#555` grid. Inline code, links and code blocks were tightened to
      the same scale.
- [~] **Reference-image fidelity is still unverified.** The supplied reference
      image was not available in this session, so this pass adopts a clean
      GitHub-docs direction rather than claiming pixel parity. Re-send the
      image to tune spacing/colors exactly.
- [x] **HTML fragment previews get readable defaults.** Standalone artifact
      fragments now render with system typography, 1.6 line height, capped
      width, styled headings/code/tables/quotes and light/dark color-scheme
      support. Full HTML documents still keep their own styles.
- [x] **Tests.** `markdownParsesRichBlocksAndInlineSpans` now asserts the
      neutral table border/header styling in addition to alignment/cell
      content.
- [~] **Next scopes:** searchable combo pickers and dependent-field rules;
      then billing RPC screens, appearance and plugin deep links.

**Verification.** `hermes_protocol_tests` 17, `hermes_ui_tests` 39,
`hermes_settings_tests` 34, `hermes_chat_tests` 22, `hermes_click_tests` 15,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 8,
`hermes_pet_tests` 6 — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (forty-fourth pass)

**Account-owned connector operation progress.**

- [x] **Account operations are stored, not dropped.** `connection.update`
      frames with `owner.type == "account"` no longer disappear when no
      transcript card matches. `ProfileSnapshot.account_operations` keeps the
      latest operation per `op_id`; `ParseConnectionOperation` now also reads
      the owner type. Open operations win over settled ones for a connector,
      matching desktop `accountOperationFor`.
- [x] **Initial connect snapshot survives page rebuilds.**
      `connectors.connect` already returns the operation snapshot. The
      Capabilities connect path now hands that result to
      `ProfileWorker::RememberAccountOperation`, which stores it before the
      page can rebuild, so progress does not depend on a broadcast arriving
      first.
- [x] **Capabilities rows show live account progress.** A connector row whose
      account operation is open renders its target state (`operation
      initiated`, `operation pending`, …) and the status label reads
      `Connector operation: <state>`. Settled operations are retained for a
      possible retry but stop claiming live progress; the connector list owns
      the final connected/disconnected state.
- [x] **Stale updates and metadata are guarded.** The worker rejects an older
      `seq`, merges omitted target metadata (`detail`, `instructions`,
      `connect_url`, `connection_id`, aliases, tools, `required_env`,
      `resolved`), caps retained account operations at 12, and bumps the
      snapshot revision so an open settings page rebuilds and refetches.
- [x] **Native tests.** `connectionOperationParsesTargetsAndBuildsAnswer` now
      covers the account owner and open-over-settled selection;
      `capabilitiesMergesAccountOperationIntoConnectorRow` covers the live row
      label/status and confirms a settled operation is not shown as progress.
- [~] **Next scopes:** live-verify connector progress and the other pending
      gateway flows, then reduce remaining REST gaps. GPT-Live/WebRTC remains
      blocked on a native WebRTC bridge.

**Verification.** `hermes_protocol_tests` 17, `hermes_ui_tests` 38,
`hermes_settings_tests` 31, `hermes_chat_tests` 22, `hermes_click_tests` 15,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 8,
`hermes_pet_tests` 6 — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (forty-third pass)

**Connector operation cards and full gateway-event coverage.**

- [x] **`connection.request` / `connection.update` cards.** The worker parses
      the operation snapshot (`op_id`, monotonic `seq`, deadline, settled
      reason, display suggestions, and every target's kind/action/state,
      detail/instructions, link, tools and `required_env`) into
      `PendingRequest.connection`. The transcript request bar renders one row
      per target with state, a link opener, credential fields, Skip/Approve and
      Continue. Answers go through the canonical `connection.respond` RPC with
      the live session as `owner`; approved rows carry the collected `env`.
      Updates stay on the card until the gateway reports `settled`, and a frame
      whose `seq` is not newer cannot move a row.
- [x] **Resume restore.** `session.resume` now restores `pending_connection`
      with the live session id, so a reconnecting client sees the same card
      and deadline instead of losing the operation.
- [x] **Event coverage.** `connection.request` and `connection.update` were
      the last two gateway-agent events without a native handler. The gateway
      now has native handlers for all **26/26** methods.
- [x] **Directory display cards accept composer text.** A display operation
      (`manage_connections` directory, no targets) is answered by plain text
      typed in the composer: `SubmitText` and the detached composer consume it
      as `said` plus `settled_by: continue`. A slash command or an attachment
      cannot be an answer; it settles the card with Continue first, then
      routes normally, matching desktop `skipConnectionRequest`.
- [x] **Native tests.** `connectionOperationParsesTargetsAndBuildsAnswer`
      covers target/env parsing, display suggestions, settlement and the
      `connection.respond` envelope; `connectionCardAnswersTargetsAndContinues`
      covers rendering a pending and a connected target, collecting a secret
      env value, approving, and Continue; `connectionCardYieldsToTypedText`
      covers ending a target card with Continue before routing a message;
      `connectionDisplayCardUsesComposerText` covers the directory `said`
      answer.
- [~] **Next scopes:** account-owned connector updates on the Capabilities
      page, remaining REST gaps and GPT-Live/WebRTC.

**Verification.** `hermes_protocol_tests` 17, `hermes_ui_tests` 38,
`hermes_settings_tests` 30, `hermes_chat_tests` 22, `hermes_click_tests` 15,
`hermes_sidebar_tests` 8, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (forty-second pass)

**Backend-owned prompt queue and project/new-chat fixes.**

- [x] **Pending admissions are the queue source.** `session.info` and resume
      snapshots now parse `pending` into `PendingAdmission` rows (admission id,
      input id, text, status, outcome, controls, steering, sequence, epoch and
      execution generation). The composer badge and queue dialog read those
      rows, so queue edits/cancels affect what the gateway will actually run.
- [x] **Snapshot fencing.** `PendingSnapshotWins` rejects an older same-epoch
      snapshot so a delayed resume answer cannot retire an admission a newer
      fanout still lists. A new owner epoch is authoritative, and a missing
      revision cannot pin a stale queue forever.
- [x] **Server queue actions.** The queue dialog sends
      `prompt.replace_queued` (new submission id + edited text at the FIFO
      tail), `prompt.cancel`, `prompt.steer_queued`, and
      `prompt.resolve_unknown` (with `execution_generation`). `status ==
      started` rows stay hidden because a started admission belongs to the
      live turn. Started steering can never be retroactively edited; delivered
      steered history is labelled as history.
- [x] **Queued sends are run-after, not steers.** `prompt.submit` carries
      `queued: true` for Ctrl+Enter/`/queue` while a turn runs. The gateway
      owns the queued user row, so the native worker skips the optimistic
      transcript bubble and the old local queue drain is gone. Local
      `queued_prompts_` survives only as legacy/test compatibility.
- [x] **New chat stays in the scoped project.** `ProjectWorkingDirectory`
      resolves the project tree path (falling back to the list primary path)
      and every new-chat entry point routes through `MainWindow::NewSession`
      with that cwd. The selected local placeholder remains visible under the
      scoped project until `projects.tree` persists it.
- [x] **Command palette single-click.** `itemClicked` runs a command in
      addition to Enter activation; `RunCurrent()` ignores the second
      activation after the palette closes.
- [x] **Native tests.** `pendingAdmissionsParseAndBuildQueueActions` covers
      parsing, snapshot fencing and all four params builders;
      `promptHistoryEditsAndCancelsServerQueue` covers server edit/replace,
      unknown resolve/cancel, steer availability and row removal after a
      successful RPC; `commandPaletteRunsActionOnSingleClick` and
      `scopedProjectKeepsNewLocalChatVisible` cover the two UI regressions.
- [~] **Next scopes:** `connection.request`/`connection.update` connector
      operation cards, remaining REST gaps and GPT-Live/WebRTC.

**Verification.** `hermes_protocol_tests` 16, `hermes_ui_tests` 38,
`hermes_settings_tests` 30, `hermes_chat_tests` 19, `hermes_click_tests` 15,
`hermes_sidebar_tests` 8, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (forty-first pass)

**Browser/CDP automation:** browser control now has a native route instead of
only Computer Use diagnostics. The gateway remains the discovery/launch
authority, while Qt attaches directly to the concrete DevTools endpoint.

- [x] **Direct CDP client.** `src/browser_automation.*` parses HTTP(S)
      discovery endpoints, direct `/devtools/page/<id>` websockets and
      `/devtools/browser/<id>` targets. HTTP discovery tries `/json/list` then
      `/json/version`; browser targets attach with
      `Target.getTargets` + `Target.attachToTarget(flatten: true)` and send the
      returned `sessionId` on every frame.
- [x] **Screenshots and coordinates.** `Page.captureScreenshot` decodes PNG
      data in the client; `MainWindow` saves through `QSaveFile` on a worker.
      Click, double-click, right-click, move and drag emit real
      `Input.dispatchMouseEvent` moved/pressed/released sequences with
      `button`, `buttons` and `clickCount`.
- [x] **Gateway and consent.** Connect/status/disconnect call
      `browser.manage` with the active profile, then attach to the returned
      URL. The first-open real-profile offer writes
      `browser.use_real_profile: true` through `SaveConfig`; “Not now” declines
      only the real profile and mutes the offer for this app run, while “Don’t
      ask again” persists
      `native.browser_real_profile_prompt_dismissed`. A native fallback keeps
      the consent state stable until config refetch completes.
- [x] **Entry points.** `/browser`, the command palette’s “Browser automation”
      action and `Ctrl+Shift+D` open the dialog. Object names cover the
      endpoint/status/connect/disconnect/navigate/screenshot/coordinate
      controls and the consent buttons.
- [x] **Native tests.**
      `browserAutomationParsesEndpointsAndConsent` covers endpoint forms,
      consent gating and `browser.manage` params/results.
      `browserAutomationBuildsPointerEventsAndScreenshots` covers mouse
      sequences, PNG decode/save and screenshot names.
      `cdpClientDispatchesScreenshotAndCoordinates` drives a fake
      `QWebSocketServer` through connect, screenshot, click and drag frames.
      `cdpClientDiscoversHttpAndAttachesBrowserTarget` covers HTTP `/json/list`
      discovery, flat browser-target attach and `sessionId` propagation.
      `browserAutomationDialogSurfacesControls` covers dialog controls,
      gateway refusal messages and consent checkbox state.
- [ ] **Remaining:** this is external-Chromium CDP automation, not the desktop
      Electron `preview.read`/`preview.act` in-app preview bridge. There is no
      generic CDP-connect consent separate from `browser.use_real_profile`.
- [~] **Next scopes:** `connection.request`/`connection.update` connector
      operation cards, remaining REST gaps and GPT-Live/WebRTC.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 37,
`hermes_settings_tests` 30, `hermes_chat_tests` 19, `hermes_click_tests` 14,
`hermes_sidebar_tests` 7, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (fortieth pass)

**Right-rail generated artifact browser:** substantial generated HTML, SVG and
code fences now become versioned artifacts in the Outputs tab, mirroring the
desktop's transcript-derived registry.

- [x] **Detection parity.** `src/artifacts.*` ports desktop
      `artifact-detect.ts`: HTML documents ≥160 chars, HTML fragments ≥1200,
      SVG ≥2000, and code ≥48 lines or ≥3000 chars, excluding prose/terminal/
      rich-renderer languages. Titles come from `<title>`, `<h1>`, leading
      filename comments or declarations; slug, FNV-1a content hash and download
      extensions match desktop.
- [x] **Version registry.** `CollectGeneratedArtifacts` rebuilds from assistant
      code blocks, dedupes by content hash, appends versions for the same
      `(kind, language, title)` slug, caps at 24 artifacts / 20 versions and
      evicts oldest history under a 4 MiB retained-content budget. Outputs are
      newest-updated first.
- [x] **Browser UI.** Generated rows open a right-rail preview with
      `vN / M` prev/next controls, HTML in `QTextBrowser`, SVG in `QSvgWidget`,
      code as selectable plain text, Source toggle, Copy, Save…, and Open in
      browser (temporary HTML file). Save runs on a worker through `QSaveFile`
      with the desktop download filename. Local file/image preview behavior is
      unchanged.
- [x] **Native tests.**
      `PreferencesTest::artifactDetectionAndNamingMatchDesktop` covers all
      desktop thresholds, h1 fallback, filename/declaration titles, non-artifact
      languages, slug/hash and download names.
      `rightRailBrowsesGeneratedArtifactVersions` covers registry versioning,
      duplicate-content no-op, version stepping, copy and save callbacks.
      `rightRailRendersHtmlArtifactAndSource` covers HTML/SVG rendering and
      source toggling.
- [ ] **Remaining:** artifacts are memory-only and transcript-derived because
      there is no `/api/artifacts` gateway route; project binding still has no
      backend route.
- [~] **Next scopes:** `connection.request`/`connection.update`,
      remaining REST gaps, GPT-Live/WebRTC and browser/CDP automation.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 32,
`hermes_settings_tests` 30, `hermes_chat_tests` 19, `hermes_click_tests` 14,
`hermes_sidebar_tests` 7, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (thirty-ninth pass)

**Project appearance and auto-project hide:** the native project surface now
matches the desktop's row look and lets auto-discovered rows be hidden without
deleting backend state.

- [x] **Project appearance.** `src/project_appearance.*` mirrors the desktop's
      28 codicon names and 12 HSL swatches, maps names to codepoints and parses
      HSL colours. `ProjectAppearanceDialog` applies `icon`/`color`
      immediately; explicit rows use `projects.update` (empty string clears
      either key), while auto rows are adopted through `projects.create` with
      the row path/label and the chosen look.
- [x] **Auto-project hide and restore.** Auto rows now emit `dismiss_auto`
      instead of `delete`; `MainWindow` persists hidden ids in
      `native.dismissed_auto_projects` without issuing a gateway delete,
      filters only `is_auto` rows in `FilterVisibleProjects`, resets a scope
      that points at a hidden row, and offers `Restore hidden projects` in
      the command palette.
- [x] **Sidebar/menu fix.** The project menu exposes `Appearance…` for
      explicit and auto rows, `Hide auto project` for auto rows, and keeps
      path/edit/folder/active/delete actions conditional on explicit projects
      and non-empty paths. The session menu's delete branch was restored.
- [x] **Native tests.**
      `PreferencesTest::projectAppearanceParsesColorsIconsAndFiltersDismissedAutos`
      covers codepoint/HSL parsing, swatch/icon inventories, dismissed-id JSON
      round-trip, auto-only filtering, create/update params and dialog
      callbacks.
- [x] **Zero-project add affordance.** A successful empty `projects.list`
      still paints the PROJECTS header, `All projects` row and add glyph;
      only an unavailable/failed project fetch keeps the section hidden.
      Covered by `SidebarTest::emptyProjectStateStillOffersCreate`.
- [ ] **Remaining:** project archive/restore has a gateway method but no
      desktop project-menu surface.
- [~] **Next scopes:** right-rail artifact browser, artifact project binding,
      `connection.request`/`connection.update`, remaining REST gaps,
      GPT-Live/WebRTC and browser/CDP automation.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 29,
`hermes_settings_tests` 30, `hermes_chat_tests` 19, `hermes_click_tests` 14,
`hermes_sidebar_tests` 7, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`.

## Status update — 2026-11-11 (thirty-eighth pass)

**Restart continuity:** the native shell now reopens the previous workspace
before the gateway finishes syncing, without making the cache authoritative.

- [x] **Session restart cache.** `src/cache_store.{h,cc}` adds versioned,
      per-profile JSON snapshots for the selected session, session summaries
      and a capped recent transcript (120 rows / 4 MB). Writes use
      `QSaveFile`, `HERMES_UI_CACHE` relocates the cache for tests/portable
      runs, and malformed or version-mismatched files are ignored. Cached rows
      are loaded by `ProfileWorker` before `FetchSessions()`, have streaming
      flags cleared and hydrated duration `-1`, and keep `history_loaded_`
      false so REST replaces them. Dirty snapshots flush every 3 s and on
      `Shutdown()`; the selected session is also fetched immediately even if
      the websocket has not connected yet.
- [x] **Project restart cache.** `MainWindow` loads the project list/tree,
      active project and scope on a background thread with a profile/generation
      guard, never applying an older disk read after fresher
      `projects.list`/`projects.tree` data. Project mutations schedule a
      debounced background save; the destructor flushes any final dirty state.
- [x] **Active profile restore.** The last activated profile is stored by name
      in `native.active_profile`; startup resolves it through
      `src/profile_selection.*` and falls back to `default` when that profile
      was deleted or renamed. Profile-name lookup also backs
      `ActivateProfileByName`.
- [x] **Native tests.** `PreferencesTest::uiCacheRoundTripsSessionsAndProjects`
      covers session/project round-trips, cache isolation, corrupt files,
      version rejection, and the “history, not a spinner” invariant.
      `PreferencesTest::profileSelectionFallsBackWhenSavedNameIsGone` covers
      startup profile resolution.
- [ ] **Remaining:** the project menu still lacks appearance (`icon`/`color`),
      archive/restore and auto-project dismiss/restore; the zero-project add
      affordance remains palette-only.

## Status update — 2026-11-11 (thirty-seventh pass)

**Project status:** profile-scoped projects and queued/steered prompt management
are now native; the broad desktop-only surfaces remain partial.

- [x] **Profile-scoped projects.** `projects.list` + `projects.tree` are fetched
      in parallel with the active profile attached, throttled (15 s loaded /
      1.2 s cold), and ignored when a late callback belongs to a profile that
      has already switched. The sidebar paints an `All projects` row plus one
      row per backend tree node in the session scroll; selecting a project
      filters sessions by the backend's `sessionIds` only, and scoped
      `TODAY`/`EARLIER` splits use the last session's `last_active_secs` cutoff
      instead of filtered positions.
- [x] **Expandable project rows.** Every backend project row now has a
      chevron and expands its complete backend `sessionIds` set directly
      beneath the row. Activating the row or its chevron scopes and expands;
      collapsing or choosing `All projects` restores the flat recency list.
      Project rows and nested sessions share the session scroll so long
      catalogues no longer overflow a fixed header. Covered by
      `SidebarTest::projectRowsScopeSessionsAndAdd`.
- [x] **Project management.** Create/edit dialogs, delete, add/remove folder,
      set primary folder and set-active all call their `projects.*` mutators
      with the active profile. The sidebar context menu exposes
      enter/new-chat/edit/folder/active/delete plus `Reveal in file manager`
      and `Copy path`; `New chat in project` sends `session.create` with the
      project primary folder as `cwd` + `cwd_explicit`. Project creation is
      also in the command palette because the sidebar add glyph is hidden
      while no projects exist.
- [x] **Queued and steered prompt management.** `PromptHistoryDialog` lists
      queued and steered history, previews/edits/deletes entries and loads a
      prompt back into the composer. Queued server rows are the gateway's
      pending admissions: edit replaces at the FIFO tail, delete cancels (or
      resolves an UNKNOWN admission), and “Steer now” calls
      `prompt.steer_queued` when the row advertises the `steer` control.
      Started admissions are hidden. Steered entries are honestly labelled as
      already-delivered history. Composer steering and `/steer` both route
      through `MainWindow::RecordSteeredPrompt`, capped at 20 entries.
- [x] **Project appearance, auto-hide and empty state.** Explicit projects
      write icon/color through `projects.update`; auto projects adopt the same
      look through `projects.create`. Hidden auto projects persist locally in
      `native.dismissed_auto_projects` and return through the palette's
      “Restore hidden projects”. A project section with zero rows still offers
      the add affordance. Archive/restore remains off the desktop UI path.
- [~] **Next scopes:** artifact project binding,
      `connection.request`/`connection.update`, remaining REST gaps and
      GPT-Live/WebRTC.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 28,
`hermes_settings_tests` 30, `hermes_chat_tests` 19, `hermes_click_tests` 14,
`hermes_sidebar_tests` 6, `hermes_pet_tests` 6, `hermes_gateway_tests` 5
(+1 skipped) — all passed after `clang-format -style=Google`. The gateway suite
was re-run once the local dashboard recovered from its earlier `state=starting`
condition; no route assertions changed.

## Status update — 2026-11-11 (thirty-sixth pass)

**Project status:** the native Qt shell is feature-complete for the core
settings/chat path and still partial on the broad desktop-only surfaces.

- **Done:** settings data pages (About/Sessions/Notifications/Keybinds),
  providers/keys/vault/gateway, full JSON-RPC replay/idempotency/fencing,
  transcript text selection, desktop hover actions on every message item,
  detached chat, pet overlay, WhatsApp QR onboarding, messaging handoff and
  audio-output selection.
- **Partial:** scheduled tasks/artifacts/capabilities (artifact project binding
  remains), plugin/billing/update surfaces, chat parity (pin-to-context uses
  the documented client-side fallback; the generated-artifact browser landed
  in the fortieth pass), platform surfaces (GPT-Live/WebRTC, browser/CDP, HUD/wake and
  multi-window panes beyond chat), and the REST audit at **146/307** paths
  (161 gaps). Gateway events are **26/26**; connector operation cards now
  answer `connection.request` / `connection.update`.
- [x] **Scheduled-task readability and footer actions.** Cron cards cap the
      copy column at 80% of the card so long names, schedules and recent-run
      summaries wrap instead of clipping in the settings scroll area. Pause/
      resume, edit, run-now and delete now live in a separate icon action row
      below each task, with tooltips and accessible names; the existing
      `cronRow_*`, `cronToggle_*`, `cronEdit_*`, `cronTrigger_*`,
      `cronDelete_*` and `cronRuns_*` object names are preserved. Covered by
      `scheduledTaskRowsWrapAndUseIconActions`.
- [~] **Next scopes:** right-rail artifact browser, artifact project binding,
      `connection.request`/`connection.update`, remaining 165 REST gaps,
      GPT-Live/WebRTC and browser/CDP automation.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 19,
`hermes_settings_tests` 29, `hermes_chat_tests` 15, `hermes_click_tests` 14,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 5,
`hermes_pet_tests` 6 — all passed after `clang-format -style=Google`.

## Landed on 2026-11-10 (thirty-fifth pass)

- [x] **Detachable chat window.** `Ctrl+Shift+N` and the command palette open
      a top-level `DetachedChatWindow` with its own transcript and composer.
      It reuses the active profile/session, all message-action handlers,
      request cards, model/reasoning pickers, attachments, slash commands,
      queue/steer and `@`-mention listing; `Refresh()` keeps its snapshot in
      sync and the theme switch reaches it. Covered by
      `detachedChatWindowSharesSnapshotAndComposer`.
- [x] **Desktop pet overlay.** `pet.info` spritesheets decode on a worker,
      animate by state row, derive `running`/`waiting`/`review`/`idle` from the
      live snapshot and keep transparent margins click-through. Covered by
      `disabledPayloadIsClean`, `decodesSpriteGeometryAndRows`,
      `rejectsMalformedSprites` and `overlayResolvesRunRowAndAnimates`.
- [x] **WhatsApp QR onboarding.** Start/poll/apply/cancel run through the
      dashboard routes, render a vendored Nayuki QR, auto-fill `self-chat`
      allowed users and release abandoned pairings. Covered by the WhatsApp
      leg of `rendersDesktopSectionsAndSavesSparsePatch`.
- [x] **Audio-device selection.** Voice settings list Qt Multimedia outputs
      and inputs; output applies to read-aloud immediately. Covered by
      `voiceSectionExposesAudioDevices`.
- [x] **Messaging handoff.** `/handoff <platform>` queues
      `handoff.request`, polls `handoff.state` and calls `handoff.fail` on
      timeout. Covered by `handoffPlatformAndStatusText`.
- [x] **Capabilities crash fix.** The connector list completion is captured
      with `QPointer` guards, so a page rebuild cannot leave the async
      callback touching a destroyed panel/layout. Covered by
      `capabilitiesLateConnectorListIsIgnoredAfterPageDestroyed`.
- [~] **Remaining Platform surfaces:** GPT-Live/WebRTC sessions (no native
      WebRTC stack), floating HUD/wake indicator, multi-window panes beyond
      chat, and agent direct spawn/kanban.

**Verification.** `hermes_protocol_tests` 15, `hermes_ui_tests` 19,
`hermes_settings_tests` 28, `hermes_chat_tests` 15, `hermes_click_tests` 14,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 5,
`hermes_pet_tests` 6 — all passed after `clang-format -style=Google`.

## Landed on 2026-11-10 (thirty-fourth pass)

- [x] **Hover action row on every message item.** The standalone Copy/Edit
      chips are gone. Every transcript item now reserves the same
      desktop-style footer row and reveals it on hover, so showing actions
      never shifts layout:
      - assistant replies: duration · branch-in-new-chat · copy · copy full ·
        read aloud · refresh · react;
      - user prompts: branch · copy · copy full · read aloud · edit · react;
      - system notices and tool-only replies: copy · copy full.
      The row keeps the existing `chat:copymsg` / `chat:copyfullmsg` /
      `chat:branchmsg` / `chat:speakmsg` / `chat:editmsg` /
      `chat:regenmsg` / `chat:reactmsg` targets, the right-click menu, and
      icon hover styling. Covered by `everyMessageItemUsesHoverActionRow`,
      `hoverActionRowMatchesDesktop`, `messageActionsEditAndRegenerate`,
      `copyChipsCopyEveryBubble` and `systemNoticesShareHoverActionRow`.
- [~] **Message actions** remain partial only because pin-to-context still
      uses the documented client-side prompt prefix
      (`ComposePinnedPrompt`) instead of a shared gateway pin contract.

**Verification.** `hermes_protocol_tests` 14, `hermes_ui_tests` 19,
`hermes_settings_tests` 26, `hermes_chat_tests` 14, `hermes_click_tests` 14,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 5 — all passed.

## Landed on 2026-11-10 (thirty-third pass)

- [x] **Assistant hover action row.** Every assistant reply now reserves a
      desktop-style footer row and reveals it on transcript hover:
      duration (for completed live turns) · branch-in-new-chat · copy · copy
      full response · read aloud · refresh · react. The controls use codicon
      glyphs with the desktop tooltips, share the existing message handlers,
      keep the right-click menu intact, and reserve height so hovering never
      shifts the transcript. Covered by
      `hoverActionRowMatchesDesktop`, `messageActionsEditAndRegenerate` and
      `copyChipsCopyEveryBubble`.
- [~] **Message actions** remain partial only because pin-to-context still
      uses the documented client-side prompt prefix
      (`ComposePinnedPrompt`) instead of a shared gateway pin contract.

**Verification.** `hermes_protocol_tests` 14, `hermes_ui_tests` 19,
`hermes_settings_tests` 26, `hermes_chat_tests` 12, `hermes_click_tests` 14,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 5 — all passed.

## Landed on 2026-11-10 (thirty-second pass)

Remaining `[~]` scopes: per-route REST coverage, pin-to-context parity, right-rail
artifact browser, connection-operation cards, shell extras

- [x] **Gateway JSON-RPC client.** `GatewaySocket` now records request
      sessions, keeps a per-session replay watermark (`src/gateway_replay.*`),
      resets it on `gateway.ready` epoch changes, asks
      `session.events.since` on resume/reconnect, refetches durable state on a
      truncated ring, and re-delivers replay `open_requests` as well as the
      resume snapshot. `session.create` sends the canonical `request_id` +
      `source: gui` (the legacy `idempotency_key` is refused by the canonical
      owner);
      `session.mutate` carries `request_id` + `expected_revision` +
      `expected_generation` and is used by Delete (with the REST snapshot
      fallback). Prompt submission keeps the receipt by `admission_id` and
      queries `prompt.receipt {include_result:true}` after resume. Covered by
      `gateway_protocol` (replay ordering/epoch reset, payload fencing).
- [x] **Reaction and notice event handling.** `notification.show/clear` upsert
      keyed agent notices into the Context pane and status bar;
      `message.reaction` and hydrated `display_metadata.reactions` paint a
      grouped emoji footer under user/assistant bubbles; `reaction {kind:vibe}`
      shows a transient status. Covered by `reactionFooterAddsTranscriptHeight`,
      `rightRailShowsContextAgentsAndArtifacts`, and `gateway_protocol`.
- [x] **Session-control, MoA and review events.** `session.control.update`
      renders Goal/Loop/Heartbeat lines in Context; `moa.reference`,
      `moa.progress`, `moa.aggregating` and `moa.phase` append to the reasoning
      disclosure (later progress counters append instead of replacing earlier
      references); and `review.summary` / `agent.print` land as status notes.
      Covered by `gateway_protocol`, `rightRailShowsContextAgentsAndArtifacts`
      and the full suite.
- [~] **REST route audit.** `parity/rest_routes.py` generates
      `parity/rest_routes.md` + `.json` from the gateway's 307 FastAPI routes and
      the native `/api` literals. Current audit: **142 matched, 165 gaps,
      3 native-only** (path-level; verbs are not inferred from C++ call sites).
      `parity/check_parity.py` reports the counts without failing the scoped
      contract. The remaining gaps still need per-route implementation or a
      documented desktop-only/backend-replaced reason.
- [x] **Event coverage: 26/26 gateway-agent methods.** `tool.generating` and
      `tool.progress` drive the tool row/status label through
      `ToolGeneratingText` / `ToolProgressText`; `connection.request` /
      `connection.update` now drive a native connector card with per-target
      answers through `connection.respond`.

**Verification.** `hermes_protocol_tests` 14, `hermes_ui_tests` 19,
`hermes_settings_tests` 26, `hermes_chat_tests` 11, `hermes_click_tests` 14,
`hermes_gateway_tests` 5 (+1 skipped), `hermes_sidebar_tests` 5 — all passed.
`python3 parity/rest_routes.py` regenerated the 142/307 audit; `check_parity.py`
now emits the non-blocking REST section.

## Landed on 2026-11-10 (thirty-first pass)

Remaining `[~]` scopes: right-rail artifact browser, pin-to-context parity,
shell extras

- [x] **Composer live voice input.** The composer now paints a microphone
      control (`composer:voice`) with a `Ctrl+Shift+M` shortcut and command
      palette entry. It calls the gateway-owned capture path
      (`voice.toggle {action:on}` then `voice.record {action:start|stop}`), so
      no Qt WebRTC stack is required. `voice.status`, `voice.partial` and
      `voice.transcript` are handled before session filtering; partial text and
      recording state render in the composer, and each completed transcript is
      inserted into the draft exactly once via a monotonic serial. Busy
      microphone and wake-word ownership errors surface in the status bar.
      Covered by `composerLiveVoiceStateAndPinnedPill`.
- [~] **Message pin-to-context.** Assistant replies can be pinned/unpinned from
      the transcript context menu (`Ctrl+Shift+P` for the latest reply) and
      pinned excerpts prefix subsequent prompts. This is a documented
      client-side fallback because the gateway exposes no message-pin RPC;
      unlike the desktop, it is not yet backed by a shared context contract.
      Covered by `messageActionsEditAndRegenerate` and
      `promptSubmitTruncationIsExplicit`.
- [x] **Voice event plumbing.** `ProfileSnapshot` carries recording, status,
      partial and transcript-serial state, and the worker publishes voice
      events even when no session id is attached. New Session, profile
      activation and session selection reset the transcript cursor and pinned
      context so stale speech cannot leak across chats.

## Landed on 2026-11-10 (thirtieth pass)

Remaining `[~]` scopes: right-rail artifact browser, shell extras

- [x] **Real attachment tickets.** Prompt submission now queues the gateway
      attachment verb for every staged file before `prompt.submit`:
      `image.attach` for image suffixes, `pdf.attach` for PDFs and
      `file.attach` (with a display name) for everything else. The prompt is
      held until every attachment RPC has answered, so the model receives
      actual image/PDF tickets rather than only path text. Failures surface as
      `Attachment failed: …` without dropping the prompt. Covered by
      `attachmentAttachRpcMatchesMediaType`.
- [x] **Connector-owned subpages.** Each capabilities connector row now opens
      account and tool dialogs, and the panel browses the hosted catalog.
      Accounts come from `connectors.accounts` and support alias rename
      (`connectors.accounts.rename`) and removal
      (`connectors.accounts.remove`) with dialog confirmation. Tools come from
      `connectors.tools`, render name/facet/description plus source and stale
      state, and force a network refresh. Catalog reads
      `connectors.catalog` with category/description. All requests use the
      active profile through the generic connector RPC bridge. Device-code
      authorization renders the gateway's `instructions`/`hint`, the
      `user_code`, required credential names and advertised tools, and falls
      back to `verification_uri` when no `connect_url` is returned. Covered by
      `capabilitiesConnectorConnectPollsOperationAndReloads`.
- [x] **Global session search in the right rail.** A Search tab debounces
      queries, reads `/api/sessions/search` on the profile worker (never the UI
      thread), renders title/snippet/message count and opens a clicked result
      as the active session. Stale responses are dropped by generation. Covered
      by `rightRailShowsTodoTimelineAndFiltersPrompts`.
- [x] **Right-rail artifact preview.** Each Outputs row keeps the existing
      workspace-open action and adds Preview: text files render in the rail
      (64 KB cap), local images render scaled, relative paths resolve against
      the profile home, and remote values explain that the workspace viewer is
      required. Preview includes its own open action. Covered by
      `rightRailPreviewsArtifactText`.

## Landed on 2026-11-10 (twenty-ninth pass)

Remaining `[~]` scopes: right-rail preview/search, shell extras, connector prompts

- [x] **Right-rail Todo and Timeline panes.** `todo.updated` now feeds
      `ProfileSnapshot.todos`/`todo_revision`; revisions are monotonic and the
      list clears on New Session / session selection. The rail’s Todo tab
      renders priority order with nesting, status markers (`[ ]`, `[>]`,
      `[x]`, `[~]`) and an active/done/cancelled summary. The Timeline tab
      lists user prompts with timestamps from the loaded transcript, filters
      them with a prompt search box and jumps the transcript to a clicked
      row’s durable `row_id`. Covered by
      `rightRailShowsTodoTimelineAndFiltersPrompts`.
- [x] **Right-rail row layout.** Agent, Todo, Timeline and Output rows are now
      inserted into their pane layouts, so they stack and scroll instead of
      sitting at the pane origin.

## Landed on 2026-11-10 (twenty-eighth pass)

Remaining `[~]` scopes: inline cards

- [x] **Inline transcript request cards.** Approval/clarify requests render as
      the last transcript item, owned by `TranscriptWidget` rather than a dock
      above the composer. The card reserves layout height, participates in
      transcript scrolling and disappears (restoring height) when answered or
      cleared. Covered by `approvalRendersAsInlineTranscriptCard`.

## Landed on 2026-11-10 (twenty-seventh pass)

Remaining `[~]` scopes: connectors, fallback-chain editing, model picker

- [x] **Hosted connector connect flow.** Capabilities renders the account
      connector roster (`connectors.list`), starts `connectors.connect` for a
      selected slug, opens the returned authorization link through the
      injectable browser hook, polls `connectors.operation.status` once a
      second and reloads the roster when the operation settles. The section is
      hidden when the gateway has no connector RPC. Covered by
      `capabilitiesConnectorConnectPollsOperationAndReloads`.
- [x] **Ordered fallback-chain editor.** `fallback_providers` is an ordered
      `provider/model` list on the Model page; parsing accepts comma/newline
      input, preserves extra keys (`base_url`) for unchanged pairs and accepts
      legacy string entries on read. Covered by
      `modelSectionEditsOrderedFallbackChain`.

## Landed on 2026-11-10 (twenty-sixth pass)

Remaining `[~]` scopes: delegation routes, fallback chain and chat find

- [x] **Delegation route editors.** Model routes get add/edit/remove (name,
      description, provider/model/base URL, API key that never echoes a saved
      value, reasoning effort, service tier and an ordered
      `fallback_providers` chain) plus a default-route picker. Profile routes
      get description editing, default selection and preserve the existing
      default when adding/removing. Both write the whole
      `PUT /api/delegation/backends/*` block. Covered by
      `delegationEditsModelFallbacksAndProfileRoutes`.
- [x] **Transcript find bar.** `Ctrl+F` opens a shell find bar over the
      transcript; matching is case-insensitive across message blocks, wraps
      with ↑/↓, counts matches, highlights every hit and outlines the active
      one, and `Esc` clears it. Covered by
      `transcriptSearchFindsAndWrapsMatches`.

## Landed on 2026-11-10 (twenty-fifth pass)

Remaining `[~]` scopes: chat right rail and model visibility

- [x] **Chat right rail.** `Ctrl+Shift+B` or the command palette toggles a
      native rail with Context, Agents and Outputs tabs. Context renders
      session id, merged `session.usage` (`used / max · percent`), message
      count, model, reasoning effort and total tokens. Agents reuses the live
      `subagent.*` roster with status/depth/duration. Outputs dedupes
      `:::artifact` and image blocks newest-first and opens the Artifacts
      workspace. Covered by `rightRailShowsContextAgentsAndArtifacts`.
- [x] **Model visibility overlay.** The composer's model menu filters entries
      against local `native.model_visibility` prefs, hides fully-hidden
      providers and adds **Visible models…**. The overlay checkbox list
      persists hidden `provider/model` keys without touching the gateway.
      Covered by `modelVisibilityFiltersPickerAndPersists`.

## Landed on 2026-11-10 (twenty-fourth pass)

Remaining `[~]` scopes: model choice, agent controls, session lineage

- [x] **Per-toolset model choice.** The toolset Configure dialog loads the
      selected provider's catalog from `GET /api/tools/toolsets/<name>/models`
      on the profile worker and offers priced rows. Switching writes
      `PUT /api/tools/toolsets/<name>/model` with `{model, provider}` and keeps
      status inline. Provider changes invalidate stale catalog responses.
      Covered by `capabilityToolsetsAreCheckboxes`.
- [x] **Agent steering and stop.** Live delegated rows expose a steer field +
      `Steer` (`subagent.steer`, `{session_id, subagent_id, text, profile}`)
      and `Stop` (`subagent.interrupt`). Rejections stay inline on that row;
      finished children have no controls. Covered by
      `rendersAgentRosterFromSubagentEvents`.
- [x] **Session lineage and branch navigation.** Session rows parse
      `parent_session_id`, `_branched_from`, `_reset_from` and
      `continuation_kind`. Branches render as `↳`, resets as `↻`, compression
      continuations as `⟳`, tooltips name the parent, and `Open parent`
      navigates a selected child. Covered by `sessionsArchiveAndTransfer`.

## Landed on 2026-11-10 (twenty-third pass)

Remaining `[~]` scopes: toolset/plugin settings, attachment drawer

- [x] **Per-toolset configuration.** Every capability toolset row with a
      configuration matrix exposes `Configure` (`toolsetConfig_<name>`): the
      dialog reads `GET /api/tools/toolsets/<name>/config?profile=…`, lists
      every provider with its readiness, and renders that provider's declared
      env keys. Secrets stay password-masked, blank means “keep”, and Save
      writes the selected provider (`PUT …/provider`) then only non-blank env
      values (`PUT …/env`) before refreshing Capabilities. Covered by
      `capabilityToolsetsAreCheckboxes`.
- [x] **Per-plugin settings forms.** Agent plugins with a manifest
      `settings_schema` get `pluginSettings_<name>`. The form renders
      string/number/boolean/enum/JSON fields and separate secret fields;
      secrets go to profile-scoped `PUT /api/env`, while non-secret values go
      through the gateway RPC `plugins.manage {action:"settings", key, values,
      profile}`. Validation is inline and never echoes a stored secret.
      Covered by `pluginSettingsSaveSchemaAndSecrets`.
- [x] **Composer attachment drawer.** Attachments that no longer fit the chip
      row collapse into a painted `+N` chip (`composer:attachment-drawer`)
      instead of vanishing; its menu lists the hidden files and removes one
      by index while every path still rides the prompt. Covered by
      `composerSteersQueuesAndPastesImages`.
- [x] Full suite green: `hermes_ui_tests` 13, `hermes_settings_tests` 23,
      `hermes_chat_tests` 8, `hermes_click_tests` 14, `hermes_gateway_tests`
      5 passed / 1 skipped, `hermes_sidebar_tests` 5.

## Landed on 2026-11-10 (twenty-second pass)

Platform surfaces: computer-use diagnostics and terminal backend

- [x] **Computer Use panel.** Capabilities now loads
      `GET /api/tools/computer-use/status?profile=…` asynchronously on the
      profile worker, renders platform support, driver version, readiness,
      Accessibility/Screen Recording state, failed checks and backend errors,
      and offers `Grant permissions` only when `can_grant` is true and the
      driver is not ready.
- [x] **Grant lifecycle.** Granting posts
      `POST /api/tools/computer-use/permissions/grant?profile=…`, then polls
      `GET /api/actions/computer-use-grant/status?lines=200` at 1.5 s from a
      page-owned timer until the action exits, refreshes the status, and shows
      failures inline. It never claims that automation is live.
- [x] **Terminal backend picker.** Capabilities lists
      `GET /api/tools/terminal/backends?profile=…` with per-backend probe
      status/detail and writes `PUT /api/tools/terminal/backend?profile=…`
      with `{backend}`. Selection reloads the active row and keeps probe
      guidance visible.
- [x] Tests: `capabilitiesComputerUseStatusAndGrant` asserts rendered
      readiness/permissions/checks, grant dispatch, action polling, status
      refresh and recheck; `capabilitiesTerminalBackendSelectsAndReloads`
      asserts probe text, active selection, PUT body and reload.

## Landed on 2026-11-10 (twenty-first pass)

Notifications: native delivery, per-kind policy and test

- [x] **Platform delivery.** `NotificationService` uses `NSUserNotification`
      on macOS (the unbundled developer binary has no bundle proxy, so
      `UNUserNotificationCenter` is unavailable) and a tray balloon elsewhere.
      A delegate presents alerts even when Hermes is focused and activates the
      app when a notification is clicked.
- [x] **Desktop policy.** The seven desktop kinds (approval, input, turn done,
      turn error, background done, credits, plugin) are local QSettings prefs.
      Completion alerts require the selected session and a background window;
      approval/input also break through for another session while focused;
      global kinds only require backgrounding. A one-second duplicate guard
      records each request id before the first delivery attempt.
- [x] **Shell integration.** Turn completion/error calls the same service the
      page test uses, and newly parked approval/clarify/sudo/secret requests
      produce named alerts (“Approval needed — <session>”). Muting sounds also
      silences the completion cue and OS notification sound.
- [x] **Notifications page.** Master enable, per-kind toggles, mute-sounds and
      `Send test notification` replace the old “not wired yet” stub.
      Permission is requested by the OS on first delivery, and the page tells
      the user where to allow Hermes if nothing appears.
- [x] Tests: `notificationPolicyMatchesDesktopAttentionRules` covers
      foreground/background, active/other session, global and disabled kinds;
      `notificationsPersistKindsAndSendTest` drives the page toggles, verifies
      QSettings persistence, the master/kind dependency, sound muting, and both
      successful and rejected test deliveries.

## Landed on 2026-11-10 (twentieth pass)

Skill hub: sources, remote search, preview and security scan

- [x] **Async hub requests.** `Supervisor::{ListSkillHubSources,
      SearchSkillHub,PreviewSkillHub,ScanSkillHub}` dispatch to the profile
      worker and return through `HubRequestFinished`. Callbacks run on the UI
      thread; a monotonic token keeps overlapping searches and previews
      independent, and a slow GitHub/index fan-out cannot freeze the shell.
- [x] **Sources.** The browse dialog loads `GET
      /api/skills/hub/sources?profile=…` and fills the source picker with
      labels, including unavailable/rate-limited markers.
- [x] **Remote search.** `skillHubQuery` + `skillHubSource` +
      `skillHubSearch` call `GET /api/skills/hub/search?q=…&source=…&limit=20
      &profile=…`; results carry name, description, source, trust and an
      installed marker from the response's `installed` map. Timed-out sources
      are named in the result summary.
- [x] **Preview.** `skillHubPreview` calls `GET /api/skills/hub/preview` and
      renders the manifest plus full `skill_md` without installing.
- [x] **Security scan.** `skillHubScan` calls `GET /api/skills/hub/scan` and
      renders verdict, policy, summary and findings. A `policy: "block"`
      verdict disables Install for that row.
- [x] Tests: `capabilityToolsetsAreCheckboxes` injects asynchronous sources,
      search, preview and scan hooks, then asserts the source picker, query,
      source filter, limit, result row, preview markdown and scan verdict.

## Landed on 2026-11-10 (nineteenth pass)

Chat message actions: edit-and-resend and regenerate

- [x] **Durable row ids.** `ChatMessage.row_id` is populated from the
      `id` returned by `GET /api/sessions/<id>/messages`. Rewind targets are
      durable message rows, not transcript ordinals, so a re-render cannot
      retarget the wrong turn.
- [x] **Truncating submit.** `prompt_submit.{h,cc}` builds the desktop's
      truncation params: `confirm_truncate`, `truncate_before_row_id`, plus
      `confirm_empty_truncate` / `confirm_deep_truncate` only when the cut
      empties history or archives later user turns. `GatewaySocket::SendPromptAt`
      sends those params through `prompt.submit`; a plain send stays identical.
- [x] **Worker rewind.** `ProfileWorker::SendPromptAt` drops the target user row
      and every later transcript row before appending the optimistic
      replacement, then submits with the truncation params.
- [x] **Transcript actions.** User rows get an `Edit` chip (and context-menu
      “Edit and resend”); completed assistant rows with a preceding user turn
      get `Retry` (context menu “Regenerate reply”). The shell opens a prefilled
      editor for edits and finds the preceding user turn for regeneration;
      running turns refuse with an inline note instead of truncating under a
      live generation.
- [x] Tests: `messageActionsEditAndRegenerate` clicks the painted chips and
      asserts the exact durable row ids and edited text reach the handlers;
      `promptSubmitTruncationIsExplicit` covers ordinary sends, first/middle/last
      user-turn classification, missing row ids and the presence/absence of
      each confirmation bit.

## Landed on 2026-11-10 (eighteenth pass)

MCP raw editor and Telegram pairing onboarding

- [x] **MCP config editor.** Each server row has `mcpEdit_<name>`, opening
      `mcpEditDialog_<name>` with the raw `mcp_servers` entry
      (`GET /api/config?include_defaults=false&profile=…`). Saving parses the
      JSON object, re-inserts it under the same name and replaces the whole map
      through `PUT /api/mcp/servers?profile=…`, so untouched env/headers and
      other servers survive. This is the parity path for per-server `tools`
      selection (e.g. `"tools": ["search"]`) without a redacted-summary
      round-trip.
- [x] **Telegram onboarding.** `messagingOnboard_telegram` starts a dashboard
      pairing, shows the suggested bot and deep link, opens it through the
      injectable `open_url`, and polls every 500 ms. `ready` prefills the
      owner's numeric id into `messagingOnboardAllowedUsers` and enables Apply;
      Apply posts `{allowed_user_ids}` to
      `/api/messaging/telegram/onboarding/<id>/apply?profile=…`, reports
      `needs_restart`, and refreshes the messaging page. Closing without Apply
      cancels the pairing through DELETE.
- [x] Tests: `capabilityToolsetsAreCheckboxes` edits the raw memory entry,
      adds a tool limit and asserts the untouched server's auth header survives
      the whole-map replace; `rendersWorkspacePagesFromSnapshot` starts
      Telegram setup, opens the deep link, observes the polled owner id, applies
      it and asserts abandoned setups are cancelled.

## Landed on 2026-11-10 (seventeenth pass)

Capabilities: approved MCP catalog browser

- [x] **Catalog browse.** `mcpCatalogBrowse` opens `mcpCatalogDialog` from
      `GET /api/mcp/catalog?profile=…` through
      `PageContext::list_mcp_catalog`/`Supervisor::ListMcpCatalog`. Rows show
      transport, description and installed state; installed entries disable
      their Install button.
- [x] **Declared credentials only.** Entries with `required_env` render one
      password field per declared key (`mcpCatalogEnv_<name>_<KEY>`); the
      install body sends only the entries the user filled plus `{name,
      enable:true}` to `POST /api/mcp/catalog/install?profile=…`.
      Git-bootstrap entries report that the gateway is installing in the
      background instead of claiming completion.
- [x] Test: `capabilityToolsetsAreCheckboxes` opens the catalog, fills the
      declared `ROOT` value and asserts the profile-scoped install path/body.

## Landed on 2026-11-10 (sixteenth pass)

Sessions bulk operations/retention and MCP OAuth

- [x] **Bulk sessions.** The Sessions list is multi-select. Delete always posts
      `POST /api/sessions/bulk-delete` with the selected ids; archive/restore
      fan out one fenced `PATCH /api/sessions/<id>` per selected row.
      `sessionDeleteEmpty` issues `DELETE /api/sessions/empty`.
- [x] **Retention.** `sessionPruneDays` (1–3650) and
      `sessionPruneArchived` feed `POST /api/sessions/prune` with
      `{older_than_days, include_archived}`. Destructive controls are direct
      buttons, matching the existing no-confirmation pattern.
- [x] **MCP OAuth.** Eligible HTTP servers (`auth != header`, `source !=
      plugin`) get `mcpOAuth_<name>`; start returns a flow id + authorization
      URL, the URL is opened through injectable `open_url`, and a 250 ms timer
      polls `GET /api/mcp/oauth/flows/<id>` until `approved`/`error`.
      `mcpOAuthCancel_<name>` settles an abandoned flow with DELETE. Approval
      re-probes the server for its tool count and refreshes the page.
- [x] Tests: `sessionsArchiveAndTransfer` covers multi-select archive/restore,
      bulk-delete ids, delete-empty and retention body;
      `capabilityToolsetsAreCheckboxes` covers start/open/poll/cancel and the
      re-probe status.

## Landed on 2026-11-10 (fifteenth pass)

Messaging platform credential setup

- [x] **Configure dialog.** Platforms that advertise `env_vars` get
      `messagingConfigure_<id>`, opening `messagingConfigDialog_<id>` with one
      field per gateway-declared setting (`messagingEnv_<id>_<KEY>`), the
      gateway prompt/help text, required markers, password echo for
      `is_password`, and a `Clear saved value` checkbox
      (`messagingClear_<id>_<KEY>`) for values already on disk.
- [x] **Safe writes.** Existing secrets are shown only as the gateway's
      redacted placeholder and are never sent back. Save builds `{env,
      clear_env}` from non-empty edits plus checked clears (omitting both when
      nothing changed) and PUTs `/api/messaging/platforms/<id>?profile=…`.
      Enable toggles now carry the same profile scope.
- [x] **Connection test.** `messagingTest_<id>` calls
      `POST /api/messaging/platforms/<id>/test` through
      `PageContext::test_platform`/`Supervisor::TestMessagingPlatform` and
      writes the gateway's `message` plus `auth_posture` into
      `messagingTestStatus_<id>`; transport errors show the same slot.
- [x] Tests: `rendersWorkspacePagesFromSnapshot` opens the Telegram config,
      verifies the token field is blank/password-masked, saves a new token,
      clears the allowlist, asserts the body contains only the new token plus
      `clear_env: ["TELEGRAM_ALLOWED_USERS"]`, and runs the connection probe
      to verify the inline status.

## Landed on 2026-11-10 (fourteenth pass)

Capabilities: skill authoring, official hub install and MCP removal

- [x] **Skill editor.** Every skill row has `Edit` (`skillEdit_<name>`), which
      reads `GET /api/skills/content?name=…&profile=…` through the injected
      `PageContext::get_skill_content`/`Supervisor::GetSkillContent` path and
      opens a full `SKILL.md` editor (`skillEditContent`) with the source path
      shown. Save is a full rewrite via `PUT /api/skills/content?profile=…`;
      read failures surface inline on `skillStatus_<name>`.
- [x] **New skill.** `skillNew` opens a name/category/SKILL.md form and posts
      the gateway's `SkillCreate` shape to `POST /api/skills?profile=…`, with
      the category omitted when blank.
- [x] **Official hub browser.** `skillHubBrowse` lists the entire local
      official catalog through `GET /api/skills/hub/official?profile=…`
      (`Supervisor::ListOfficialSkills`). The dialog marks installed entries,
      disables their Install button, and starts uninstalled entries through
      `POST /api/skills/hub/install?profile=…` with `{identifier}`. Remote hub
      search remains unported.
- [x] **Hub lifecycle.** `Update installed` posts `POST
      /api/skills/hub/update?profile=…`.
      Skills whose provenance is `hub` get `skillUninstall_<name>` and
      `POST /api/skills/hub/uninstall?profile=…` with `{name}`.
- [x] **MCP removal.** `mcpRemove_<name>` issues `DELETE
      /api/mcp/servers/<name>?profile=…`; the previous add/test/toggle paths
      now carry the same profile scope so a stale active profile cannot
      redirect a config write.
- [x] Tests: `capabilityToolsetsAreCheckboxes` now drives the skill editor
      (read path, rewrite body, profile-scoped route), creates a new skill with
      a category, opens the official catalog (installed-row gating and install
      dispatch), and removes an MCP server. `rendersEveryDataPageFromSnapshot`
      asserts the new authoring/hub/removal controls exist.

## Landed on 2026-11-10 (thirteenth pass)

Rich markdown in the transcript

- [x] **Headings H1–H6.** The parser carries the level in `MessageBlock.detail`
      and the transcript scales the font/line height per level instead of
      flattening every heading to one size.
- [x] **Lists.** `-`, `*`, `+` bullets, numbered `1.`/`1)` items with the
      marker painted in the gutter, and `- [ ]` / `- [x]` task rows with real
      checkbox glyphs. Existing plain bullets keep their dot.
- [x] **Quotes, rules and tables.** `>` quotes get an italic secondary-colour
      body and a left bar; `---`/`***` paint a hairline; pipe tables require a
      delimiter row, honour `:---`, `---:` and `:---:` alignment, and render
      through `QTextDocument` with escaped cells.
- [x] **Inline links and strikethrough.** `[label](url)` becomes an anchor and
      `~~text~~` becomes `<s>`; unclosed emphasis is closed at the end of a
      streaming token.
- [x] Tests: `markdownParsesRichBlocksAndInlineSpans` covers every new block
      kind and inline span; `markdownBlocksRenderInTranscript` exercises the
      full layout/paint path with a mixed markdown message.

## Landed on 2026-11-10 (twelfth pass)

Session transfer, fenced session actions, and skill enable/lock controls

- [x] **Session export/import.** Settings → Sessions gained Export and Import.
      Export streams `GET /api/sessions/{id}/export?profile=…` straight to a
      `QSaveFile`; Import accepts the gateway's `{"sessions":[…]}` payload, one
      exported session object, or a bare array (`ParseSessionImport`), then
      posts `/api/sessions/import` with `request_id` and the anchor's
      `expected_revision` (zero for a new anchor). Tested by
      `sessionsArchiveAndTransfer` plus
      `sessionImportAcceptsBareAndWrappedShapes`.
- [x] **Sessions page archive/restore.** Rows expose Archive/Restore and the
      worker routes session PATCH/DELETE through `PatchSession` /
      `DeleteSession`, so the gateway receives the required fence
      (`request_id`, `expected_revision`, delete generation) instead of a raw
      settings-page mutation. Covered by the same widget test.
- [x] **Skills enable/disable + lock.** Every capability skill row now has a
      real enabled checkbox (`PUT /api/skills/toggle?profile=…`) and a
      Lock/Unlock button (`PUT /api/skills/lock?profile=…`), with provenance and
      usage in the row detail. Covered by `capabilityToolsetsAreCheckboxes`.
- [x] Follow-up landed: session bulk actions/retention (sixteenth pass),
      capability catalog install (seventeenth pass), and the skills-hub
      sources/search/preview/scan/install/update/uninstall flow (twentieth
      pass).

## Landed on 2026-11-10 (eleventh pass)

Capabilities: MCP add + live probe, and the enable route corrected

- [x] **MCP add server.** The Capabilities page has an inline form (name, URL or
      command, args) posting the gateway's `MCPServerCreate` shape to
      `POST /api/mcp/servers`. URL servers send `url`; stdio servers send
      `command` plus whitespace-split `args`.
- [x] **MCP test.** Every server row has Test, routed through
      `Supervisor::TestMcpServer` → `POST /api/mcp/servers/{name}/test`; the
      result paints inline (`N tools` or the gateway error) instead of a modal.
- [x] **Enable/disable route fixed.** The toggle was POSTing the server root;
      the gateway only accepts `PUT /api/mcp/servers/{name}/enabled`.
- [x] Covered by `capabilityToolsetsAreCheckboxes` (add URL + stdio bodies,
      probe callback and inline status, PUT enable route).
- [x] Follow-up landed: whole-map MCP config edit/remove and raw JSON editing
      (eighteenth pass), dashboard-mediated OAuth (sixteenth/seventeenth
      passes), approved catalog browse/install (seventeenth pass), and the
      skills hub (twentieth pass).
- [ ] Still missing from this area: connectors (and their OAuth) and a
      dedicated per-server tool-selection UI beyond raw JSON.

## Landed on 2026-11-10 (tenth pass)

Artifact upload/download over the gateway's binary and multipart routes

- [x] **Artifact upload.** Upload picks a local file (or uses the injectable
      picker in tests), POSTs it as multipart to
      `/api/files/upload-stream?profile=…` with `path` + `overwrite=true`, and
      refreshes the listing. `GatewayHttp::PostMultipart` extends the existing
      attachment upload with extra form fields; the 64 MiB ceiling is shared.
- [x] **Artifact download.** Download writes `/api/fs/download?path=…&profile=…`
      through `GatewayHttp::DownloadFile`, which reads the binary body (no JSON
      parse) and commits atomically with `QSaveFile`. The button is disabled for
      folders and `..`.
- [x] Covered by `rendersWorkspacePagesFromSnapshot`: injected pickers and
      callbacks assert the exact destination (`/tmp/work/photo.png`), local
      source, refresh, and remote/save paths for download.

## Landed on 2026-11-10 (ninth pass)

Composer completion/history/meter, cron editing, Bots and Webhooks editors, custom endpoints, all keybinds

- [x] **`@` path mentions.** Typing `@` (at start or after whitespace) opens a
      filtered completion popup. Directories are listed through
      `GET /api/fs/list?path=&profile=` via `Supervisor::ListDirectory`;
      Up/Down/Tab/Enter/Esc work, accepting a folder re-lists it, and accepting
      a file inserts a trailing space. Bare `@dir/file` tokens are promoted to
      the gateway's typed `@file:"…"` / `@folder:"…"` shape on submit
      (`Composer::PromotePathRefs`). Tested in
      `composerMentionsCompletePathsAndPromoteRefs` (nested listing, menu
      filtering, promotion, already-typed refs untouched).
- [x] **Prompt history + context meter.** Up/Down browse user prompts derived
      from the live transcript, preserving multiline caret movement and the
      in-progress draft; the composer paints `used / max · pct%` beside the
      model/reasoning chips from `session.usage` (`MergeSessionUsage` accepts
      nested and legacy frames). Tests: `composerHistoryBrowsesAndRestoresDraft`,
      `composerTokenMeterFormatsContextUsage`,
      `sessionUsageMergesNestedAndLegacyFrames`.
- [x] **Cron job editing.** Every job row has Edit, seeding a dialog with
      name/schedule/prompt/deliver/model/provider. Only changed fields are sent
      as `{"updates": …}` to `PUT /api/cron/jobs/{id}?profile=…`, so untouched
      values are never rewritten. Covered by `pageActionsRouteThroughWorkerCallback`.
- [x] **Bots editors.** Profile rows gained model assignment
      (`PUT /api/profiles/{name}/model`), description and SOUL editors, plus
      export (`GET …/export`) and import (`POST /api/profiles/import`) through
      synchronous `PageContext` hooks. Tested by
      `botsEditModelDescriptionSoulAndExportArchive`.
- [x] **Webhooks page.** Real page state from `GET /api/webhooks`, enable
      toggle, create dialog (name/events/prompt/deliver) and per-subscription
      enable/delete. The one-time signing `secret` is surfaced in the page
      notice rather than discarded. Tested by
      `webhooksEnableCreateAndToggleSubscriptions`.
- [x] **Custom endpoint editor.** Add/Edit dialog with name, base URL, model,
      API key, context length, Discover, Make default and Validate;
      `POST /api/providers/custom-endpoints/validate` is global while save is
      profile-scoped. Tested by `customEndpointEditorValidatesAndSaves`.
- [x] **Keybinds are complete and resettable.** The Keybinds page now lists all
      19 actions the window registers (palette alternates, model picker,
      keybinds, sidebar/statusbar, page scroll, session/profile cycling,
      profiles 1–4, find, chat, stop, theme, settings). Rebind normalises to a
      portable sequence and updates the row; Reset restores the default and
      clears the stored override. Covered by `rendersEveryDataPageFromSnapshot`.

## Landed on 2026-11-10 (eighth pass)

Artifacts editor, cron history + blueprints, plugin lifecycle, local models, delegation, attachment chips

- [x] **Artifacts is an editor, not a viewer.** The preview pane is a
      `QPlainTextEdit` seeded from `/api/fs/read-text`; Save routes through
      `ProfileWorker::SaveArtifact`, which re-reads the file first and refuses
      when the bytes on disk differ from the seed (stale-write guard) before
      `POST /api/fs/write-text`. New folder (`POST /api/files/mkdir`), New file
      (write-text with empty content) and Delete (`DELETE /api/files`,
      recursive) are real actions; the page shows the `GET /api/fs/git-root`
      root. Images and PDFs use `/api/fs/read-data-url` — images paint in a
      `QLabel`, PDFs get an Open button.
- [x] **Cron got the rest of its surface.** Per-job run history
      (`GET /api/cron/jobs/{id}/runs`; bounded to ten jobs × five runs), delivery
      targets (`/api/cron/delivery-targets`) in both the job summary and the
      create form, blueprint catalog (`/api/cron/blueprints`) with a form built
      from each blueprint's own field schema and
      `POST /api/cron/blueprints/instantiate`, and a New task dialog posting
      `/api/cron/jobs` `{name, schedule, prompt, deliver}`.
- [x] **Plugins: enable/disable/update/remove/install.** The page reads
      `/api/dashboard/plugins/hub` (77 agent plugins live) — checkboxes hit
      `POST /api/dashboard/agent-plugins/{name}/enable|disable`, Update and
      Remove appear only for git-installed/removable rows. The catalog is
      deliberately lazy (`plugincatalog` page state, "Browse catalog") because
      the route scans every installed plugin; Install posts
      `/api/dashboard/agent-plugins/install` `{catalog_name, enable}`.
- [x] **Local models page** (`Settings → Local models`): runtime/server status,
      hardware panel (UMA, VRAM, available RAM), catalog with download state
      and fit summary; Download (`/api/local-models/download`), Use
      (`/activate`), Delete (`DELETE /api/local-models/models/{id}`), server
      start/stop (`/api/local-models/server`).
- [x] **Delegation page** (`Settings → Delegation`): backend picker bound to
      `PUT /api/delegation/backend`, blocked reason, model-route rows, and
      profile routes with Add/Remove writing
      `PUT /api/delegation/backends/profile_routes`.
- [x] **Attachment chips.** Staged uploads render as painted chips between the
      editor and the toolbar with a `×` hit target (`composer:attachment:N` is
      in the click audit). Submit names every path under
      `Attached files:`, and dropping files on the composer stages them through
      the same 0600 worker copy as the picker. Duplicates are ignored.
- [x] Live verification: `scripts/verify_artifact_editor.py` creates a folder,
      writes/reads two versions and deletes it, then disables and re-enables
      `browser-browserbase` reading the hub back each time — observed
      `{'write': True, 'read_back': 'second version', 'delete': True}` and
      `{'before': 'enabled', 'toggled': 'disabled', 'restored': 'enabled'}`.
      `PUT /api/delegation/backend {backend: default}` answered
      `{"ok":true,"backend":"default"}` and routing re-read `configured=default`.

## Landed on 2026-11-10 (seventh pass)

Booleans are checkboxes, capabilities toggle, pre-made themes

- [x] **Capabilities toggle.** Every toolset row carries a real checkbox bound
      to `PUT /api/tools/toolsets/{name}` `{enabled}` — the same route the CLI
      picker uses. Verified end to end through the widget: clicking
      `toolsetToggle_video` answered `{"ok":true,"enabled":true}` and the list
      reflected it; clicking again restored it.
- [x] **Any boolean reads as a checkbox** (`BoolCheck` in settings_pages.cc):
      interactive where a mutation exists (toolsets, MCP servers, messaging
      platforms), inert (transparent to the mouse, no focus, "read-only" tip)
      where it is only a value (gateway enabled / signed in).
- [x] **Themes page** (`Settings → Themes`): 15 cards — Hermes built-in plus the
      14 schemes of https://github.com/beatreichenbach/qt-themes (MIT),
      vendored under `resources/themes/*.json` and compiled by
      `scripts/gen_theme_presets.py`. Each card has a live swatch and a Use
      button; the active scheme is marked and persisted
      (`native.theme.preset`), restored before the first paint. Choosing
      Light/Dark on the Appearance page clears the preset.
- [x] **Skill commands were listed as `//name`.** The catalog's skill keys
      arrive already slashed and the parser prepended another; names are now
      normalised to exactly one leading slash (0 double-slash entries, 157
      skills listed).
- [x] Probes: `--click-object <objectName>` (polls until the widget exists,
      clicks buttons/checkboxes), `--settings <page>`.

## Landed on 2026-11-10 (sixth pass)

Key capture + the whole `/` registry wired into the composer

- [x] **Cmd+K works everywhere.** Shortcuts no longer rely on QShortcut: every
      binding is dispatched from an application-level event filter, so a
      focused editor, an open popup or a dialog can no longer swallow the
      chord. Bare keys are still skipped while a text field has focus.
      Verified with the composer holding text: Cmd+K opens the palette and the
      draft survives.
- [x] Desktop chords added alongside the existing ones: `Cmd+P` (palette —
      the desktop binds both), `Cmd+Shift+M` (model picker), `Cmd+/` (keybinds
      page), `Cmd+B` (sidebar), `Cmd+Shift+S` (status bar), PageUp/PageDown
      (transcript), `Ctrl+Tab`/`Ctrl+Shift+Tab` (next/previous session).
      `--key-audit` now reads the binding table itself.
- [x] **Fixed the stylesheet nobody was applying.** The global QSS had a gap in
      its `%N` numbering (`%12` missing) and used the multi-arg `arg()`
      overload, so Qt logged "1 argument(s) missing" and left `%1`… in place —
      every widget style in that sheet was dropped. Placeholders are now
      contiguous and applied chained.
- [x] Slash popup fed by the gateway catalog (`commands.catalog`, 258 entries):
      typing `/st` lists `/stop`, `/steer`, `/status`, `/statusbar` — commands
      that exist nowhere in the native table. Native commands keep their own
      description (the gateway's `/stop` kills background processes; ours stops
      the turn).

## Landed on 2026-11-10 (fifth pass)

`/steer` really steers, visible slash feedback, copy on every bubble

- [x] **`/steer` was broken, not missing.** `session.steer` is fenced on
      `execution_generation`; the shell sent `{session_id, text}` only, so the
      gateway answered `invalid_params`, and the old snapshot-gated call often
      fired after the turn ended (`stale_generation`). Now: event frames carry
      the owner's execution stamp, the worker tracks it, and the request sends
      it. When no turn holds the session the text is delivered as the next
      prompt instead of erroring. **Verified end-to-end**: probe session →
      tiny turn → `/steer …` → `status=queued` from the gateway.
- [x] **Slash feedback is visible.** Every command now appends a centred muted
      notice to the transcript (`MessageRole::kSystem`), so "usage: /steer
      <text>", "Steer queued…", "Queued for the next turn (2 in queue)…",
      "`/x` is not available for this session" are impossible to miss. Worker
      `status_text` used to be invisible: it was never rendered anywhere.
- [x] **Copy any generated text.** Every user bubble, assistant message and
      tool row has a Copy chip (right of the user bubble, on the assistant
      author row, right of each tool row); copying a tool row yields its
      output. Reasoning and code blocks keep their own Copy affordances.
      `chat:copymsg:<n>` targets are in the click audit, so a dead chip fails
      the suite.
- [x] **Fenced delete fixed.** `DELETE /api/sessions/{id}` needs
      `request_id` + `expected_revision` + `expected_generation` (read from
      `/mutation-snapshot`); a delete without the generation is
      `400 invalid_params`, a stale one `409 admission_conflict`. Session
      deletes now actually delete, verified by re-listing.

## Landed on 2026-11-10 (fourth pass)

Whole slash registry + generation feedback

- [x] **Every slash command, not a hand-picked list.** The shell now fetches
      `commands.catalog` over the gateway socket (7 categories, **258 entries**
      including plugin/skill commands) and feeds the composer popup. Commands
      without a native fast path are executed with `slash.exec`
      (`{session_id, command}`) and their output is rendered into the
      transcript. `/steer`, `/queue`, `/status`, `/context`, `/usage`,
      `/compress`, `/undo`, `/retry`, `/save`, `/agents`, `/plan`, … all work
      this way; unknown-but-command-shaped input returns the gateway error
      instead of being sent to the model as prose.
      Local fast paths stay native: `/new`, `/stop`, `/title`, `/reasoning`,
      `/model`, `/profile`, `/attach`, `/settings`, `/theme`, `/search`,
      `/help`.
- [x] Commands the shared gateway refuses over `slash.exec` are implemented on
      the session's own verbs, like the desktop: `/steer` (`/s`) calls
      `session.steer` while a turn runs and sends the text as a new prompt when
      idle; `/queue` (`/q`) keeps a native next-turn queue that drains when the
      turn finishes; `/retry` re-sends the last user prompt; `/undo [N]`
      re-runs the prompt N turns back (it does **not** truncate stored
      history — stated in the status line).
- [x] Native queue is visible: `/queue` with no arguments reports the depth and
      a transient confirms each enqueue.
- [x] Verified live: `/status` ran through the worker and returned 493 bytes
      of transcript output; the popup lists `/steer`, `/status`, `/sessions`,
      `/skills`, `/save`, `/snapshot`, `/stop`, `/subgoal`, `/skin`.
- [x] **Active-generation feedback**: the composer shows `generating Ns` with
      marching accent dots (140 ms tick) in place of “enter to send”; the status
      bar dot pulses; the running session row in the sidebar gets a pulsing
      accent dot.
- [x] **Completion feedback**: soft two-tone chime (`Qt6::Multimedia`,
      generated `resources/sounds/complete.wav`, falls back to the system beep
      when Multimedia is absent), a 1.4 s accent underline flash under the title
      bar, and a `Generation complete · 12.4s` status-bar message.
- [x] Both cues are user-toggleable (“Completion sound” / “Completion flash” in
      the rail “…” menu) and persist through native prefs.
- [x] Probes: `--slash <command>`, `--demo-running`, `--demo-complete`.

## Landed on 2026-11-10 (third pass)

Slash commands and key capture

- [x] **Composer slash commands** with completion: typing `/` opens a popup
      (filtered as you type, Up/Down/Tab/Enter/Esc) listing `/new` (`/reset`,
      `/clear`), `/stop`, `/reasoning [low|medium|high]`, `/model`, `/profile
      [name]`, `/title <text>`, `/attach [path]`, `/settings`, `/theme`
      (`/skin`), `/search` (`/find`), `/help` (`/commands`). Enter on a
      complete command runs it; unknown `/…` text still goes to the agent so
      gateway-registered commands keep working. `/help` opens the palette
      listing every command.
- [x] **Cmd+K was dead because two shortcuts shared the sequence** (focus
      composer + command palette), and Qt fires neither on an ambiguous
      shortcut. All window shortcuts now go through one registry that rejects
      duplicates, and `--key-audit` prints `action<TAB>sequence` and exits
      non-zero on a clash. Audit output: command_palette Ctrl+K, find_sessions
      Ctrl+F, interrupt Ctrl+., new_chat Ctrl+N, profile_1..4 Ctrl+1..4,
      settings Ctrl+,, theme Ctrl+Shift+D.
- [x] Shortcut capture test: `Ctrl+K` typed **into the composer editor** still
      reaches the window shortcut (the editor does not swallow it).
- [x] Popup stylesheet colours keep their alpha (`QColor::name()` dropped it,
      painting the selected row as an opaque near-black bar).
- [x] Probes: `--composer-text`, `--press <sequence>`, `--key-audit`.

## Landed on 2026-11-10 (second pass)

Clickable-area audit and the bugs it found

- [x] **Click audit** (`native_click_audit`): every custom-painted surface now
      exposes `ClickTargets()` (name + rect + modal flag) and the audit clicks
      each one, failing on a dead region. Covers titlebar search/settings,
      sidebar nav / profile chips / add-profile / new chat / session rows,
      composer send, reasoning headers, code-block Copy and tool rows.
- [x] **Titlebar magnifier** now opens the command palette (it only re-focused
      an already-focused composer, so it looked inert). The palette also lists
      the five workspace pages and nineteen settings pages.
- [x] **Code block Copy** — the icon painted in the code header was dead; it
      now copies the block to the clipboard and shows “Copied” for 1.2 s.
- [x] **Tool rows expand** — the chevron was dead; clicking a tool row shows
      the full output (previously only a one-line, clipped summary).
- [x] **Reasoning expands both ways** — audit proves collapsed → expanded →
      collapsed. Header gained a `show` / `hide` hint, and
      `reasoning.available` no longer appends the body a second time when the
      provider already streamed deltas (that duplication made the panel huge
      and made expanding look broken).
- [x] Transcript right-click: **Copy message / Copy reasoning**.
- [x] Artifacts file rows open on a **single click** (and still on Enter).
- [x] Archived sessions are hidden; the rail “…” menu has **Show archived**
      and the row menu offers **Unarchive**.
- [x] Probes for real-window checks: `--demo` (synthetic transcript with
      reasoning, code and tool rows), `--demo-click <target>`, `--session <id>`.

## Landed on 2026-11-10

Shell / navigation

- [x] Sidebar workspace nav rail, matching the desktop `SIDEBAR_NAV` entry
      points: **Capabilities, Messaging, Artifacts, Scheduled tasks, Bots**
      (icon + label rows, hover/active state, tooltips). Clicking opens the
      page in the chat column; the sidebar and title bar stay put.
- [x] `PagesView`: the workspace-page host (title, Refresh, Close, scrollable
      body) wired to the active profile's snapshot.
- [x] Workspace pages (not Settings subpages):
  - **Capabilities** — toolsets, skills, MCP servers with enable toggles.
  - **Messaging** — all gateway platforms with enabled toggles
    (`PUT /api/messaging/platforms/{id}`).
  - **Artifacts** — file-system browser (`/api/fs/default-cwd`,
    `/api/fs/list`), directory navigation and a preview pane
    (`/api/fs/read-text`, binary/large notice).
  - **Bots** — profile roster with New bot / Open / Delete
    (`GET/POST/DELETE /api/profiles`).
  - **Scheduled tasks** — cron jobs with pause/resume/run/delete.
- [x] Profile rail **“+” actually adds a profile**: name prompt →
      `POST /api/profiles` → worker pool reload → switch to the new profile.
      Deleting a profile from the Bots page removes it from the rail.

Sessions

- [x] Session row context menu: **Pin / Unpin, Rename, Archive, Delete**
      (right-click any row).
- [x] **Pinned section** at the top of the sidebar with a pin glyph; TODAY /
      EARLIER follow (search switches to a single RESULTS section).
- [x] Fenced mutations: every `PATCH`/`DELETE /api/sessions/{id}` now carries
      `request_id` + `expected_revision`. Without them the gateway answers
      `409 client_upgrade_required` — verified against the live gateway and
      re-verified end-to-end through the app's own worker
      (`--pin <session-id>` → row pinned → restored).

Chat

- [x] **Reasoning blocks** stream: `reasoning.delta` / `thinking.delta` /
      `reasoning.available` are folded onto the streaming answer instead of
      being dropped, rendered as a collapsible “Thinking…” / “Thought”
      panel (auto-expanded while thinking, collapsed when the turn ends,
      click the header to toggle).
- [x] Streaming answers re-parse markdown while deltas arrive (headings,
      lists, fences appear during the turn instead of only on completion).

Tooling / verification

- [x] `--page <id>` and `--pin <session-id>` probe flags for screenshots and
      end-to-end checks.
- [x] `scripts/gen_feature_inventory.py` → `docs/desktop-feature-inventory.md`
      (routes, areas, every settings file, keybind actions, 307 REST routes,
      26 event methods, native LOC).
- [x] `scripts/verify_pin_roundtrip.py` (control-socket ticket → PATCH →
      read-back → restore).
- [x] Native tests added: `native_sidebar` (nav routing, add-profile hit
      target, pinned-first row selection), `native_chat` (reasoning collapse/
      expand, post-turn collapse), plus workspace-page rendering
      (messaging toggles route the PUT, Bots rows, artifacts list/preview).

Observed verification (this machine, 2026-11-10):

- `cmake --build build -j8` clean; `ctest` **5/5 passed**
  (`native_ui`, `native_settings`, `native_sidebar`, `native_chat`,
  `gateway_config`).
- Offscreen screenshots: sidebar nav + Artifacts (listing + preview),
  Capabilities (29 toolsets), Messaging (33 platforms, toggles), Bots
  (6 profiles).
- Live gateway: files listing, session rows, model options (9 providers) and
  the pin round-trip all answered. Messaging route takes ~6 s on this box
  (it stats every platform), so its page builds late — not an error.

## Native today

- [x] Chat shell, sidebar, profile switch, composer, streaming, tool rows,
      clarify/approval dock, stop, new chat, session search, command palette,
      light/dark theme.
- [x] Settings route shell: left rail, breadcrumb/title, search, Close,
      schema-driven form, sparse autosave, error/status line.
- [x] Nine config sections generated from the desktop `SECTIONS` table
      (**106 config keys**): Model, Chat, Appearance (theme only), Workspace,
      Safety, Browser, Memory & Context, Voice, Advanced.
- [x] Settings data pages: Providers, API keys, Credential vault, Gateway,
      Notifications, Keybinds, Sessions, Plugins, Billing, Scheduled tasks,
      Capabilities, About/updates.
- [x] Workspace pages: Capabilities, Messaging, Artifacts, Bots, Scheduled
      tasks (see above).

## Remaining work — by desktop area

Each row names the desktop source in the inventory so it can be diffed rather
than guessed. Nothing below is claimed as ported.

### Chat (`src/app/chat`, 431 files)

- [x] Attachments: the composer's paperclip stages a picked file as a 0600
      copy under `<profile>/uploads/`; staged files render as removable chips
      with thumbnails, drag-drop stages dropped files, clipboard images paste
      as staged PNGs, and files that overflow collapse into a `+N` drawer.
      Submission queues `image.attach` / `pdf.attach` / `file.attach` before
      `prompt.submit` and holds the prompt until every ticket is accepted.
- [x] Markdown: paragraphs, headings, bullets/tasks, code, quotes, rules,
      tables, links, bold/italic/strike, inline code, images, diff/math/
      mermaid fences, display/inline math, artifact embeds, footnotes and
      citations.
- [x] Inline transcript cards: clarify/approval render as the last transcript
      item with reserved layout height and a respond handler; clearing the
      request restores the transcript height. Scroll anchoring and per-card
      focus remain polish items on top of the card contract.
- [~] Message actions: the desktop hover row (turn duration,
      branch-in-new-chat, copy, copy full response, read aloud, refresh, react)
      is painted under every assistant reply and reserves height so it cannot shift the transcript.
      Copy/read-aloud/edit/regenerate land through row-targeted
      `prompt.submit` truncation; assistant read-aloud uses `/api/audio/speak`
      plus Qt Multimedia; Branch in new chat / rewind / delete conversation /
      reactions use `session.branch`, `session.undo`, REST delete and
      `message.react`; the right-click menu keeps copy reasoning and pin.
      Pin-to-context works through a client-side prompt prefix
      (`Ctrl+Shift+P`, max 8 replies) but still lacks a gateway pin contract,
      so desktop-equivalent shared semantics are unverified.
- [x] Composer: the full slash registry + completion, attachment
      chips/thumbnails/clipboard paste/overflow drawer, prompt history,
      context/token meter, `@` path mentions, Enter-steer/Ctrl+Enter-queue,
      file dictation and gateway live-voice input land. The microphone button
      drives `voice.toggle`/`voice.record`, renders partial transcripts and
      inserts final transcripts once per serial; `Ctrl+Shift+M` provides the
      keyboard path.
- [x] Right sidebar panes (73 files): a native Context/Agents/Todo/Timeline/
      Outputs rail lands via `Ctrl+Shift+B`, with live usage, delegated rows,
      the `todo.updated` task list, prompt timeline with filtering and row
      jumps, global session search, and artifact/image links plus an inline
      text/image preview. Generated HTML/SVG/code artifacts open in the
      desktop-style versioned browser with copy/save/source/browser actions.
- [x] Session import/export, archive/restore, retention/prune, delete-empty
      and bulk actions.
- [x] Session lineage and branch-aware navigation: branch/reset/compression
      markers, parent tooltips and `Open parent`.
- [x] Model picker overlay: the composer menu plus a persistent visibility
      overlay (hidden models/providers are filtered locally), per-session
      model scope (`SetConfig` with `--provider`) and ordered fallback-chain
      editing on the Model page.

### Workspace pages

- [x] Capabilities subpages (128 files): toolset + MCP toggles, MCP
      add/test/remove/OAuth, raw MCP config edit, approved catalog
      browse/install, skill enable/lock/edit/create, hub sources/remote
      search/preview/scan and official hub install/update/uninstall landed.
      Per-toolset provider/env/model configuration, plugin manifest
      `settings_schema` forms and account connector list/connect + browser
      operation polling land. Connector accounts (rename/remove), tool lists
      (cached/refresh), catalog browse and device-code/user-code prompts land.
- [x] Terminal backend picker: live probe rows
      (`GET /api/tools/terminal/backends`) and profile-scoped selection
      (`PUT /api/tools/terminal/backend`).
- [x] Computer Use diagnostics and permissions: async status, TCC grant and
      grant-action polling, with inline failures. Browser/CDP automation is
      implemented separately under platform surfaces.
- [x] Messaging onboarding: Telegram pairing (deep link, poll, owner id,
      apply, cancel) and WhatsApp QR pairing (start/poll/apply/cancel,
      self-chat allowed-user autofill, restart hint) land, with the
      credentials editor, per-platform test and gateway restart prompts.
- [~] Artifacts: browse + text editor with stale-write detection
      (`/api/fs/write-text`), mkdir/delete, upload/download
      (`/api/files/upload-stream`, `/api/fs/download`), git root and image/pdf
      preview (`/api/fs/read-data-url`) land. The right rail also detects and
      versions generated HTML/SVG/code artifacts. Still missing: project
      binding and a gateway `/api/artifacts` list.
- [x] Cron: jobs/runs/blueprints, delivery targets, create and sparse edit
      (`PUT /api/cron/jobs/{id}` with `{"updates": …}`), plus pause/resume/run/
      delete.
- [x] Bots: per-profile model assignment (`PUT /api/profiles/{name}/model`),
      description and SOUL editing, export/import.

### Platform surfaces

- [~] Agents/teams: an event-driven roster renders `subagent.*` status,
      progress stream, output tail and files from live gateway events; live
      rows steer (`subagent.steer`) and stop (`subagent.interrupt`).
      `/handoff <platform>` queues `handoff.request`, polls `handoff.state`
      and fails a timed-out pending row through `handoff.fail`. Direct spawn
      and kanban cards remain missing.
- [x] Webhooks page (`/webhooks`): enable, create, per-subscription toggle and
      delete, one-time secret surfaced.
- [x] Read-aloud TTS: assistant messages synthesize through
      `POST /api/audio/speak` on the profile worker and play through Qt
      Multimedia.
- [~] Voice: the schema-driven Voice page covers TTS/STT providers, voices,
      GPT-Live mode and client-direct settings; read-aloud, file dictation and
      gateway-owned live microphone capture work. Output-device selection
      applies immediately to read-aloud; the input-device preference is stored
      only because the gateway host owns microphone capture. Desktop
      GPT-Live/WebRTC sessions remain missing (no native WebRTC stack).
- [x] Computer Use diagnostics, TCC permission grant and grant-action
      polling.
- [x] Browser/computer automation: native CDP client with endpoint discovery,
      screenshots, real-profile consent and coordinate actions; covered by
      `browserAutomationParsesEndpointsAndConsent`,
      `browserAutomationBuildsPointerEventsAndScreenshots`,
      `cdpClientDispatchesScreenshotAndCoordinates`,
      `cdpClientDiscoversHttpAndAttachesBrowserTarget` and
      `browserAutomationDialogSurfacesControls`.
- [x] Starmap: graph stats/clusters/nodes/edges/memory cards, node detail and
      delete through `/api/learning/*`.
- [x] Quick entry: `Ctrl+Shift+Space` overlay submits a new chat through the
      primary shell.
- [~] Command Center, HUD, pets (`pet-generate`, `pet-overlay`), floating HUD
      and wake indicator. The transparent always-on-top pet overlay decodes
      the spritesheet off the UI thread, animates state rows, follows the
      snapshot and restores its saved position; `Ctrl+Shift+O` toggles it.
      The chat right rail now uses collapsible Context/Agents/Todo/Timeline/
      Outputs sections plus Project/Git/Terminal, with a themed project
      preview and PTY-backed shell; the generated HTML/SVG/code artifacts open
      in a versioned browser with copy/save/browser/source actions. Global
      session search moved to the chat header with context completion; project
      binding for generated artifacts remains.
- [~] Multi-window: a detachable chat window (`Ctrl+Shift+N`) reuses the
      active session, transcript actions and composer under one Supervisor.
      Settings/preview/terminal/artifact windows, per-window state and routing
      (`app-window.ts`, `use-window-controls-*`) remain missing.
- [~] Shell extras: painted message context menus and the `Ctrl+F` global
      session search bar land. Hooks, contribution points, titlebar clusters,
      statusbar items, tours and boot-failure/onboarding/updates overlays
      remain missing.

### Settings (115 files)

- [x] Local models: status + hardware + catalog with download/activate/delete
      and server start/stop (`/api/local-models/*`).
- [x] Voice providers and live voice editors: provider/model/voice combos,
      dependent STT/TTS visibility, editable custom voice ids, and audio
      device rows.
- [x] Delegation: backend selection, model-route add/edit/remove with ordered
      fallbacks and defaults, and profile-route description/default editing
      (`/api/delegation/backend`, `.../model_map`, `.../profile_routes`).
- [x] Memory providers config + layers: `/api/memory` status, built-in layer
      sizes, saved-provider config fields, `surface=declared` GET/PUT and
      provider reload.
- [~] Plugins: hub enable/disable/update/remove + catalog install and
      manifest `config_schema` settings forms land; plugin-owned subpages,
      deep links and billing remain missing.
- [ ] Billing plans, checkout, auto-reload, keep-going-on-credits.
- [x] Sessions: archive, retention, import/export, bulk.
- [x] Appearance: density, translucency, animations, message width,
      timestamps, code-block scale and font stack. Marketplace themes remain
      unsupported by the native renderer.
- [~] Keybinds: all 19 window actions are registered with duplicate detection
      and listed/rebindable/resettable on the Keybinds page. Desktop-only
      pane/tile actions still have no target in this shell.
- [x] Notifications: native OS service, permission prompt guidance, per-kind
      delivery policy, duplicate guard, mute and test button.
- [~] Gateway: custom endpoints editor lands; connection registry, web-app
      gateway settings and shared metrics are missing.
- [~] Subpage navigation, field-key search and `page#field` deep links land
      for the nine config sections with desktop-mirrored chips. Searchable
      combo pickers and the remaining dependent-field rules are still
      missing.

### Gateway / protocol

- [x] JSON-RPC client (`session.mutate` with generation fencing,
      `session.create` canonical `request_id` idempotency, receipts/replay,
      `open_requests` re-delivery). Delete uses `session.mutate`;
      title/sidebar/archive still use the fenced REST patch until those routes
      move.
- [~] 307 REST routes → `parity/rest_routes.md` now maps all 307: **146
      matched, 161 gaps, 3 native-only**. The remainder need per-route
      implementation or a documented desktop-only/backend-replaced reason.
- [x] Event methods: 26 published, 26 handled (`message.*`, `tool.*`,
      `approval/clarify`, `connection.request/update`, `todo.updated`,
      `status.update`, `error`, `notification.*`, `session.control.update`,
      `moa.*`, `reaction`, `message.reaction`, `review.summary`,
      `agent.print`, voice.*).

## Acceptance for “parity”

1. Every desktop route has a native route with the same entry point and
   keyboard shortcut.
2. Every backend call has the same request shape, profile scope, fencing and
   error path.
3. The native widget tree exposes the same test/aria contract where the
   desktop has one.
4. Both the desktop test and the native test run for the row.
5. Only then remove the row from this file and the inventory.

## Next milestone

Artifact project binding, memory providers, per-plugin settings and the
remaining multi-window panes. Pin-to-context parity requires a gateway
contract. GPT-Live/WebRTC voice remains deferred until a native WebRTC stack
is designed; the gateway-owned live microphone path stays the working
alternative. External-Chromium CDP automation now has a native client; the
Electron in-app preview bridge remains separate. No new “parity” claim before
the inventory table is empty.
