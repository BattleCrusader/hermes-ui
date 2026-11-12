// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SETTINGS_PAGES_H_
#define HERMES_UI_SETTINGS_PAGES_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>

#include "src/model.h"
#include "src/theme.h"

class QWidget;

namespace hermes::ui {

// Everything a data page needs from the Settings route. Pages never touch the
// network themselves: they render PageState from the snapshot and ask the
// worker to refresh or mutate through these callbacks.
// Result of an async page request (skill-hub search/preview/scan). Empty
// `error` means success; callbacks always run on the UI thread.
using PageJsonCallback = std::function<void(const QJsonObject &,
                                            const QString &)>; // NOLINT

struct PageContext {
  Theme theme;
  const ProfileSnapshot *snapshot = nullptr;
  // Workspace pages already have a title/refresh bar (PagesView); suppressing
  // the per-page heading avoids painting the title twice.
  bool suppress_page_heading = false;
  std::function<void(const QString &page)> refresh;
  std::function<void(const QString &page, const QString &method,
                     const QString &path, const QJsonObject &body)>
      action;
  // Workspace pages: artifacts navigation/preview and profile lifecycle.
  std::function<void(const QString &path)> open_dir;
  std::function<void(const QString &path)> preview_file;
  // Spot-editor save (stale-write guarded in the worker).
  std::function<void(const QString &path, const QString &text)> save_file;
  // Artifact transfer. The local file pickers are injectable so the native
  // tests can drive the same callbacks without a modal dialog.
  std::function<void(const QString &destination, const QString &local_path,
                     PageJsonCallback done)>
      upload_file;
  std::function<void(const QString &remote_path, const QString &local_path,
                     PageJsonCallback done)>
      download_file;
  std::function<QString(const QString &title, const QString &start_dir)>
      pick_open_file;
  std::function<QString(const QString &title, const QString &suggested_name)>
      pick_save_file;
  // Capabilities: a live MCP probe (connect, list tools, disconnect) whose
  // result is shown inline on the server row.
  std::function<void(const QString &name, PageJsonCallback done)>
      test_mcp_server;
  // Catalog browse is a plain read; install is a normal page action with an
  // optional per-entry env map.
  std::function<void(PageJsonCallback done)> list_mcp_catalog;
  // The raw mcp_servers map from /api/config. Editing it is a whole-map
  // replace through PUT /api/mcp/servers, matching the desktop mcp.json editor.
  std::function<void(PageJsonCallback done)> get_mcp_config;
  std::function<void(const QJsonObject &servers, PageJsonCallback done)>
      replace_mcp_config;
  // Star map: one node's underlying SKILL.md or memory chunk for the detail
  // dialog. The read is network-backed, so it runs on the worker pool.
  std::function<void(const QString &id, PageJsonCallback done)>
      get_learning_node;
  // Computer Use readiness probes cua-driver and can take seconds, so the
  // status/grant/action polls all run on the profile worker.
  std::function<void(PageJsonCallback done)> get_computer_use_status;
  std::function<void(PageJsonCallback done)> grant_computer_use_permissions;
  std::function<void(const QString &name, PageJsonCallback done)>
      get_action_status;
  std::function<void(PageJsonCallback done)> list_terminal_backends;
  // Hosted connectors are gateway RPC (`connectors.*`), account-owned. The
  // catalog row list carries connection state; connect returns an operation
  // whose settled status is polled until the browser leg comes back.
  std::function<void(PageJsonCallback done)> list_connectors;
  std::function<void(const QString &connector, bool reconnect,
                     PageJsonCallback done)>
      connect_connector;
  std::function<void(const QString &op_id, PageJsonCallback done)>
      connector_operation_status;
  // Connector-owned subpages (accounts/tools/policy/catalog) are generic
  // gateway RPCs; the shell adds the active profile to every request.
  std::function<void(const QString &method, const QJsonObject &params,
                     PageJsonCallback done)>
      connector_rpc;
  std::function<void(const QString &backend, PageJsonCallback done)>
      select_terminal_backend;
  // Per-toolset configuration matrix: providers, readiness and env keys.
  std::function<void(const QString &name, PageJsonCallback done)>
      get_toolset_config;
  std::function<void(const QString &name, const QString &provider,
                     const QJsonObject &env, PageJsonCallback done)>
      save_toolset_config;
  // Per-toolset model catalog can hit a provider API; it stays off the UI.
  std::function<void(const QString &name, const QString &provider,
                     PageJsonCallback done)>
      get_toolset_models;
  std::function<void(const QString &name, const QString &model,
                     const QString &provider, PageJsonCallback done)>
      save_toolset_model;
  // Live delegated children accept steering text and a hard interrupt.
  std::function<void(const QString &session_id, const QString &subagent_id,
                     const QString &text, PageJsonCallback done)>
      steer_subagent;
  std::function<void(const QString &session_id, const QString &subagent_id,
                     PageJsonCallback done)>
      interrupt_subagent;
  // Agent plugin manifest settings are gateway RPC (`plugins.manage`), not a
  // REST route. Secrets are written separately through PUT /api/env.
  std::function<void(const QString &key, const QJsonObject &values,
                     PageJsonCallback done)>
      save_plugin_settings;
  // MCP OAuth is a dashboard-mediated browser flow: start returns the flow id
  // and authorization URL, poll reports its gateway-side status, and cancel
  // settles an abandoned flow instead of letting it squat until timeout.
  std::function<void(const QString &name, PageJsonCallback done)>
      start_mcp_oauth;
  std::function<void(const QString &flow_id, PageJsonCallback done)>
      poll_mcp_oauth;
  std::function<void(const QString &flow_id, PageJsonCallback done)>
      cancel_mcp_oauth;
  // Injectable for native tests; production opens the system browser.
  std::function<void(const QString &url)> open_url;
  // Messaging connection probe; result is surfaced inline on the platform row.
  std::function<void(const QString &platform_id, PageJsonCallback done)>
      test_platform;
  // Telegram pairing is dashboard-mediated: start returns a deep link and
  // pairing id, poll reports waiting/ready, apply writes the bot token and
  // allowlist, cancel releases the gateway-side session.
  std::function<void(const QString &bot_name, PageJsonCallback done)>
      start_telegram_onboarding;
  std::function<void(const QString &pairing_id, PageJsonCallback done)>
      poll_telegram_onboarding;
  std::function<void(const QString &pairing_id,
                     const QStringList &allowed_user_ids,
                     PageJsonCallback done)>
      apply_telegram_onboarding;
  std::function<void(const QString &pairing_id, PageJsonCallback done)>
      cancel_telegram_onboarding;
  // WhatsApp QR pairing. `allowed_users` is the gateway's comma/space string.
  std::function<void(const QString &mode, const QString &allowed_users,
                     bool reset_session, PageJsonCallback done)>
      start_whatsapp_onboarding;
  std::function<void(const QString &pairing_id, PageJsonCallback done)>
      poll_whatsapp_onboarding;
  std::function<void(const QString &pairing_id, const QString &mode,
                     const QString &allowed_users, PageJsonCallback done)>
      apply_whatsapp_onboarding;
  std::function<void(const QString &pairing_id, PageJsonCallback done)>
      cancel_whatsapp_onboarding;
  // Raw SKILL.md for the dashboard editor; read on the worker pool.
  std::function<void(const QString &name, PageJsonCallback done)>
      get_skill_content;
  // Official skill-hub catalog (local scan on the gateway). Search across the
  // remote hub sources is a separate, network-bound route.
  std::function<void(PageJsonCallback done)> list_official_skills;
  // Skill hub: sources, remote search, preview and install-time scan. These
  // are async because search fans out over the network.
  std::function<void(PageJsonCallback done)> list_skill_hub_sources;
  std::function<void(const QString &query, const QString &source, int limit,
                     PageJsonCallback done)>
      search_skill_hub;
  std::function<void(const QString &identifier, PageJsonCallback done)>
      preview_skill_hub;
  std::function<void(const QString &identifier, PageJsonCallback done)>
      scan_skill_hub;
  // Session transfer through the gateway's streaming export and fenced import.
  std::function<void(const QString &session_id, const QString &local_path,
                     PageJsonCallback done)>
      export_session;
  std::function<void(const QString &local_path, PageJsonCallback done)>
      import_sessions;
  std::function<void(const QString &name, PageJsonCallback done)> add_profile;
  std::function<void(const QString &name, PageJsonCallback done)>
      remove_profile;
  std::function<void(const QString &name)> activate_profile;
  // Bot roster: SOUL.md read/write and profile archive export/import. These
  // are profile-root operations, not scoped config writes, so they go through
  // the supervisor's install-wide client instead of PageAction.
  std::function<void(const QString &name, PageJsonCallback done)>
      get_profile_soul;
  std::function<void(const QString &name, const QString &content,
                     PageJsonCallback done)>
      set_profile_soul;
  std::function<void(const QString &name, PageJsonCallback done)>
      export_profile;
  std::function<void(const QString &archive, PageJsonCallback done)>
      import_profile;
  std::function<void(const QString &session_id)> open_session;
  std::function<void(ThemeMode mode)> set_theme;
  // Pre-made colour schemes (resources/themes/*.json). An empty id restores
  // the built-in Hermes light/dark tokens.
  std::function<void(const QString &preset_id)> set_theme_preset;
  QString theme_preset;
  // Native client preferences (keybinds, font scale, notification toggles).
  // These belong to the window, not the gateway config, so they live in
  // QSettings; the desktop keeps them in renderer local storage.
  std::function<QString(const QString &key)> get_pref;
  std::function<void(const QString &key, const QString &value)> set_pref;
  // Native OS notification delivery. The page's test button exercises the
  // exact service path the shell uses for completions and blocking prompts.
  std::function<bool(const QString &title, const QString &body, bool silent,
                     QString *error)>
      send_notification;
};

// True for pages whose content comes from gateway REST routes rather than
// from config.yaml sections.
bool IsDataPage(const QString &id);

// True for pages built natively from local preferences (no gateway route).
bool IsLocalPage(const QString &id);

// Builds the page body for `id`; returns nullptr for an unknown id. The caller
// owns the returned widget and adds it to its form layout.
QWidget *BuildDataPage(const QString &id, const PageContext &context,
                       QWidget *parent);

} // namespace hermes::ui

#endif // HERMES_UI_SETTINGS_PAGES_H_
