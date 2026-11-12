# Desktop feature inventory (generated)

Source: `~/.hermes/hermes-agent/apps/desktop` and the gateway's `hermes_cli/web_routers`. Regenerate with `python3 scripts/gen_feature_inventory.py`. Every list below is the actual file/route set on disk, so it is the parity checklist rather than a summary.
## Routes and app views

- App routes (12): session-import, new, settings, command-center, capabilities, messaging, webhooks, artifacts, cron, profiles, agents, starmap
- App views (13): session-import, agents, artifacts, capabilities, chat, command-center, cron, extension, messaging, profiles, settings, starmap, webhooks
- Session routes are `/<session-id>`; reserved app paths are excluded from the session-id parser.

## App areas (27)

- agents (1 non-test files)
- artifacts (2 non-test files)
- capabilities (99 non-test files)
- chat (215 non-test files)
- command-center (2 non-test files)
- command-palette (7 non-test files)
- context-menu (5 non-test files)
- contrib (25 non-test files)
- cron (10 non-test files)
- gateway (8 non-test files)
- hooks (8 non-test files)
- hud (10 non-test files)
- learning (1 non-test files)
- messaging (5 non-test files)
- overlays (5 non-test files)
- pet-generate (11 non-test files)
- pet-overlay (2 non-test files)
- profiles (4 non-test files)
- quick-entry (2 non-test files)
- right-sidebar (42 non-test files)
- session (82 non-test files)
- session-import (2 non-test files)
- settings (115 non-test files)
- shell (29 non-test files)
- starmap (16 non-test files)
- wake-indicator (2 non-test files)
- webhooks (1 non-test files)

## Settings files (115)

- src/app/settings/about-settings.tsx
- src/app/settings/appearance-contrib.tsx
- src/app/settings/appearance-settings.tsx
- src/app/settings/aux-task-rows.tsx
- src/app/settings/billing/account-row-value.tsx
- src/app/settings/billing/auto-reload-row.tsx
- src/app/settings/billing/current-plan-card.tsx
- src/app/settings/billing/index.tsx
- src/app/settings/billing/inline-feedback.tsx
- src/app/settings/billing/keep-going-on-credits-row.tsx
- src/app/settings/billing/plans-view.tsx
- src/app/settings/billing/tier-art.tsx
- src/app/settings/browser-real-profile-panel.tsx
- src/app/settings/chat-font-setting.tsx
- src/app/settings/combobox-input.tsx
- src/app/settings/computer-use-panel.tsx
- src/app/settings/config-field.tsx
- src/app/settings/config-settings.tsx
- src/app/settings/connections-registry.tsx
- src/app/settings/credential-key-ui.tsx
- src/app/settings/custom-endpoints-settings.tsx
- src/app/settings/delegation/model-route-dialog.tsx
- src/app/settings/delegation/model-routes.tsx
- src/app/settings/delegation/profile-routes.tsx
- src/app/settings/delegation/routing-section.tsx
- src/app/settings/delegation/subagent-model-gate.tsx
- src/app/settings/delegation-model-provider-field.tsx
- src/app/settings/delegation-route-row.tsx
- src/app/settings/env-credentials.tsx
- src/app/settings/env-var-actions-menu.tsx
- src/app/settings/fallback-models-field.tsx
- src/app/settings/gateway-settings.tsx
- src/app/settings/hud-modifier-settings.tsx
- src/app/settings/index.tsx
- src/app/settings/keybind-settings.tsx
- src/app/settings/keychain-encryption-setting.tsx
- src/app/settings/keys-settings.tsx
- src/app/settings/local-model-download-progress.tsx
- src/app/settings/local-models-browse.tsx
- src/app/settings/local-models-hardware-section.tsx
- src/app/settings/local-models-model-rows.tsx
- src/app/settings/local-models-models-section.tsx
- src/app/settings/local-models-quickstart.tsx
- src/app/settings/local-models-runtime-section.tsx
- src/app/settings/local-models-settings.tsx
- src/app/settings/managed-updates-section.tsx
- src/app/settings/memory/connect.tsx
- src/app/settings/memory/field-control.tsx
- src/app/settings/memory/memory-layers.tsx
- src/app/settings/memory/provider-config-modal.tsx
- src/app/settings/memory/provider-config-panel.tsx
- src/app/settings/minimize-to-tray-setting.tsx
- src/app/settings/model-select.tsx
- src/app/settings/model-settings.tsx
- src/app/settings/notifications-settings.tsx
- src/app/settings/onboarding-settings.tsx
- src/app/settings/pet-settings.tsx
- src/app/settings/plugin-install-details.tsx
- src/app/settings/plugin-install-modal.tsx
- src/app/settings/plugin-settings-form.tsx
- src/app/settings/plugin-settings.tsx
- src/app/settings/primitives.tsx
- src/app/settings/profile-scope.tsx
- src/app/settings/providers-settings.tsx
- src/app/settings/quick-entry-settings.tsx
- src/app/settings/screenshot-settings.tsx
- src/app/settings/searchable-select.tsx
- src/app/settings/sessions-settings.tsx
- src/app/settings/shared-metrics-settings.tsx
- src/app/settings/subpage-navigation.tsx
- src/app/settings/terminal-backend-panel.tsx
- src/app/settings/terminal-font-setting.tsx
- src/app/settings/toolset-config-panel.tsx
- src/app/settings/uninstall-section.tsx
- src/app/settings/update-channel-row.tsx
- src/app/settings/vault-settings.tsx
- src/app/settings/voice-provider-fields.tsx
- src/app/settings/web-access-settings.tsx
- src/app/settings/webapp-gateway-settings.tsx
- src/app/settings/appearance-subpages.ts
- src/app/settings/billing/api.ts
- src/app/settings/billing/billing-amounts.ts
- src/app/settings/billing/dev-fixtures.ts
- src/app/settings/billing/errors.ts
- src/app/settings/billing/open-external.ts
- src/app/settings/billing/simulated-api.ts
- src/app/settings/billing/types.ts
- src/app/settings/billing/use-billing-state.ts
- src/app/settings/billing/use-charge-poller.ts
- src/app/settings/billing/use-step-up.ts
- src/app/settings/billing/use-subscription-change.ts
- src/app/settings/cloud-team-change.ts
- src/app/settings/config-subpages.ts
- src/app/settings/constants.ts
- src/app/settings/delegation/plugin-actions.ts
- src/app/settings/delegation/routes.ts
- src/app/settings/delegation/routing-data.ts
- src/app/settings/delegation-route-draft.ts
- src/app/settings/field-copy.ts
- src/app/settings/helpers.ts
- src/app/settings/local-models-actions.ts
- src/app/settings/local-models-owner.ts
- src/app/settings/moved-tabs.ts
- src/app/settings/other-subpages.ts
- src/app/settings/plugin-install-run.ts
- src/app/settings/settings-manifest.ts
- src/app/settings/settings-search.ts
- src/app/settings/ssh-host-selection.ts
- src/app/settings/stale-aux-dismissal.ts
- src/app/settings/subpages.ts
- src/app/settings/types.ts
- src/app/settings/use-deep-link-highlight.ts
- src/app/settings/use-plugin-probe.ts
- src/app/settings/use-setting-deep-link.ts
- src/app/settings/use-settings-search.ts

## Chat modules

- composer/ (92)
- hooks/ (3)
- right-rail/ (28)
- sidebar/ (52)

## right-sidebar (42 non-test files)

- src/app/right-sidebar/file-actions.tsx
- src/app/right-sidebar/files/remote-picker.tsx
- src/app/right-sidebar/files/tree.tsx
- src/app/right-sidebar/index.tsx
- src/app/right-sidebar/review/churn-bar.tsx
- src/app/right-sidebar/review/file-tree.tsx
- src/app/right-sidebar/review/index.tsx
- src/app/right-sidebar/review/scope-row.tsx
- src/app/right-sidebar/review/ship-bar.tsx
- src/app/right-sidebar/terminal/chrome.tsx
- src/app/right-sidebar/terminal/instance.tsx
- src/app/right-sidebar/terminal/persistent.tsx
- src/app/right-sidebar/terminal/rail-new-terminal.tsx
- src/app/right-sidebar/terminal/rail.tsx
- src/app/right-sidebar/terminal/workspace.tsx
- src/app/right-sidebar/files/dnd-manager.ts
- src/app/right-sidebar/files/ipc.ts
- src/app/right-sidebar/files/prefs.ts
- src/app/right-sidebar/files/use-project-tree.ts
- src/app/right-sidebar/review/tree-data.ts
- src/app/right-sidebar/store.ts
- src/app/right-sidebar/terminal/active-resize.ts
- src/app/right-sidebar/terminal/agent-terminal-stream.ts
- src/app/right-sidebar/terminal/buffer.ts
- src/app/right-sidebar/terminal/clipboard.ts
- src/app/right-sidebar/terminal/cwd-tracking.ts
- src/app/right-sidebar/terminal/links.ts
- src/app/right-sidebar/terminal/reveal-focus.ts
- src/app/right-sidebar/terminal/revive-history.ts
- src/app/right-sidebar/terminal/revive-snapshot.ts
- src/app/right-sidebar/terminal/selection.ts
- src/app/right-sidebar/terminal/snapshot-persister.ts
- src/app/right-sidebar/terminal/terminal-context-menu.ts
- src/app/right-sidebar/terminal/terminal-drop.ts
- src/app/right-sidebar/terminal/terminal-font.ts
- src/app/right-sidebar/terminal/terminal-gateway.ts
- src/app/right-sidebar/terminal/terminal-input-bindings.ts
- src/app/right-sidebar/terminal/terminal-session-attempt.ts
- src/app/right-sidebar/terminal/terminals.ts
- src/app/right-sidebar/terminal/use-agent-terminal.ts
- src/app/right-sidebar/terminal/use-terminal-font.ts
- src/app/right-sidebar/terminal/use-terminal-session.ts

## overlays (5 non-test files)

- src/app/overlays/overlay-breadcrumb-header.tsx
- src/app/overlays/overlay-chrome.tsx
- src/app/overlays/overlay-split-layout.tsx
- src/app/overlays/overlay-view.tsx
- src/app/overlays/panel.tsx

## command-palette (7 non-test files)

- src/app/command-palette/highlight-watcher.tsx
- src/app/command-palette/index.tsx
- src/app/command-palette/marketplace-theme-page.tsx
- src/app/command-palette/pet-palette-page.tsx
- src/app/command-palette/status-row.tsx
- src/app/command-palette/contrib.ts
- src/app/command-palette/use-update-row-label.ts

## shell (29 non-test files)

- src/app/shell/approval-mode-menu.tsx
- src/app/shell/butterbar.tsx
- src/app/shell/context-usage-panel.tsx
- src/app/shell/gateway-menu-panel.tsx
- src/app/shell/hooks/use-statusbar-items.tsx
- src/app/shell/local-setup-menu-row.tsx
- src/app/shell/model-catalog-menu.tsx
- src/app/shell/model-edit-submenu.tsx
- src/app/shell/model-menu-panel.tsx
- src/app/shell/model-menu-row-decorations.tsx
- src/app/shell/reasoning-menu-panel.tsx
- src/app/shell/sidebar-label.tsx
- src/app/shell/statusbar-controls.tsx
- src/app/shell/system-resources-statusbar.tsx
- src/app/shell/terms-butterbar.tsx
- src/app/shell/titlebar-controls.tsx
- src/app/shell/titlebar-icon.tsx
- src/app/shell/wslg-window-controls.tsx
- src/app/shell/group-setter.ts
- src/app/shell/hooks/use-change-workspace.ts
- src/app/shell/hooks/use-compact-conversation.ts
- src/app/shell/hooks/use-context-breakdown.ts
- src/app/shell/hooks/use-focused-session-row.ts
- src/app/shell/hooks/use-overlay-routing.ts
- src/app/shell/hooks/use-status-snapshot.ts
- src/app/shell/hooks/use-window-controls-overlay-width.ts
- src/app/shell/titlebar.ts
- src/app/shell/use-model-menu-controller.ts
- src/app/shell/use-touch-titlebar.ts

## hooks (8 non-test files)

- src/app/hooks/use-config-record.ts
- src/app/hooks/use-debounced.ts
- src/app/hooks/use-keybinds.ts
- src/app/hooks/use-on-profile-switch.ts
- src/app/hooks/use-profile-switch-latch.ts
- src/app/hooks/use-refresh-hotkey.ts
- src/app/hooks/use-route-enum-param.ts
- src/app/hooks/use-route-overlay-active.ts

## contrib (25 non-test files)

- src/app/contrib/context.tsx
- src/app/contrib/controller.tsx
- src/app/contrib/mcp-install-deeplink-dialog.tsx
- src/app/contrib/panes.tsx
- src/app/contrib/plugin-preview-consent-dialog.tsx
- src/app/contrib/surfaces.tsx
- src/app/contrib/wiring.tsx
- src/app/contrib/workspace-page-header.tsx
- src/app/contrib/dev/credits-notice-demo.ts
- src/app/contrib/hooks/desktop-onboarding-metrics.ts
- src/app/contrib/hooks/remembered-session.ts
- src/app/contrib/hooks/use-background-sync.ts
- src/app/contrib/hooks/use-desktop-integrations.ts
- src/app/contrib/hooks/use-desktop-metrics.ts
- src/app/contrib/hooks/use-live-work-roster.ts
- src/app/contrib/hooks/use-pet-bridge.ts
- src/app/contrib/hooks/use-quick-entry-bridge.ts
- src/app/contrib/hooks/use-session-tile-delegate.ts
- src/app/contrib/index.ts
- src/app/contrib/layout-presets.ts
- src/app/contrib/layout-sides.ts
- src/app/contrib/session-rpc-dispatcher.ts
- src/app/contrib/types.ts
- src/app/contrib/wiring-effects.ts
- src/app/contrib/wiring-routing.ts

## Capabilities subpages

- catalog/ (24)
- connectors/ (45)
- mcp/ (8)
- plugins/ (7)
- skills/ (6)
- toolsets/ (4)
- ui/ (2)

## Keybind actions (24)

- composer.cancel
- composer.dictate
- composer.focus
- composer.help
- composer.history
- composer.mention
- composer.newline
- composer.queue
- composer.send
- composer.slash
- composer.steer
- composer.voice
- nav.agents
- nav.artifacts
- nav.capabilities
- nav.cron
- nav.messaging
- nav.profiles
- nav.settings
- profile.create
- profile.default
- profile.next
- profile.prev
- session.archive

## Gateway REST routes (307)

```
DELETE /api/credentials/pool/{provider}/{index}
DELETE /api/cron/jobs/{job_id}
DELETE /api/dashboard/agent-plugins/{name:path}
DELETE /api/env
DELETE /api/files
DELETE /api/learning/node
DELETE /api/local-models/models/{model_id}
DELETE /api/mcp/oauth/flows/{flow_id}
DELETE /api/mcp/servers/{name}
DELETE /api/messaging/relay/{platform}/accounts/{platform_user_id}
DELETE /api/messaging/telegram/onboarding/{pairing_id}
DELETE /api/messaging/whatsapp/onboarding/{pairing_id}
DELETE /api/ops/hooks
DELETE /api/profiles/{name}
DELETE /api/providers/custom-endpoints/{endpoint_id}
DELETE /api/providers/oauth/sessions/{session_id}
DELETE /api/providers/oauth/{provider_id}
DELETE /api/sessions/empty
DELETE /api/sessions/{session_id}
DELETE /api/webhooks/{name}
GET    /api/actions/{name}/status
GET    /api/analytics/models
GET    /api/analytics/usage
GET    /api/audio/elevenlabs/voices
GET    /api/audio/voice-config
GET    /api/audio/voice-live/status
GET    /api/chat/workspaces
GET    /api/config
GET    /api/config/defaults
GET    /api/config/raw
GET    /api/config/schema
GET    /api/credentials/pool
GET    /api/cron/blueprints
GET    /api/cron/delivery-targets
GET    /api/cron/jobs
GET    /api/cron/jobs/{job_id}
GET    /api/cron/jobs/{job_id}/runs
GET    /api/curator
GET    /api/dashboard/font
GET    /api/dashboard/plugins
GET    /api/dashboard/plugins/auto-update
GET    /api/dashboard/plugins/catalog
GET    /api/dashboard/plugins/hub
GET    /api/dashboard/plugins/rescan
GET    /api/dashboard/themes
GET    /api/delegation/routing
GET    /api/desktop-plugins/list
GET    /api/desktop-plugins/read
GET    /api/desktop-plugins/root
GET    /api/egress/status
GET    /api/env
GET    /api/files
GET    /api/files/download
GET    /api/files/read
GET    /api/files/stream
GET    /api/fs/default-cwd
GET    /api/fs/download
GET    /api/fs/git-root
GET    /api/fs/list
GET    /api/fs/read-data-url
GET    /api/fs/read-text
GET    /api/gateway/migrate/plan
GET    /api/git/base-branches
GET    /api/git/branches
GET    /api/git/file-diff
GET    /api/git/gh-auth
GET    /api/git/review/commit-context
GET    /api/git/review/diff
GET    /api/git/review/list
GET    /api/git/review/rev-parse
GET    /api/git/review/ship-info
GET    /api/git/status
GET    /api/git/worktrees
GET    /api/health
GET    /api/health/idle
GET    /api/hermes/update/check
GET    /api/hermes/update/receipt
GET    /api/host/identity
GET    /api/learning/graph
GET    /api/learning/node
GET    /api/local-models/catalog
GET    /api/local-models/hardware
GET    /api/local-models/jobs
GET    /api/local-models/jobs/{job_id}
GET    /api/local-models/search
GET    /api/local-models/search/files
GET    /api/local-models/status
GET    /api/logs
GET    /api/mcp/catalog
GET    /api/mcp/oauth/callback/{server_name:path}
GET    /api/mcp/oauth/flows/{flow_id}
GET    /api/mcp/servers
GET    /api/media
GET    /api/media/proxy
GET    /api/memory
GET    /api/memory/providers/{name}/config
GET    /api/messaging/platforms
GET    /api/messaging/relay
GET    /api/messaging/relay/link/{code}
GET    /api/messaging/telegram/onboarding/{pairing_id}
GET    /api/messaging/whatsapp/onboarding/{pairing_id}
GET    /api/model/auxiliary
GET    /api/model/info
GET    /api/model/moa
GET    /api/model/options
GET    /api/model/recommended-default
GET    /api/ops/backup/download
GET    /api/ops/checkpoints
GET    /api/ops/hooks
GET    /api/pairing
GET    /api/portal
GET    /api/profiles
GET    /api/profiles/active
GET    /api/profiles/projects/tree
GET    /api/profiles/sessions
GET    /api/profiles/sessions/sidebar
GET    /api/profiles/{name}/desktop-overlay
GET    /api/profiles/{name}/setup-command
GET    /api/profiles/{name}/soul
GET    /api/providers/custom-endpoints
GET    /api/providers/oauth
GET    /api/providers/oauth/nous/handoff
GET    /api/providers/oauth/{provider_id}/poll/{session_id}
GET    /api/sessions
GET    /api/sessions/empty/count
GET    /api/sessions/search
GET    /api/sessions/stats
GET    /api/sessions/{session_id}
GET    /api/sessions/{session_id}/export
GET    /api/sessions/{session_id}/latest-descendant
GET    /api/sessions/{session_id}/messages
GET    /api/sessions/{session_id}/messages/around
GET    /api/sessions/{session_id}/mutation-snapshot
GET    /api/sessions/{session_id}/timeline
GET    /api/shared-metrics/consent
GET    /api/skills
GET    /api/skills/content
GET    /api/skills/hub/official
GET    /api/skills/hub/preview
GET    /api/skills/hub/scan
GET    /api/skills/hub/search
GET    /api/skills/hub/sources
GET    /api/ssh/ownership
GET    /api/status
GET    /api/system/stats
GET    /api/tools/computer-use/status
GET    /api/tools/terminal/backends
GET    /api/tools/toolsets
GET    /api/tools/toolsets/{name}/config
GET    /api/tools/toolsets/{name}/models
GET    /api/ui-preferences
GET    /api/webapp-access/status
GET    /api/webhooks
GET    /dashboard-plugins/@{profile}/{plugin_name}/{file_path:path}
GET    /dashboard-plugins/{plugin_name}/{file_path:path}
GET    /webapp/launch
GET    /webapp/window
PATCH  /api/profiles/{name}
PATCH  /api/sessions/{session_id}
PATCH  /api/ui-preferences
POST   /api/audio/speak
POST   /api/audio/stt-lease
POST   /api/audio/transcribe
POST   /api/audio/tts-lease
POST   /api/audio/voice-live/session
POST   /api/chat/file-upload
POST   /api/chat/image-upload
POST   /api/credentials/pool
POST   /api/cron/blueprints/instantiate
POST   /api/cron/fire
POST   /api/cron/jobs
POST   /api/cron/jobs/{job_id}/cancel
POST   /api/cron/jobs/{job_id}/pause
POST   /api/cron/jobs/{job_id}/resume
POST   /api/cron/jobs/{job_id}/trigger
POST   /api/curator/run
POST   /api/dashboard/agent-plugins/activate
POST   /api/dashboard/agent-plugins/install
POST   /api/dashboard/agent-plugins/update-all
POST   /api/dashboard/agent-plugins/{name:path}/disable
POST   /api/dashboard/agent-plugins/{name:path}/enable
POST   /api/dashboard/agent-plugins/{name:path}/update
POST   /api/dashboard/plugins/{name:path}/visibility
POST   /api/desktop-plugins/reconcile
POST   /api/env/reveal
POST   /api/files/mkdir
POST   /api/files/ticket
POST   /api/files/upload
POST   /api/files/upload-stream
POST   /api/fs/write-text
POST   /api/gateway/drain
POST   /api/gateway/migrate
POST   /api/gateway/restart
POST   /api/gateway/start
POST   /api/gateway/stop
POST   /api/git/branch/switch
POST   /api/git/review/commit
POST   /api/git/review/create-pr
POST   /api/git/review/pr-list
POST   /api/git/review/push
POST   /api/git/review/revert
POST   /api/git/review/stage
POST   /api/git/review/unstage
POST   /api/git/worktree/add
POST   /api/git/worktree/remove
POST   /api/health/retirement
POST   /api/hermes/update
POST   /api/local-models/activate
POST   /api/local-models/download
POST   /api/local-models/download-browsed
POST   /api/local-models/download/pause
POST   /api/local-models/download/resume
POST   /api/local-models/eject
POST   /api/local-models/quickstart
POST   /api/local-models/runtime/install
POST   /api/local-models/server
POST   /api/local-models/sideload
POST   /api/mcp/catalog/install
POST   /api/mcp/servers
POST   /api/mcp/servers/{name}/auth
POST   /api/mcp/servers/{name}/test
POST   /api/memory/providers/{name}/setup
POST   /api/memory/reset
POST   /api/messaging/platforms/{platform_id}/test
POST   /api/messaging/relay/{platform}/link
POST   /api/messaging/telegram/onboarding/start
POST   /api/messaging/telegram/onboarding/{pairing_id}/apply
POST   /api/messaging/whatsapp/onboarding/start
POST   /api/messaging/whatsapp/onboarding/{pairing_id}/apply
POST   /api/model/set
POST   /api/ops/backup
POST   /api/ops/checkpoints/prune
POST   /api/ops/config-migrate
POST   /api/ops/debug-share
POST   /api/ops/doctor
POST   /api/ops/dump
POST   /api/ops/hooks
POST   /api/ops/import
POST   /api/ops/import-upload
POST   /api/ops/prompt-size
POST   /api/ops/security-audit
POST   /api/pairing/approve
POST   /api/pairing/clear-pending
POST   /api/pairing/revoke
POST   /api/profiles
POST   /api/profiles/active
POST   /api/profiles/import
POST   /api/profiles/sessions/pull-requests
POST   /api/profiles/{name}/describe-auto
POST   /api/profiles/{name}/export
POST   /api/profiles/{name}/open-terminal
POST   /api/providers/custom-endpoints
POST   /api/providers/custom-endpoints/validate
POST   /api/providers/custom-endpoints/{endpoint_id}/activate
POST   /api/providers/oauth/{provider_id}/start
POST   /api/providers/oauth/{provider_id}/submit
POST   /api/providers/validate
POST   /api/sessions/bulk-delete
POST   /api/sessions/import
POST   /api/sessions/owner-backfill
POST   /api/sessions/prune
POST   /api/skills
POST   /api/skills/hub/install
POST   /api/skills/hub/uninstall
POST   /api/skills/hub/update
POST   /api/tools/computer-use/permissions/grant
POST   /api/tools/toolsets/{name}/post-setup
POST   /api/webapp-access/restart
POST   /api/webapp-access/start
POST   /api/webapp-access/stop
POST   /api/webapp/socket-ticket
POST   /api/webapp/window-ticket
POST   /api/webhooks
POST   /api/webhooks/enable
POST   /webapp/window-session
PUT    /api/config
PUT    /api/config/raw
PUT    /api/cron/jobs/{job_id}
PUT    /api/curator/paused
PUT    /api/dashboard/font
PUT    /api/dashboard/plugin-providers
PUT    /api/dashboard/plugins/auto-update
PUT    /api/dashboard/theme
PUT    /api/delegation/backend
PUT    /api/delegation/backends/model_map
PUT    /api/delegation/backends/profile_routes
PUT    /api/env
PUT    /api/learning/node
PUT    /api/mcp/servers
PUT    /api/mcp/servers/{name}/enabled
PUT    /api/memory/provider
PUT    /api/memory/providers/{name}/config
PUT    /api/messaging/platforms/{platform_id}
PUT    /api/model/moa
PUT    /api/profiles/{name}/description
PUT    /api/profiles/{name}/model
PUT    /api/profiles/{name}/soul
PUT    /api/shared-metrics/consent
PUT    /api/skills/content
PUT    /api/skills/lock
PUT    /api/skills/toggle
PUT    /api/tools/terminal/backend
PUT    /api/tools/toolsets/{name}
PUT    /api/tools/toolsets/{name}/env
PUT    /api/tools/toolsets/{name}/model
PUT    /api/tools/toolsets/{name}/provider
PUT    /api/webhooks/{name}/enabled
```

## Gateway event methods (26)

- agent.print
- approval.request
- clarify
- clarify.request
- connection.request
- connection.update
- message.delta
- message.interim
- notification.clear
- notification.show
- reaction
- reasoning.available
- reasoning.delta
- review.summary
- secret
- session.control.update
- sudo
- thinking.delta
- todo.updated
- tool.complete
- tool.generating
- tool.progress
- tool.start
- vault.code
- vault.save_login
- vault.unlock_prompt

## Native source (measured)

| file | lines |
|---|---:|
| chat_view.cc | 1086 |
| chat_view.h | 50 |
| click_target.h | 25 |
| command_palette.cc | 317 |
| command_palette.h | 67 |
| composer.cc | 747 |
| composer.h | 115 |
| gateway_endpoint.cc | 194 |
| gateway_endpoint.h | 50 |
| gateway_http.cc | 283 |
| gateway_http.h | 83 |
| gateway_socket.cc | 333 |
| gateway_socket.h | 97 |
| main.cc | 551 |
| main_window.cc | 1590 |
| main_window.h | 162 |
| markdown.cc | 156 |
| markdown.h | 22 |
| model.h | 195 |
| native_prefs.cc | 41 |
| native_prefs.h | 21 |
| pages_view.cc | 221 |
| pages_view.h | 79 |
| paint_utils.cc | 73 |
| paint_utils.h | 37 |
| preferences_dialog.cc | 190 |
| preferences_dialog.h | 52 |
| profile_worker.cc | 2312 |
| profile_worker.h | 147 |
| request_bar.cc | 500 |
| request_bar.h | 75 |
| settings_pages.cc | 2480 |
| settings_pages.h | 65 |
| settings_view.cc | 662 |
| settings_view.h | 112 |
| sidebar.cc | 752 |
| sidebar.h | 124 |
| slash_commands.cc | 110 |
| slash_commands.h | 35 |
| status_bar.cc | 101 |
| status_bar.h | 49 |
| supervisor.cc | 553 |
| supervisor.h | 107 |
| theme.cc | 134 |
| theme.h | 78 |
| theme_presets.cc | 207 |
| theme_presets.h | 31 |
| title_bar.cc | 258 |
| title_bar.h | 60 |
| widget_theme.cc | 154 |
| widget_theme.h | 23 |
