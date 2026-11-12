// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MAIN_WINDOW_H_
#define HERMES_UI_MAIN_WINDOW_H_

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "src/appearance_prefs.h"
#include "src/background_task.h"
#include "src/model.h"
#include "src/notification_prefs.h"
#include "src/projects.h"
#include "src/supervisor.h"
#include "src/theme.h"

class QLabel;
class QLineEdit;
class QShortcut;
class QSplitter;
#ifdef HERMES_HAS_MULTIMEDIA
class QAudioOutput;
class QMediaPlayer;
class QSoundEffect;
#endif
class QTemporaryFile;
class QTimer;

namespace hermes::ui {

class ChatView;
class CommandPalette;
class Composer;
class DetachedChatWindow;
class NotificationService;
class PagesView;
class PetOverlay;
class QuickEntryDialog;
class BrowserAutomationDialog;
class SessionSearchBar;
class RightRail;
class SettingsView;
class Sidebar;
class StatusBar;
class TitleBar;

// Top-level shell: title bar, sidebar, transcript, composer and status bar.
// Profile switching only rebinds the transcript to another warm snapshot.
class MainWindow : public QWidget {
  Q_OBJECT

 public:
  explicit MainWindow(ThemeMode mode, bool enable_streaming = true,
                      QWidget* parent = nullptr);
  ~MainWindow() override;

  void SetThemeMode(ThemeMode mode);
  void SetThemePreset(const QString& preset_id);
  void ActivateProfile(int index);
  void ActivateProfileByName(const QString& name);
  void SubmitText(const QString& text);
  // Composer slash commands. Returns true when the command was handled
  // locally; unknown commands fall through to the agent as plain prompts.
  bool RunSlashCommand(const QString& text);
  void ShowSlashHelp();
  void CycleSession(int delta);
  void RegisterShortcut(const QString& action, const QKeySequence& sequence,
                        const std::function<void()>& handler);
  void NewSession();
  QString NewSessionCwd() const;
  void SetAutoApprove(bool auto_approve);
  void SetAutoClarify(const QString& answer);
  void ShowCommandPalette();
  void ShowPreferences();
  void ShowSettings();
  void ShowSettingsPage(const QString& page);
  void ShowWorkspacePage(const QString& page);
  void ShowQuickEntry();
  void ShowDetachedChat();
  void ShowBrowserAutomation();
  void RequestHandoff(const QString& platform);
  void TogglePetOverlay();
  void ShowFindBar();
  void HideFindBar();
  void Dictate();
  void ToggleLiveVoice();
  void SubmitQuickEntry(const QString& text);
  void ShowAddProfile();
  void HandleSessionAction(const QString& session_id, const QString& action);
  // Probe hook: pin a session through the same worker path the row menu uses.
  void PinSessionForProbe(const QString& session_id);
  void AttachFileForProbe(const QString& local_path);
  void SelectSessionForProbe(const QString& session_id);
  void ShowDemoTranscript();
  void SetComposerTextForProbe(const QString& text);
  void RunSlashForProbe(const QString& command);
  void SteerForProbe(const QString& text);
  void RunSteerProbe();
  void PurgeProbeSessions();
  void SetDemoRunningForProbe(bool running);
  void DemoCompleteForProbe();
  void PressKeyForProbe(const QString& sequence);
  void ClickForProbe(const QString& target_name);
  void SetFontScale(double scale);
  void ApplyKeybinds(const QHash<QString, QString>& overrides);
  // "action<TAB>sequence" per registered window shortcut, for --key-audit.
  QStringList ShortcutAudit() const;

  // Runs `iterations` synchronous profile switches and prints the measured
  // model+paint distribution. Used by --bench.
  void BenchSwitch(int iterations);

  QString ActiveProfileLabel() const;

 protected:
  void closeEvent(QCloseEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void BuildUi();
  void ApplyTheme();
  void ApplyAppearancePreferences();
  void Refresh();
  void UpdateSidebarChips();
  void UpdateStatus();
  void CheckProfileCompletions();
  void ShowQueuedPromptsDialog();
  void RecordSteeredPrompt(const QString& text);
  // Gateway-owned pending admissions are the queue; legacy local entries only
  // survive for gateways without prompt.cancel. Started rows are no longer
  // queue items and are hidden, matching the desktop queue panel.
  int PendingQueueCount(const ProfileSnapshot& snapshot) const;
  void SyncComposerQueueBadge(int override_count = -1);
  void QueuePromptText(const QString& text);
  void RefreshProjects(bool force = false);
  void LoadProjectCacheForProfile(const QString& profile);
  void ScheduleProjectCacheSave();
  void PersistProjectCacheNow();
  void ApplyProjectState();
  void SyncArtifactsRoot();
  void SyncRightRailProject();
  // Sidebar/right-rail panes are splitter children. Collapse flips a pinned
  // rail width; expansion restores the last user width from native prefs.
  void SetSidebarCollapsed(bool collapsed);
  void SetRightRailEnabled(bool visible);
  void ApplyPaneLayout(bool restore_widths);
  void PersistPaneLayout();
  void OnPaneSplitterMoved();
  void SetProjectScope(const QString& project_id);
  void HandleProjectAction(const QString& project_id, const QString& action);
  void ShowProjectAppearance(const QString& project_id);
  void DismissAutoProject(const QString& project_id);
  void RestoreHiddenProjects();
  void CreateProject();
  void EditProject(const ProjectInfo& project);
  void DeleteProject(const ProjectInfo& project);
  void SetActiveProject(const QString& project_id);
  void AddProjectFolder(const ProjectInfo& project);
  void RemoveProjectFolder(const ProjectInfo& project);
  void SetProjectPrimaryFolder(const ProjectInfo& project);
  const ProjectInfo* FindProject(const QString& project_id) const;
  QString ProjectPath(const QString& project_id) const;
  void UpdateRequest(const ProfileSnapshot& snapshot);
  void UpdateFindBar();
  void ApplyRunIndicators(const ProfileSnapshot& snapshot);
  void ApplyVoiceState(const ProfileSnapshot& snapshot);
  void ApplyGatewayEvents(const ProfileSnapshot& snapshot);
  // Runs a blocking gateway/filesystem operation on the thread pool. The
  // captured request scope keeps profile switching from racing the worker.
  void RunPageBackground(
      std::function<BackgroundResult()> work,
      std::function<void(const QJsonObject&, const QString&)> done);

  Theme theme_;
  ThemeMode mode_ = ThemeMode::kLight;
  AppearancePreferences appearance_;
  QColor user_bubble_base_;
  QString default_ui_family_;
  QString default_mono_family_;
  Supervisor supervisor_;
  TitleBar* title_bar_ = nullptr;
  Sidebar* sidebar_ = nullptr;
  ChatView* chat_view_ = nullptr;
  CommandPalette* command_palette_ = nullptr;
  SettingsView* settings_view_ = nullptr;
  PagesView* pages_view_ = nullptr;
  Composer* composer_ = nullptr;
  QWidget* composer_host_ = nullptr;
  SessionSearchBar* session_search_ = nullptr;
  RightRail* right_rail_ = nullptr;
  QSplitter* body_splitter_ = nullptr;
  QTimer* pane_prefs_timer_ = nullptr;
  int sidebar_width_ = 0;
  int right_rail_width_ = 0;
  bool sidebar_collapsed_ = false;
  bool right_rail_enabled_ = false;
  bool right_rail_collapsed_ = false;
  bool applying_pane_layout_ = false;
  bool pane_layout_ready_ = false;
  QuickEntryDialog* quick_entry_ = nullptr;
  PetOverlay* pet_overlay_ = nullptr;
  DetachedChatWindow* detached_chat_ = nullptr;
  BrowserAutomationDialog* browser_automation_ = nullptr;
  bool browser_consent_muted_ = false;
  QTimer* handoff_poll_timer_ = nullptr;
  QString handoff_session_;
  QString handoff_platform_;
  int handoff_poll_ticks_ = 0;
  QTimer* pet_poll_timer_ = nullptr;
  bool pet_fetch_pending_ = false;
  QString pet_revision_;
  StatusBar* status_bar_ = nullptr;
  QTimer* poll_timer_ = nullptr;
  std::vector<QString> drafts_;
  int last_revision_ = -1;
  int last_unread_sum_ = -1;
  int last_attachment_serial_ = 0;
  int last_slash_catalog_serial_ = -1;
  quint64 last_voice_transcript_serial_ = 0;
  quint64 last_notice_serial_ = 0;
  quint64 last_reaction_serial_ = 0;
  std::vector<QString> pinned_context_;
  std::vector<bool> last_running_;
  QStringList queued_prompts_;
  // Recently steered prompts, newest last. They are already delivered, so the
  // review dialog shows them read-only but can load one back for editing.
  QStringList steered_prompts_;
  ProjectsState project_state_;
  QString project_scope_ = AllProjectsId();
  QStringList dismissed_auto_project_ids_;
  bool project_list_pending_ = false;
  bool project_tree_pending_ = false;
  bool project_cache_dirty_ = false;
  quint64 project_cache_generation_ = 0;
  QTimer* project_cache_timer_ = nullptr;
  qint64 last_project_fetch_ms_ = 0;
  qint64 run_started_ms_ = 0;
  // Per-profile start stamp for the turn that is actually running. Switching
  // chats must not restart the composer's "generating Ns" timer.
  std::map<QString, qint64> run_started_by_profile_;
  // Session id captured when each profile's turn started; the worker clears it
  // on completion, so the notification name must be saved here.
  std::map<QString, QString> running_session_by_profile_;
  QSoundEffect* completion_sound_ = nullptr;
#ifdef HERMES_HAS_MULTIMEDIA
  QMediaPlayer* read_aloud_player_ = nullptr;
  QAudioOutput* read_aloud_output_ = nullptr;
  std::unique_ptr<QTemporaryFile> read_aloud_file_;
#endif
  std::unique_ptr<NotificationService> notifications_;
  QSet<QString> notified_requests_;
  NotificationThrottle notification_throttle_;
  void OnGenerationFinished(const ProfileSnapshot& snapshot, qint64 elapsed_ms,
                            const QString& finished_session_id = QString(),
                            int profile_index = -1);
  void NotifyPendingRequests(const ProfileSnapshot& snapshot);
  void EditMessage(const ChatMessage& message, const QString& text);
  void RegenerateMessage(const ChatMessage& message);
  void ReadAloud(const ChatMessage& message, const QString& text);
  void ApplyAudioOutputDevice(const QString& device_id);
  void RefreshPetOverlay();
  void PollPetOverlay();
  void PollHandoffState();
  void HidePetOverlay();
  void ApplyPetState(const ProfileSnapshot& snapshot);
  void PinMessage(const ChatMessage& message, const QString& text);
  void PinLastAssistantMessage();
  void ClearPinnedContext();
  void BranchFromMessage(const ChatMessage& message);
  void RewindLastTurn(const ChatMessage& message);
  void ReactToMessage(const ChatMessage& message, const QString& emoji);
  void DeleteConversation();
  QString ActiveSessionId() const;
  void SendEditedPrompt(const QString& text, qint64 user_row_id);
  bool completion_sound_enabled_ = true;
  bool completion_flash_enabled_ = true;
  void ToggleCompletionPref(const QString& key, bool* target);
  int last_active_ = -1;
  int last_connected_ = -1;
  double last_switch_ms_ = 0.0;
  double font_scale_ = 1.0;
  QString theme_preset_;
  // Shortcuts are dispatched from an application-level event filter rather
  // than QShortcut: a modal popup, a focused QTextEdit or a tool window can
  // swallow a window-level shortcut, which is what made Cmd+K look dead.
  struct KeyBinding {
    QString action;
    QKeySequence sequence;
    QString text;
    std::function<void()> handler;
  };
  std::vector<KeyBinding> key_bindings_;
  QSet<QString> shortcut_keys_;

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  bool demo_mode_ = false;
  bool auto_approve_ = false;
  QString auto_clarify_;
  QString auto_answered_id_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_MAIN_WINDOW_H_
