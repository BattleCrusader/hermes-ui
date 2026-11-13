// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PAGES_VIEW_H_
#define HERMES_UI_PAGES_VIEW_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>
#include <functional>

#include "src/model.h"
#include "src/settings_pages.h"
#include "src/theme.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace hermes::ui {

class KanbanView;

// Full-page workspace surfaces the desktop mounts inside the workspace pane:
// Capabilities, Messaging, Artifacts and the Bots (profiles) roster. They share
// the Settings data-page renderers but are not Settings: the sidebar nav owns
// them and the chat stays beneath, exactly like the desktop's workspace pages.
class PagesView : public QWidget {
  Q_OBJECT

 public:
  explicit PagesView(Theme theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetSnapshot(const ProfileSnapshot& snapshot);
  void OpenPage(const QString& id);
  bool IsOpen() const;
  QString CurrentPage() const { return page_; }
  KanbanView* Kanban() const { return kanban_; }

  std::function<void()> on_close;
  std::function<void(const QString& page)> on_page_request;
  std::function<void(const QString& page, const QString& method,
                     const QString& path, const QJsonObject& body)>
      on_page_action;
  std::function<void(const QString& path)> on_open_dir;
  std::function<void(const QString& path)> on_preview_file;
  std::function<void(const QString& path, const QString& text)> on_save_file;
  std::function<void(const QString& destination, const QString& local_path,
                     PageJsonCallback done)>
      on_upload_file;
  std::function<void(const QString& remote_path, const QString& local_path,
                     PageJsonCallback done)>
      on_download_file;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_test_mcp_server;
  std::function<void(PageJsonCallback done)> on_list_mcp_catalog;
  std::function<void(PageJsonCallback done)> on_get_mcp_config;
  std::function<void(const QJsonObject& servers, PageJsonCallback done)>
      on_replace_mcp_config;
  std::function<void(const QString& id, PageJsonCallback done)>
      on_get_learning_node;
  std::function<void(PageJsonCallback)> on_get_computer_use_status;
  std::function<void(PageJsonCallback)> on_grant_computer_use_permissions;
  std::function<void(const QString&, PageJsonCallback)> on_get_action_status;
  std::function<void(PageJsonCallback)> on_list_terminal_backends;
  std::function<void(PageJsonCallback)> on_list_connectors;
  std::function<void(const QString&, bool, PageJsonCallback)>
      on_connect_connector;
  std::function<void(const QString&, PageJsonCallback)>
      on_connector_operation_status;
  std::function<void(const QString&, const QJsonObject&, PageJsonCallback)>
      on_connector_rpc;
  std::function<void(const QString&, PageJsonCallback)>
      on_select_terminal_backend;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_start_mcp_oauth;
  std::function<void(const QString& flow_id, PageJsonCallback done)>
      on_poll_mcp_oauth;
  std::function<void(const QString& flow_id, PageJsonCallback done)>
      on_cancel_mcp_oauth;
  std::function<void(const QString& url)> on_open_url;
  // Messaging connection test. The gateway answers with {ok, state, message,
  // auth_posture}; the page surfaces the message and leaves setup untouched.
  std::function<void(const QString& platform_id, PageJsonCallback done)>
      on_test_platform;
  std::function<void(const QString& bot_name, PageJsonCallback done)>
      on_start_telegram_onboarding;
  std::function<void(const QString& pairing_id, PageJsonCallback done)>
      on_poll_telegram_onboarding;
  std::function<void(const QString& pairing_id,
                     const QStringList& allowed_user_ids,
                     PageJsonCallback done)>
      on_apply_telegram_onboarding;
  std::function<void(const QString& pairing_id, PageJsonCallback done)>
      on_cancel_telegram_onboarding;
  // WhatsApp links by QR: start returns a pairing id and (eventually) a QR
  // payload; poll reports waiting/connected, apply writes mode + allowlist.
  std::function<void(const QString& mode, const QString& allowed_users,
                     bool reset_session, PageJsonCallback done)>
      on_start_whatsapp_onboarding;
  std::function<void(const QString& pairing_id, PageJsonCallback done)>
      on_poll_whatsapp_onboarding;
  std::function<void(const QString& pairing_id, const QString& mode,
                     const QString& allowed_users, PageJsonCallback done)>
      on_apply_whatsapp_onboarding;
  std::function<void(const QString& pairing_id, PageJsonCallback done)>
      on_cancel_whatsapp_onboarding;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_get_skill_content;
  std::function<void(PageJsonCallback done)> on_list_official_skills;
  std::function<void(PageJsonCallback)> on_list_skill_hub_sources;
  std::function<void(const QString&, const QString&, int, PageJsonCallback)>
      on_search_skill_hub;
  std::function<void(const QString&, PageJsonCallback)> on_preview_skill_hub;
  std::function<void(const QString&, PageJsonCallback)> on_scan_skill_hub;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_get_toolset_config;
  std::function<void(const QString& name, const QString& provider,
                     const QJsonObject& env, PageJsonCallback done)>
      on_save_toolset_config;
  std::function<void(const QString&, const QString&, PageJsonCallback)>
      on_get_toolset_models;
  std::function<void(const QString&, const QString&, const QString&,
                     PageJsonCallback)>
      on_save_toolset_model;
  std::function<void(const QString&, const QString&, const QString&,
                     PageJsonCallback)>
      on_steer_subagent;
  std::function<void(const QString&, const QString&, PageJsonCallback)>
      on_interrupt_subagent;
  std::function<void(const QString&, const QJsonObject&, PageJsonCallback)>
      on_save_plugin_settings;
  std::function<void(const QString& session_id, const QString& local_path,
                     PageJsonCallback done)>
      on_export_session;
  std::function<void(const QString& local_path, PageJsonCallback done)>
      on_import_sessions;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_add_profile;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_remove_profile;
  std::function<void(const QString& name)> on_activate_profile;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_get_profile_soul;
  std::function<void(const QString& name, const QString& content,
                     PageJsonCallback done)>
      on_set_profile_soul;
  std::function<void(const QString& name, PageJsonCallback done)>
      on_export_profile;
  std::function<void(const QString& archive, PageJsonCallback done)>
      on_import_profile;
  std::function<void(const QString& session_id)> on_select_session;
  std::function<QString(const QString& key)> get_pref;
  std::function<void(const QString& key, const QString& value)> set_pref;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void BuildUi();
  void Rebuild();
  void ClearBody();
  static QString TitleFor(const QString& id);

  Theme theme_;
  ProfileSnapshot snapshot_;
  QString page_;
  int last_revision_ = -1;

  QLabel* title_ = nullptr;
  QLabel* subtitle_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* refresh_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  KanbanView* kanban_ = nullptr;
  QWidget* body_ = nullptr;
  QVBoxLayout* body_layout_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PAGES_VIEW_H_
