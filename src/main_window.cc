// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/main_window.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSaveFile>
#include <QScreen>
#include <QShortcut>
#include <QSplitter>
#include <QTemporaryFile>
#include <QTextEdit>
#include <QUrl>
#include <QUuid>

#include "src/theme_presets.h"
#ifdef HERMES_HAS_MULTIMEDIA
#include <QAudioDevice>
#include <QAudioOutput>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#endif
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "src/audio_data_url.h"
#include "src/browser_automation.h"
#include "src/cache_store.h"
#include "src/chat_view.h"
#include "src/command_palette.h"
#include "src/composer.h"
#include "src/detached_chat.h"
#include "src/gateway_events.h"
#include "src/gateway_http.h"
#include "src/gateway_protocol.h"
#include "src/kanban_view.h"
#include "src/model_visibility.h"
#include "src/native_prefs.h"
#include "src/notification_prefs.h"
#include "src/notification_service.h"
#include "src/pages_view.h"
#include "src/pet_overlay.h"
#include "src/profile_selection.h"
#include "src/project_appearance.h"
#include "src/project_dialog.h"
#include "src/projects.h"
#include "src/prompt_history_dialog.h"
#include "src/prompt_submit.h"
#include "src/quick_entry.h"
#include "src/request_bar.h"
#include "src/right_rail.h"
#include "src/session_search_bar.h"
#include "src/settings_view.h"
#include "src/sidebar.h"
#include "src/slash_commands.h"
#include "src/status_bar.h"
#include "src/title_bar.h"
#include "src/widget_theme.h"

namespace hermes::ui {
namespace {

// The desktop derives its composer history from the live session messages
// instead of keeping a parallel ring; do the same so edited/resumed sessions
// cannot drift.
QStringList ComposerHistory(const ProfileSnapshot& snapshot) {
  QStringList history;
  for (auto it = snapshot.messages.rbegin(); it != snapshot.messages.rend();
       ++it) {
    if (it->role != MessageRole::kUser) {
      continue;
    }
    QString text;
    for (const MessageBlock& block : it->blocks) {
      if (block.kind == BlockKind::kTool) {
        continue;
      }
      if (!text.isEmpty()) {
        text += QLatin1Char('\n');
      }
      text += block.text;
    }
    if (text.isEmpty()) {
      text = it->raw_text;
    }
    text = text.trimmed();
    if (!text.isEmpty()) {
      history.append(text);
    }
  }
  return history;
}

QString AssistantMessageText(const ChatMessage& message) {
  QString text;
  for (const MessageBlock& block : message.blocks) {
    if (block.kind == BlockKind::kTool) {
      continue;
    }
    if (!text.isEmpty()) {
      text += QStringLiteral("\n\n");
    }
    text += block.text;
  }
  if (text.trimmed().isEmpty()) {
    text = message.raw_text;
  }
  return text.trimmed();
}

QString SessionNotificationName(const ProfileSnapshot& snapshot,
                                const QString& session_id) {
  for (const SessionSummary& session : snapshot.sessions) {
    if (session.id == session_id) {
      if (!session.title.trimmed().isEmpty()) {
        return session.title.trimmed();
      }
      if (!session.preview.trimmed().isEmpty()) {
        return session.preview.trimmed();
      }
      break;
    }
  }
  return session_id.isEmpty() ? QStringLiteral("this chat")
                              : QStringLiteral("#") + session_id.right(6);
}

}  // namespace

MainWindow::MainWindow(ThemeMode mode, bool enable_streaming, QWidget* parent)
    : QWidget(parent), mode_(mode), supervisor_(enable_streaming) {
  setObjectName(QStringLiteral("mainWindow"));
  // Application-level: a focused editor, an open popup or a dialog would keep a
  // window-level QShortcut from ever firing.
  qApp->installEventFilter(this);
  theme_ = mode_ == ThemeMode::kDark ? Theme::Dark() : Theme::Light();
  default_ui_family_ = theme_.ui_family;
  default_mono_family_ = theme_.mono_family;
  user_bubble_base_ = theme_.user_bubble;
  appearance_ = LoadAppearancePreferences(
      [](const QString& key) { return NativePref(key); });
  font_scale_ = appearance_.ui_scale;
  ApplyAppearanceToTheme(appearance_, user_bubble_base_, default_ui_family_,
                         default_mono_family_, &theme_);
  setWindowTitle(QStringLiteral("Hermes"));
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
  setMinimumSize(980, 640);
  resize(1280, 820);
  BuildUi();
  ApplyAppearancePreferences();
  supervisor_.Start();
  // Resume the profile the user left. A deleted/renamed profile falls back to
  // the gateway's default (index 0); stale indexes never select a stranger.
  QStringList profile_names;
  for (int index = 0; index < supervisor_.profile_count(); ++index) {
    profile_names.push_back(supervisor_.ProfileName(index));
  }
  const int saved_profile = ProfileIndexByName(
      profile_names, NativePref(QStringLiteral("native.active_profile")));
  if (saved_profile > 0) {
    supervisor_.Activate(saved_profile);
    sidebar_->SetActiveIndex(saved_profile);
  }
  // Preferences that style the shell live behind the same settings pages; read
  // them once so saved keybinds and font scale apply before Settings is opened.
  supervisor_.FetchPage(supervisor_.active_index(), QStringLiteral("keybinds"));
  supervisor_.FetchPage(supervisor_.active_index(),
                        QStringLiteral("appearance"));
  drafts_.assign(static_cast<std::size_t>(supervisor_.profile_count()),
                 QString());
  dismissed_auto_project_ids_ = DecodeDismissedAutoProjectIds(
      NativePref(QStringLiteral("native.dismissed_auto_projects")));

  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(60);
  connect(poll_timer_, &QTimer::timeout, this, [this]() { Refresh(); });
  poll_timer_->start();

  // Project scope changes are cheap and frequent; coalesce cache writes so a
  // click never turns into a synchronous filesystem operation on the UI.
  project_cache_timer_ = new QTimer(this);
  project_cache_timer_->setSingleShot(true);
  project_cache_timer_->setInterval(750);
  connect(project_cache_timer_, &QTimer::timeout, this,
          [this]() { PersistProjectCacheNow(); });

  // All window shortcuts go through one registry. Two shortcuts on the same
  // sequence make Qt treat the key as ambiguous and fire neither (that is why
  // Cmd+K appeared dead), so duplicates are rejected here and by the
  // --key-audit probe.
  RegisterShortcut(QStringLiteral("profile_1"),
                   QKeySequence(Qt::CTRL | Qt::Key_1),
                   [this]() { ActivateProfile(0); });
  RegisterShortcut(QStringLiteral("profile_2"),
                   QKeySequence(Qt::CTRL | Qt::Key_2),
                   [this]() { ActivateProfile(1); });
  RegisterShortcut(QStringLiteral("profile_3"),
                   QKeySequence(Qt::CTRL | Qt::Key_3),
                   [this]() { ActivateProfile(2); });
  RegisterShortcut(QStringLiteral("profile_4"),
                   QKeySequence(Qt::CTRL | Qt::Key_4),
                   [this]() { ActivateProfile(3); });
  RegisterShortcut(QStringLiteral("theme"),
                   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D), [this]() {
                     SetThemeMode(mode_ == ThemeMode::kLight
                                      ? ThemeMode::kDark
                                      : ThemeMode::kLight);
                   });
  RegisterShortcut(QStringLiteral("new_chat"),
                   QKeySequence(Qt::CTRL | Qt::Key_N),
                   [this]() { NewSession(); });
  RegisterShortcut(QStringLiteral("new_kanban_task"),
                   QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N), [this]() {
                     ShowWorkspacePage(QStringLiteral("kanban"));
                     pages_view_->Kanban()->NewTask();
                   });
  RegisterShortcut(
      QStringLiteral("interrupt"), QKeySequence(Qt::CTRL | Qt::Key_Period),
      [this]() { supervisor_.Interrupt(supervisor_.active_index()); });
  RegisterShortcut(QStringLiteral("find_sessions"),
                   QKeySequence(Qt::CTRL | Qt::Key_F),
                   [this]() { ShowFindBar(); });

  const auto snapshot = supervisor_.ActiveSnapshot();
  if (!enable_streaming) {
    chat_view_->SetFollowBottom(false);
  }
  chat_view_->SetSnapshot(snapshot);
  if (snapshot) {
    chat_view_->SetProfileName(snapshot->label);
    sidebar_->SetSnapshot(*snapshot);
    sidebar_->SetActiveIndex(supervisor_.active_index());
    composer_->SetModelCatalog(snapshot->model_providers, snapshot->provider,
                               snapshot->model);
    composer_->SetReasoning(snapshot->reasoning_effort);
    composer_->SetUsage(snapshot->usage.context_used,
                        snapshot->usage.context_max);
    UpdateStatus();
    if (right_rail_ != nullptr) {
      right_rail_->SetSnapshot(*snapshot);
    }
    composer_->SetMentionRoot(snapshot->home);
    UpdateComposerHistory(*snapshot);
    ApplyRunIndicators(*snapshot);
    last_voice_transcript_serial_ = snapshot->voice_transcript_serial;
    ApplyVoiceState(*snapshot);
    ApplyPetState(*snapshot);
    if (detached_chat_ != nullptr) {
      detached_chat_->SetSnapshot(snapshot);
    }
    if (settings_view_ != nullptr && settings_view_->IsOpen()) {
      settings_view_->SetSnapshot(*snapshot);
    }
    UpdateRequest(*snapshot);
    last_revision_ = snapshot->revision;
  }
  if (!enable_streaming) {
    chat_view_->ScrollToTop();
  }
  UpdateSidebarChips();
  UpdateStatus();
}

MainWindow::~MainWindow() {
  if (project_cache_timer_ != nullptr) {
    project_cache_timer_->stop();
  }
  if (project_cache_dirty_ && project_state_.loaded &&
      !project_state_.profile.isEmpty()) {
    CachedProjectState cache;
    cache.profile = project_state_.profile;
    cache.scope = project_scope_;
    cache.projects = project_state_;
    QString error;
    SaveProjectCache(cache, &error);
  }
  supervisor_.Stop();
}

void MainWindow::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  title_bar_ = new TitleBar(theme_, this);
  root->addWidget(title_bar_);

  sidebar_width_ = NativePref(QStringLiteral("layout.sidebar_width")).toInt();
  if (sidebar_width_ <= 0) {
    sidebar_width_ = theme_.sidebar_width;
  }
  right_rail_width_ =
      NativePref(QStringLiteral("layout.right_rail_width")).toInt();
  if (right_rail_width_ <= 0) {
    right_rail_width_ = 340;
  }
  sidebar_collapsed_ = NativePref(QStringLiteral("layout.sidebar_collapsed")) ==
                       QStringLiteral("1");
  const QString rail_visible =
      NativePref(QStringLiteral("layout.right_rail_visible"));
  right_rail_enabled_ =
      rail_visible.isEmpty() ? true : rail_visible == QStringLiteral("1");
  right_rail_collapsed_ =
      NativePref(QStringLiteral("layout.right_rail_collapsed")) ==
      QStringLiteral("1");

  body_splitter_ = new QSplitter(Qt::Horizontal, this);
  body_splitter_->setObjectName(QStringLiteral("shellSplitter"));
  body_splitter_->setChildrenCollapsible(false);
  body_splitter_->setHandleWidth(5);
  root->addWidget(body_splitter_, 1);

  sidebar_ = new Sidebar(theme_, body_splitter_);
  body_splitter_->addWidget(sidebar_);

  auto* chat_column = new QWidget(this);
  auto* chat_layout = new QVBoxLayout(chat_column);
  chat_layout->setContentsMargins(0, 0, 0, 0);
  chat_layout->setSpacing(0);

  session_search_ = new SessionSearchBar(theme_, chat_column);
  session_search_->on_session_open = [this](const QString& session_id) {
    supervisor_.SelectSession(supervisor_.active_index(), session_id);
    ClearPinnedContext();
  };
  session_search_->SetSearchHandler(
      [this](const QString& query,
             std::function<void(const QJsonObject&, const QString&)> done) {
        supervisor_.SearchSessions(supervisor_.active_index(), query, 20,
                                   std::move(done));
      });
  session_search_->SetSuggestionsProvider([this]() {
    QStringList suggestions;
    if (const auto snapshot = supervisor_.ActiveSnapshot()) {
      for (const SessionSummary& session : snapshot->sessions) {
        if (!session.title.trimmed().isEmpty()) {
          suggestions.append(session.title.trimmed());
        }
      }
    }
    for (const ProjectInfo& project : project_state_.projects) {
      if (!project.name.trimmed().isEmpty()) {
        suggestions.append(project.name.trimmed());
      }
    }
    return suggestions;
  });
  chat_layout->addWidget(session_search_);

  chat_view_ = new ChatView(theme_, chat_column);
  chat_view_->SetEditMessageHandler(
      [this](const ChatMessage& message, const QString& text) {
        EditMessage(message, text);
      });
  chat_view_->SetRegenerateMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        RegenerateMessage(message);
      });
  chat_view_->SetReadAloudHandler(
      [this](const ChatMessage& message, const QString& text) {
        ReadAloud(message, text);
      });
  chat_view_->SetPinMessageHandler(
      [this](const ChatMessage& message, const QString& text) {
        PinMessage(message, text);
      });
  chat_view_->SetBranchMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        BranchFromMessage(message);
      });
  chat_view_->SetRewindMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        RewindLastTurn(message);
      });
  chat_view_->SetReactMessageHandler(
      [this](const ChatMessage& message, const QString& emoji) {
        ReactToMessage(message, emoji);
      });
  chat_view_->SetDeleteConversationHandler([this]() { DeleteConversation(); });
  chat_layout->addWidget(chat_view_, 1);

  // Workspace pages share the chat column: the desktop mounts them inside the
  // workspace pane, so the sidebar and title bar stay put while one is open.
  pages_view_ = new PagesView(theme_, chat_column);
  pages_view_->hide();
  chat_layout->addWidget(pages_view_, 1);

  chat_view_->SetRequestRespondHandler([this](const QString& request_id,
                                              const QJsonObject& result) {
    supervisor_.RespondRequest(supervisor_.active_index(), request_id, result);
  });

  composer_host_ = new QWidget(chat_column);
  QWidget* composer_host = composer_host_;
  auto* composer_layout = new QHBoxLayout(composer_host);
  composer_layout->setContentsMargins(24, 0, 24, 12);
  composer_layout->setSpacing(0);
  composer_ = new Composer(theme_, composer_host);
  composer_->SetHiddenModels(
      ParseHiddenModels(NativePref(QStringLiteral("native.model_visibility"))));
  composer_->setMaximumWidth(780);
  composer_->setMinimumWidth(460);
  composer_->on_stop = [this]() {
    status_bar_->ShowTransient(QStringLiteral("Stopping…"), 2000);
    supervisor_.Interrupt(supervisor_.active_index());
  };
  composer_->on_model_selected = [this](const QString& provider,
                                        const QString& model) {
    supervisor_.SetModel(supervisor_.active_index(), provider, model);
  };
  composer_->on_use_default_model = [this]() {
    supervisor_.UseDefaultModel(supervisor_.active_index());
  };
  composer_->on_refresh_models = [this]() {
    supervisor_.RefreshModelOptions(supervisor_.active_index());
  };
  composer_->on_reasoning_selected = [this](const QString& effort) {
    supervisor_.SetReasoning(supervisor_.active_index(), effort);
  };
  composer_layout->addStretch(1);
  composer_layout->addWidget(composer_, 0);
  composer_layout->addStretch(1);
  composer_host->setFixedHeight(128);
  QObject::connect(composer_, &Composer::HeightChanged, composer_host,
                   [composer_host](int height) {
                     composer_host->setFixedHeight(height + 12);
                   });
  chat_layout->addWidget(composer_host);

  right_rail_ = new RightRail(theme_, body_splitter_);
  right_rail_->SetCollapsed(right_rail_collapsed_);
  right_rail_->on_collapsed_changed = [this](bool collapsed) {
    right_rail_collapsed_ = collapsed;
    ApplyPaneLayout(true);
    PersistPaneLayout();
  };
  if (right_rail_enabled_) {
    right_rail_->show();
  } else {
    right_rail_->hide();
  }
  right_rail_->on_steer_subagent =
      [this](const QString& session_id, const QString& subagent_id,
             const QString& text, RightRail::SubagentControlDone done) {
        supervisor_.SteerSubagent(supervisor_.active_index(), session_id,
                                  subagent_id, text, std::move(done));
      };
  right_rail_->on_interrupt_subagent =
      [this](const QString& session_id, const QString& subagent_id,
             RightRail::SubagentControlDone done) {
        supervisor_.InterruptSubagent(supervisor_.active_index(), session_id,
                                      subagent_id, std::move(done));
      };
  right_rail_->on_artifact_open = [this](const SessionArtifact& artifact) {
    status_bar_->ShowTransient(
        QStringLiteral("Output: %1").arg(artifact.value));
    ShowWorkspacePage(QStringLiteral("artifacts"));
  };
  right_rail_->on_artifact_save = [this](const SessionArtifact& artifact,
                                         const QString& suggested_name) {
    const QString path =
        QFileDialog::getSaveFileName(this, QStringLiteral("Save artifact"),
                                     QDir::home().filePath(suggested_name));
    if (path.isEmpty() || artifact.versions.empty()) {
      return;
    }
    const int index =
        std::max(0, std::min(artifact.version_index,
                             static_cast<int>(artifact.versions.size()) - 1));
    const QString content =
        artifact.versions[static_cast<std::size_t>(index)].content;
    RunBackground(
        this,
        [path, content]() {
          QSaveFile file(path);
          if (!file.open(QIODevice::WriteOnly)) {
            return BackgroundResult{
                QJsonObject(), QStringLiteral("Could not open %1").arg(path)};
          }
          if (file.write(content.toUtf8()) < 0 || !file.commit()) {
            return BackgroundResult{
                QJsonObject(), QStringLiteral("Could not write %1").arg(path)};
          }
          return BackgroundResult{QJsonObject(), QString()};
        },
        [this, path](const BackgroundResult& result) {
          if (result.error.isEmpty()) {
            status_bar_->ShowTransient(
                QStringLiteral("Saved artifact: %1").arg(path));
          } else {
            status_bar_->ShowTransient(result.error);
          }
        });
  };
  right_rail_->on_generate_commit_message =
      [this](
          const QString& diff, const QString& recent_commits,
          const QString& avoid,
          std::function<void(const QString& text, const QString& error)> done) {
        const int index = supervisor_.active_index();
        const auto snapshot = supervisor_.ActiveSnapshot();
        QJsonObject variables{
            {QStringLiteral("avoid"), avoid},
            {QStringLiteral("diff"), diff},
            {QStringLiteral("recent_commits"), recent_commits}};
        QJsonObject params{
            {QStringLiteral("template"), QStringLiteral("commit_message")},
            {QStringLiteral("temperature"), 0.8},
            {QStringLiteral("variables"), variables}};
        if (snapshot != nullptr) {
          if (!snapshot->selected_session_id.isEmpty()) {
            params.insert(QStringLiteral("session_id"),
                          snapshot->selected_session_id);
          }
          if (!snapshot->name.isEmpty()) {
            params.insert(QStringLiteral("profile"), snapshot->name);
          }
        }
        supervisor_.RequestRpc(
            index, QStringLiteral("llm.oneshot"), params,
            [done](const QJsonObject& result, const QString& error) {
              if (!error.isEmpty()) {
                done(QString(), error);
                return;
              }
              done(result.value(QStringLiteral("text")).toString(), QString());
            });
      };
  right_rail_->on_timeline_open = [this](qint64 row_id) {
    chat_view_->ScrollToMessageRow(row_id);
  };
  body_splitter_->addWidget(chat_column);
  body_splitter_->setStretchFactor(1, 1);
  body_splitter_->addWidget(right_rail_);
  QObject::connect(body_splitter_, &QSplitter::splitterMoved, this,
                   [this](int, int) { OnPaneSplitterMoved(); });
  pane_prefs_timer_ = new QTimer(this);
  pane_prefs_timer_->setSingleShot(true);
  pane_prefs_timer_->setInterval(350);
  QObject::connect(pane_prefs_timer_, &QTimer::timeout, this,
                   [this]() { PersistPaneLayout(); });
  ApplyPaneLayout(true);

  notifications_ = std::make_unique<NotificationService>(this);
  completion_sound_enabled_ =
      NativePref(QStringLiteral("completion_sound")) != QStringLiteral("0");
  completion_flash_enabled_ =
      NativePref(QStringLiteral("completion_flash")) != QStringLiteral("0");
#ifdef HERMES_HAS_MULTIMEDIA
  completion_sound_ = new QSoundEffect(this);
  completion_sound_->setSource(
      QUrl(QStringLiteral("qrc:/sounds/complete.wav")));
  completion_sound_->setVolume(0.35);
  read_aloud_player_ = new QMediaPlayer(this);
  read_aloud_output_ = new QAudioOutput(this);
  read_aloud_output_->setVolume(0.8);
  read_aloud_player_->setAudioOutput(read_aloud_output_);
  ApplyAudioOutputDevice(NativePref(QStringLiteral("audio.output_device")));
  connect(read_aloud_player_, &QMediaPlayer::errorOccurred, this,
          [this](QMediaPlayer::Error, const QString& message) {
            status_bar_->ShowTransient(
                QStringLiteral("Read aloud failed: %1")
                    .arg(message.isEmpty() ? QStringLiteral("playback error")
                                           : message));
          });
#endif
  status_bar_ = new StatusBar(theme_, this);
  connect(status_bar_, &StatusBar::QueuedReviewRequested, this,
          [this]() { ShowQueuedPromptsDialog(); });
  root->addWidget(status_bar_);

  // A child of the window, not of the layout: the palette covers the title
  // bar and sidebar, exactly like the desktop overlay.
  command_palette_ = new CommandPalette(theme_, this);
  command_palette_->hide();
  RegisterShortcut(QStringLiteral("command_palette"),
                   QKeySequence(QStringLiteral("Ctrl+K")),
                   [this]() { ShowCommandPalette(); });
  // The desktop binds both Cmd+K and Cmd+P to the palette (nav.commandPalette
  // defaults ['mod+k', 'mod+p']).
  RegisterShortcut(QStringLiteral("command_palette_alt"),
                   QKeySequence(QStringLiteral("Ctrl+P")),
                   [this]() { ShowCommandPalette(); });
  RegisterShortcut(QStringLiteral("model_picker"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+M")), [this]() {
                     command_palette_->OpenWith(QStringLiteral("model"));
                   });
  RegisterShortcut(QStringLiteral("keybinds_panel"),
                   QKeySequence(QStringLiteral("Ctrl+/")),
                   [this]() { ShowSettingsPage(QStringLiteral("keybinds")); });
  RegisterShortcut(QStringLiteral("toggle_sidebar"),
                   QKeySequence(QStringLiteral("Ctrl+B")),
                   [this]() { SetSidebarCollapsed(!sidebar_collapsed_); });
  RegisterShortcut(QStringLiteral("toggle_right_rail"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+B")),
                   [this]() { SetRightRailEnabled(!right_rail_enabled_); });
  RegisterShortcut(QStringLiteral("toggle_statusbar"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this]() {
                     status_bar_->setVisible(!status_bar_->isVisible());
                   });
  RegisterShortcut(
      QStringLiteral("scroll_page_up"), QKeySequence(QStringLiteral("PgUp")),
      [this]() { chat_view_->ScrollBy(-chat_view_->height() * 3 / 4); });
  RegisterShortcut(QStringLiteral("scroll_page_down"),
                   QKeySequence(QStringLiteral("PgDown")), [this]() {
                     chat_view_->ScrollBy(chat_view_->height() * 3 / 4);
                   });
  // Cmd+Tab is the macOS app switcher, so session cycling uses the bracket
  // chords the desktop binds for its next/previous tab.
  RegisterShortcut(QStringLiteral("session_next"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+]")),
                   [this]() { CycleSession(1); });
  RegisterShortcut(QStringLiteral("session_prev"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+[")),
                   [this]() { CycleSession(-1); });
  RegisterShortcut(QStringLiteral("live_voice"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+M")),
                   [this]() { ToggleLiveVoice(); });
  RegisterShortcut(QStringLiteral("pin_context"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+P")),
                   [this]() { PinLastAssistantMessage(); });
  RegisterShortcut(QStringLiteral("detach_chat"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+N")),
                   [this]() { ShowDetachedChat(); });
  RegisterShortcut(QStringLiteral("pet_overlay"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+O")),
                   [this]() { TogglePetOverlay(); });
  RegisterShortcut(QStringLiteral("browser_automation"),
                   QKeySequence(QStringLiteral("Ctrl+Shift+D")),
                   [this]() { ShowBrowserAutomation(); });

  settings_view_ = new SettingsView(theme_, this);
  settings_view_->hide();
  settings_view_->on_save = [this](const QJsonObject& patch) {
    supervisor_.SaveConfig(supervisor_.active_index(), patch);
  };
  settings_view_->on_refetch = [this]() {
    supervisor_.FetchSettings(supervisor_.active_index());
  };
  settings_view_->on_page_request = [this](const QString& page) {
    supervisor_.FetchPage(supervisor_.active_index(), page);
  };
  settings_view_->on_page_action =
      [this](const QString& page, const QString& method, const QString& path,
             const QJsonObject& body) {
        supervisor_.PageAction(supervisor_.active_index(), page, method, path,
                               body);
      };
  settings_view_->on_theme = [this](ThemeMode theme_mode) {
    SetThemeMode(theme_mode);
  };
  settings_view_->on_theme_preset = [this](const QString& preset_id) {
    SetThemePreset(preset_id);
  };
  settings_view_->on_font_scale = [this](double scale) { SetFontScale(scale); };
  settings_view_->on_appearance = [this]() { ApplyAppearancePreferences(); };
  settings_view_->on_pref_changed = [this](const QString& key,
                                           const QString& value) {
    if (key == QStringLiteral("audio.output_device")) {
      ApplyAudioOutputDevice(value);
    }
  };
  settings_view_->on_keybinds =
      [this](const QHash<QString, QString>& overrides) {
        ApplyKeybinds(overrides);
      };
  settings_view_->on_send_notification = [this](const QString& title,
                                                const QString& body,
                                                bool silent, QString* error) {
    return notifications_ != nullptr &&
           notifications_->Show(title, body, silent, error);
  };
  settings_view_->on_close = [this]() { settings_view_->hide(); };
  settings_view_->on_select_session = [this](const QString& session_id) {
    supervisor_.SelectSession(supervisor_.active_index(), session_id);
    ClearPinnedContext();
    settings_view_->hide();
  };

  // Workspace pages (Capabilities / Messaging / Artifacts / Scheduled tasks /
  // Bots) render over the chat exactly like the desktop's workspace pane.
  pages_view_->hide();
  pages_view_->on_close = [this]() {
    pages_view_->hide();
    chat_view_->show();
    if (composer_host_ != nullptr) {
      composer_host_->show();
    }
    sidebar_->SetActivePage(QString());
  };
  auto* quick_entry_shortcut =
      new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Space")), this);
  quick_entry_shortcut->setObjectName(QStringLiteral("quickEntryShortcut"));
  QObject::connect(quick_entry_shortcut, &QShortcut::activated, this,
                   [this]() { ShowQuickEntry(); });
  pages_view_->on_page_request = [this](const QString& page) {
    if (page == QStringLiteral("kanban")) {
      pages_view_->Kanban()->Refresh();
      return;
    }
    supervisor_.FetchPage(supervisor_.active_index(), page);
  };
  pages_view_->on_page_action =
      [this](const QString& page, const QString& method, const QString& path,
             const QJsonObject& body) {
        supervisor_.PageAction(supervisor_.active_index(), page, method, path,
                               body);
      };
  if (KanbanView* kanban = pages_view_->Kanban()) {
    kanban->get_pref = [](const QString& key) { return NativePref(key); };
    kanban->set_pref = [](const QString& key, const QString& value) {
      SetNativePref(key, value);
    };
    kanban->on_notify = [this](const QString& title, const QString& body) {
      status_bar_->ShowTransient(
          body.isEmpty() ? title : QStringLiteral("%1 — %2").arg(title, body));
    };
    kanban->on_request =
        [this](const QString& method, const QString& path,
               const QJsonObject& body, KanbanView::JsonCallback done) {
          const Supervisor::RequestScope scope =
              supervisor_.CaptureRequestScope();
          RunPageBackground(
              [scope, method, path, body]() {
                BackgroundResult result;
                GatewayHttp http(scope.home, scope.profile, scope.endpoint);
                QJsonDocument document;
                const QString api_path =
                    QStringLiteral("/api/plugins/kanban") + path;
                if (!http.RequestJson(method, api_path, body, &document,
                                      &result.error)) {
                  return result;
                }
                if (document.isObject()) {
                  result.data = document.object();
                }
                return result;
              },
              std::move(done));
        };
    kanban->on_upload_attachment =
        [this](const QString& path, const QString& local_path,
               KanbanView::JsonCallback done) {
          const Supervisor::RequestScope scope =
              supervisor_.CaptureRequestScope();
          RunPageBackground(
              [scope, path, local_path]() {
                BackgroundResult result;
                GatewayHttp http(scope.home, scope.profile, scope.endpoint);
                QString uploaded;
                const QString api_path =
                    QStringLiteral("/api/plugins/kanban") + path;
                if (!http.PostFile(api_path, local_path, &uploaded,
                                   &result.error)) {
                  return result;
                }
                result.data.insert(QStringLiteral("path"), uploaded);
                return result;
              },
              std::move(done));
        };
    kanban->on_download_attachment =
        [this](const QString& path, const QString& local_path,
               KanbanView::JsonCallback done) {
          const Supervisor::RequestScope scope =
              supervisor_.CaptureRequestScope();
          RunPageBackground(
              [scope, path, local_path]() {
                BackgroundResult result;
                GatewayHttp http(scope.home, scope.profile, scope.endpoint);
                const QString api_path =
                    QStringLiteral("/api/plugins/kanban") + path;
                http.DownloadFile(api_path, local_path, &result.error);
                return result;
              },
              std::move(done));
        };
  }
  pages_view_->on_open_dir = [this](const QString& path) {
    supervisor_.OpenArtifactsDir(supervisor_.active_index(), path);
  };
  pages_view_->on_preview_file = [this](const QString& path) {
    supervisor_.PreviewArtifact(supervisor_.active_index(), path);
  };
  pages_view_->on_save_file = [this](const QString& path, const QString& text) {
    supervisor_.SaveArtifact(supervisor_.active_index(), path, text);
  };
  pages_view_->on_upload_file = [this](const QString& destination,
                                       const QString& local_path,
                                       PageJsonCallback done) {
    RunPageBackground(
        [this, destination, local_path]() {
          BackgroundResult result;
          QString uploaded_path;
          if (!supervisor_.UploadArtifact(destination, local_path,
                                          &uploaded_path, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("uploaded_path"), uploaded_path);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_download_file = [this](const QString& remote_path,
                                         const QString& local_path,
                                         PageJsonCallback done) {
    RunPageBackground(
        [this, remote_path, local_path]() {
          BackgroundResult result;
          supervisor_.DownloadArtifact(remote_path, local_path, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_test_mcp_server = [this](const QString& name,
                                           PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          supervisor_.TestMcpServer(name, &result.data, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_list_mcp_catalog = [this](PageJsonCallback done) {
    RunPageBackground(
        [this]() {
          BackgroundResult result;
          QJsonArray entries;
          if (!supervisor_.ListMcpCatalog(&entries, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("entries"), entries);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_get_mcp_config = [this](PageJsonCallback done) {
    RunPageBackground(
        [this]() {
          BackgroundResult result;
          QJsonObject servers;
          if (!supervisor_.GetMcpConfig(&servers, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("servers"), servers);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_replace_mcp_config = [this](const QJsonObject& servers,
                                              PageJsonCallback done) {
    RunPageBackground(
        [this, servers]() {
          BackgroundResult result;
          supervisor_.ReplaceMcpConfig(servers, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_get_learning_node = [this](const QString& id,
                                             PageJsonCallback done) {
    RunPageBackground(
        [this, id]() {
          BackgroundResult result;
          supervisor_.GetLearningNode(id, &result.data, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_get_computer_use_status = [this](PageJsonCallback done) {
    supervisor_.GetComputerUseStatus(supervisor_.active_index(),
                                     std::move(done));
  };
  pages_view_->on_grant_computer_use_permissions =
      [this](PageJsonCallback done) {
        supervisor_.GrantComputerUsePermissions(supervisor_.active_index(),
                                                std::move(done));
      };
  pages_view_->on_get_action_status = [this](const QString& name,
                                             PageJsonCallback done) {
    supervisor_.GetActionStatus(supervisor_.active_index(), name,
                                std::move(done));
  };
  pages_view_->on_list_terminal_backends = [this](PageJsonCallback done) {
    supervisor_.ListTerminalBackends(supervisor_.active_index(),
                                     std::move(done));
  };
  pages_view_->on_select_terminal_backend = [this](const QString& backend,
                                                   PageJsonCallback done) {
    supervisor_.SelectTerminalBackend(supervisor_.active_index(), backend,
                                      std::move(done));
  };
  pages_view_->on_list_connectors = [this](PageJsonCallback done) {
    QJsonObject params{
        {QStringLiteral("owner"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("account")}}}};
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (snapshot != nullptr && !snapshot->name.isEmpty()) {
      params.insert(QStringLiteral("profile"), snapshot->name);
    }
    supervisor_.RequestRpc(supervisor_.active_index(),
                           QStringLiteral("connectors.list"), params,
                           std::move(done));
  };
  pages_view_->on_connect_connector =
      [this](const QString& connector, bool reconnect, PageJsonCallback done) {
        QJsonObject params{
            {QStringLiteral("owner"),
             QJsonObject{{QStringLiteral("type"), QStringLiteral("account")}}},
            {QStringLiteral("connectors"), QJsonArray{connector}},
            {QStringLiteral("reconnect"), reconnect}};
        const auto snapshot = supervisor_.ActiveSnapshot();
        if (snapshot != nullptr && !snapshot->name.isEmpty()) {
          params.insert(QStringLiteral("profile"), snapshot->name);
        }
        const int index = supervisor_.active_index();
        supervisor_.RequestRpc(
            index, QStringLiteral("connectors.connect"), params,
            [this, index, done = std::move(done)](const QJsonObject& result,
                                                  const QString& error) {
              if (error.isEmpty()) {
                supervisor_.RememberAccountOperation(index, result);
              }
              done(result, error);
            });
      };
  pages_view_->on_connector_operation_status = [this](const QString& op_id,
                                                      PageJsonCallback done) {
    QJsonObject params{
        {QStringLiteral("owner"),
         QJsonObject{{QStringLiteral("type"), QStringLiteral("account")}}},
        {QStringLiteral("op_id"), op_id}};
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (snapshot != nullptr && !snapshot->name.isEmpty()) {
      params.insert(QStringLiteral("profile"), snapshot->name);
    }
    supervisor_.RequestRpc(supervisor_.active_index(),
                           QStringLiteral("connectors.operation.status"),
                           params, std::move(done));
  };
  pages_view_->on_connector_rpc = [this](const QString& method,
                                         const QJsonObject& params,
                                         PageJsonCallback done) {
    QJsonObject request = params;
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (snapshot != nullptr && !snapshot->name.isEmpty()) {
      request.insert(QStringLiteral("profile"), snapshot->name);
    }
    supervisor_.RequestRpc(supervisor_.active_index(), method, request,
                           std::move(done));
  };
  pages_view_->on_start_mcp_oauth = [this](const QString& name,
                                           PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          supervisor_.StartMcpOAuth(name, &result.data, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_poll_mcp_oauth = [this](const QString& flow_id,
                                          PageJsonCallback done) {
    RunPageBackground(
        [this, flow_id]() {
          BackgroundResult result;
          supervisor_.PollMcpOAuth(flow_id, &result.data, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_cancel_mcp_oauth = [this](const QString& flow_id,
                                            PageJsonCallback done) {
    RunPageBackground(
        [this, flow_id]() {
          BackgroundResult result;
          supervisor_.CancelMcpOAuth(flow_id, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_open_url = [](const QString& url) {
    QDesktopServices::openUrl(QUrl(url));
  };
  pages_view_->on_test_platform = [this](const QString& platform_id,
                                         PageJsonCallback done) {
    RunPageBackground(
        [this, platform_id]() {
          BackgroundResult result;
          supervisor_.TestMessagingPlatform(platform_id, &result.data,
                                            &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_start_telegram_onboarding = [this](const QString& bot_name,
                                                     PageJsonCallback done) {
    RunPageBackground(
        [this, bot_name]() {
          BackgroundResult result;
          supervisor_.StartTelegramOnboarding(bot_name, &result.data,
                                              &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_poll_telegram_onboarding = [this](const QString& pairing_id,
                                                    PageJsonCallback done) {
    RunPageBackground(
        [this, pairing_id]() {
          BackgroundResult result;
          supervisor_.PollTelegramOnboarding(pairing_id, &result.data,
                                             &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_apply_telegram_onboarding =
      [this](const QString& pairing_id, const QStringList& allowed_user_ids,
             PageJsonCallback done) {
        RunPageBackground(
            [this, pairing_id, allowed_user_ids]() {
              BackgroundResult result;
              supervisor_.ApplyTelegramOnboarding(pairing_id, allowed_user_ids,
                                                  &result.data, &result.error);
              return result;
            },
            std::move(done));
      };
  pages_view_->on_cancel_telegram_onboarding = [this](const QString& pairing_id,
                                                      PageJsonCallback done) {
    RunPageBackground(
        [this, pairing_id]() {
          BackgroundResult result;
          supervisor_.CancelTelegramOnboarding(pairing_id, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_start_whatsapp_onboarding =
      [this](const QString& mode, const QString& allowed_users,
             bool reset_session, PageJsonCallback done) {
        RunPageBackground(
            [this, mode, allowed_users, reset_session]() {
              BackgroundResult result;
              supervisor_.StartWhatsAppOnboarding(mode, allowed_users,
                                                  reset_session, &result.data,
                                                  &result.error);
              return result;
            },
            std::move(done));
      };
  pages_view_->on_poll_whatsapp_onboarding = [this](const QString& pairing_id,
                                                    PageJsonCallback done) {
    RunPageBackground(
        [this, pairing_id]() {
          BackgroundResult result;
          supervisor_.PollWhatsAppOnboarding(pairing_id, &result.data,
                                             &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_apply_whatsapp_onboarding =
      [this](const QString& pairing_id, const QString& mode,
             const QString& allowed_users, PageJsonCallback done) {
        RunPageBackground(
            [this, pairing_id, mode, allowed_users]() {
              BackgroundResult result;
              supervisor_.ApplyWhatsAppOnboarding(
                  pairing_id, mode, allowed_users, &result.data, &result.error);
              return result;
            },
            std::move(done));
      };
  pages_view_->on_cancel_whatsapp_onboarding = [this](const QString& pairing_id,
                                                      PageJsonCallback done) {
    RunPageBackground(
        [this, pairing_id]() {
          BackgroundResult result;
          supervisor_.CancelWhatsAppOnboarding(pairing_id, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_get_toolset_config = [this](const QString& name,
                                              PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          supervisor_.GetToolsetConfig(name, &result.data, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_save_toolset_config =
      [this](const QString& name, const QString& provider,
             const QJsonObject& env, PageJsonCallback done) {
        RunPageBackground(
            [this, name, provider, env]() {
              BackgroundResult result;
              supervisor_.SaveToolsetConfig(name, provider, env, &result.error);
              return result;
            },
            std::move(done));
      };
  pages_view_->on_get_toolset_models = [this](const QString& name,
                                              const QString& provider,
                                              PageJsonCallback done) {
    supervisor_.GetToolsetModels(supervisor_.active_index(), name, provider,
                                 std::move(done));
  };
  pages_view_->on_save_toolset_model =
      [this](const QString& name, const QString& model, const QString& provider,
             PageJsonCallback done) {
        supervisor_.SelectToolsetModel(supervisor_.active_index(), name, model,
                                       provider, std::move(done));
      };
  pages_view_->on_steer_subagent =
      [this](const QString& session_id, const QString& subagent_id,
             const QString& text, PageJsonCallback done) {
        supervisor_.SteerSubagent(supervisor_.active_index(), session_id,
                                  subagent_id, text, std::move(done));
      };
  pages_view_->on_interrupt_subagent = [this](const QString& session_id,
                                              const QString& subagent_id,
                                              PageJsonCallback done) {
    supervisor_.InterruptSubagent(supervisor_.active_index(), session_id,
                                  subagent_id, std::move(done));
  };
  pages_view_->on_save_plugin_settings = [this](const QString& key,
                                                const QJsonObject& values,
                                                PageJsonCallback done) {
    QJsonObject params{{QStringLiteral("action"), QStringLiteral("settings")},
                       {QStringLiteral("key"), key},
                       {QStringLiteral("values"), values}};
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (snapshot && !snapshot->name.isEmpty()) {
      params.insert(QStringLiteral("profile"), snapshot->name);
    }
    supervisor_.RequestRpc(supervisor_.active_index(),
                           QStringLiteral("plugins.manage"), params,
                           std::move(done));
  };
  pages_view_->on_get_skill_content = [this](const QString& name,
                                             PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          QString content;
          QString path;
          if (!supervisor_.GetSkillContent(name, &content, &path,
                                           &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("content"), content);
          result.data.insert(QStringLiteral("path"), path);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_list_official_skills = [this](PageJsonCallback done) {
    RunPageBackground(
        [this]() {
          BackgroundResult result;
          QJsonArray skills;
          if (!supervisor_.ListOfficialSkills(&skills, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("skills"), skills);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_list_skill_hub_sources = [this](PageJsonCallback done) {
    supervisor_.ListSkillHubSources(supervisor_.active_index(),
                                    std::move(done));
  };
  pages_view_->on_search_skill_hub = [this](const QString& query,
                                            const QString& source, int limit,
                                            PageJsonCallback done) {
    supervisor_.SearchSkillHub(supervisor_.active_index(), query, source, limit,
                               std::move(done));
  };
  pages_view_->on_preview_skill_hub = [this](const QString& identifier,
                                             PageJsonCallback done) {
    supervisor_.PreviewSkillHub(supervisor_.active_index(), identifier,
                                std::move(done));
  };
  pages_view_->on_scan_skill_hub = [this](const QString& identifier,
                                          PageJsonCallback done) {
    supervisor_.ScanSkillHub(supervisor_.active_index(), identifier,
                             std::move(done));
  };
  pages_view_->on_export_session = [this](const QString& session_id,
                                          const QString& local_path,
                                          PageJsonCallback done) {
    RunPageBackground(
        [this, session_id, local_path]() {
          BackgroundResult result;
          supervisor_.ExportSession(session_id, local_path, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_import_sessions = [this](const QString& local_path,
                                           PageJsonCallback done) {
    RunPageBackground(
        [this, local_path]() {
          BackgroundResult result;
          int imported = 0;
          if (!supervisor_.ImportSessions(local_path, &imported,
                                          &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("imported"), imported);
          return result;
        },
        [this, done = std::move(done)](const QJsonObject& data,
                                       const QString& error) {
          if (error.isEmpty()) {
            Refresh();
          }
          done(data, error);
        });
  };
  pages_view_->on_add_profile = [this](const QString& name,
                                       PageJsonCallback done) {
    // The profile gateway call runs on a worker; rebuilding the warm worker
    // pool has to happen back on the UI thread.
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          supervisor_.CreateProfile(name, &result.error, false);
          return result;
        },
        [this, name, done = std::move(done)](const QJsonObject& data,
                                             const QString& error) {
          if (error.isEmpty()) {
            supervisor_.ReloadProfiles();
            drafts_.assign(
                static_cast<std::size_t>(supervisor_.profile_count()),
                QString());
            ActivateProfileByName(name);
            Refresh();
          }
          done(data, error);
        });
  };
  pages_view_->on_remove_profile = [this](const QString& name,
                                          PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          supervisor_.DeleteProfile(name, &result.error, false);
          return result;
        },
        [this, done = std::move(done)](const QJsonObject& data,
                                       const QString& error) {
          if (error.isEmpty()) {
            supervisor_.ReloadProfiles();
            drafts_.assign(
                static_cast<std::size_t>(supervisor_.profile_count()),
                QString());
            Refresh();
          }
          done(data, error);
        });
  };
  pages_view_->on_activate_profile = [this](const QString& name) {
    ActivateProfileByName(name);
  };
  pages_view_->on_get_profile_soul = [this](const QString& name,
                                            PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          QString content;
          if (!supervisor_.GetProfileSoul(name, &content, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("content"), content);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_set_profile_soul = [this](const QString& name,
                                            const QString& content,
                                            PageJsonCallback done) {
    RunPageBackground(
        [this, name, content]() {
          BackgroundResult result;
          supervisor_.SaveProfileSoul(name, content, &result.error);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_export_profile = [this](const QString& name,
                                          PageJsonCallback done) {
    RunPageBackground(
        [this, name]() {
          BackgroundResult result;
          QString archive;
          if (!supervisor_.ExportProfile(name, &archive, &result.error)) {
            return result;
          }
          result.data.insert(QStringLiteral("archive"), archive);
          return result;
        },
        std::move(done));
  };
  pages_view_->on_import_profile = [this](const QString& archive,
                                          PageJsonCallback done) {
    // Same worker-pool reload boundary as profile creation.
    RunPageBackground(
        [this, archive]() {
          BackgroundResult result;
          supervisor_.ImportProfile(archive, &result.error, false);
          return result;
        },
        [this, done = std::move(done)](const QJsonObject& data,
                                       const QString& error) {
          if (error.isEmpty()) {
            supervisor_.ReloadProfiles();
            drafts_.assign(
                static_cast<std::size_t>(supervisor_.profile_count()),
                QString());
            Refresh();
          }
          done(data, error);
        });
  };
  pages_view_->on_select_session = [this](const QString& session_id) {
    supervisor_.SelectSession(supervisor_.active_index(), session_id);
    ClearPinnedContext();
    pages_view_->hide();
    sidebar_->SetActivePage(QString());
  };
  pages_view_->get_pref = [](const QString& key) { return NativePref(key); };
  pages_view_->set_pref = [](const QString& key, const QString& value) {
    SetNativePref(key, value);
  };
  RegisterShortcut(QStringLiteral("settings"),
                   QKeySequence(QStringLiteral("Ctrl+,")),
                   [this]() { ShowSettings(); });

  // Keybinds, font scale and theme are window preferences, not gateway
  // config: apply what the last session stored before the first paint.
  settings_view_->ApplyStoredPreferences();
  const QString stored_preset =
      NativePref(QStringLiteral("native.theme.preset"));
  if (!stored_preset.isEmpty() &&
      !ThemePresetById(stored_preset).id.isEmpty()) {
    SetThemePreset(stored_preset);
  } else if (NativePref(QStringLiteral("native.theme")) ==
             QStringLiteral("dark")) {
    SetThemeMode(ThemeMode::kDark);
  }

  title_bar_->on_close = [this]() { close(); };
  title_bar_->on_minimize = [this]() { showMinimized(); };
  title_bar_->on_maximize = [this]() {
    if (isMaximized()) {
      showNormal();
    } else {
      showMaximized();
    }
  };
  // The titlebar magnifier is global search: the command palette searches
  // sessions, settings pages and workspace pages, so it is where a search
  // affordance belongs (focusing an already-focused composer looked inert).
  title_bar_->on_search = [this]() { ShowCommandPalette(); };
  title_bar_->on_settings = [this]() { ShowSettings(); };

  sidebar_->on_collapsed_changed = [this](bool collapsed) {
    SetSidebarCollapsed(collapsed);
  };
  sidebar_->on_profile_selected = [this](int index) { ActivateProfile(index); };
  sidebar_->on_add_profile = [this]() { ShowAddProfile(); };
  sidebar_->on_nav = [this](const QString& page) { ShowWorkspacePage(page); };
  sidebar_->on_settings = [this]() { ShowSettings(); };
  sidebar_->completion_sound_on = [this]() {
    return completion_sound_enabled_;
  };
  sidebar_->completion_sound_set = [this](bool on) {
    completion_sound_enabled_ = on;
    SetNativePref(QStringLiteral("completion_sound"),
                  on ? QStringLiteral("1") : QStringLiteral("0"));
  };
  sidebar_->completion_flash_on = [this]() {
    return completion_flash_enabled_;
  };
  sidebar_->completion_flash_set = [this](bool on) {
    completion_flash_enabled_ = on;
    SetNativePref(QStringLiteral("completion_flash"),
                  on ? QStringLiteral("1") : QStringLiteral("0"));
  };
  sidebar_->on_session_action = [this](const QString& session_id,
                                       const QString& action) {
    HandleSessionAction(session_id, action);
  };
  sidebar_->on_new_chat = [this]() { NewSession(); };
  sidebar_->on_session_selected = [this](const QString& session_id) {
    supervisor_.SelectSession(supervisor_.active_index(), session_id);
    ClearPinnedContext();
  };
  sidebar_->on_project_selected = [this](const QString& project_id) {
    SetProjectScope(project_id);
  };
  sidebar_->on_project_add = [this]() { CreateProject(); };
  sidebar_->on_project_action = [this](const QString& project_id,
                                       const QString& action) {
    HandleProjectAction(project_id, action);
  };

  connect(composer_, &Composer::Submitted, this,
          [this](const QString& text) { SubmitText(text); });
  connect(composer_, &Composer::VoiceToggleRequested, this,
          [this]() { ToggleLiveVoice(); });
  connect(composer_, &Composer::SteerRequested, this,
          [this](const QString& text) {
            const int active = supervisor_.active_index();
            composer_->Clear();
            status_bar_->ShowTransient(QStringLiteral("Steering…"));
            supervisor_.Steer(
                active, text, [this, text](bool ok, const QString& message) {
                  if (ok) {
                    RecordSteeredPrompt(text);
                    return;
                  }
                  if (composer_->Text().trimmed().isEmpty()) {
                    composer_->SetText(text);
                    composer_->setFocus();
                  }
                  status_bar_->ShowTransient(
                      QStringLiteral("Steer failed: %1").arg(message));
                });
          });
  connect(composer_, &Composer::QueuedReviewRequested, this,
          [this]() { ShowQueuedPromptsDialog(); });
  connect(composer_, &Composer::QueueRequested, this,
          [this](const QString& text) {
            QueuePromptText(text);
            composer_->Clear();
          });
  connect(composer_, &Composer::AttachRequested, this, [this]() {
    const QString path =
        QFileDialog::getOpenFileName(this, QStringLiteral("Attach a file"));
    if (!path.isEmpty()) {
      supervisor_.AttachFile(supervisor_.active_index(), path);
    }
  });
  connect(composer_, &Composer::FilesDropped, this,
          [this](const QStringList& paths) {
            for (const QString& path : paths) {
              supervisor_.AttachFile(supervisor_.active_index(), path);
            }
          });
  composer_->SetPathLister(
      [this](const QString& path, Composer::PathListCallback done) {
        RunPageBackground(
            [this, path]() {
              BackgroundResult result;
              QJsonArray entries;
              if (!supervisor_.ListDirectory(path, &entries, &result.error)) {
                return result;
              }
              result.data.insert(QStringLiteral("entries"), entries);
              return result;
            },
            [done = std::move(done)](const QJsonObject& data,
                                     const QString& error) {
              done(data.value(QStringLiteral("entries")).toArray(), error);
            });
      });
}

void MainWindow::EditMessage(const ChatMessage& message, const QString& text) {
  if (message.row_id <= 0) {
    return;
  }
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot && snapshot->running) {
    supervisor_.Note(supervisor_.active_index(),
                     QStringLiteral("Stop the running turn before editing"));
    return;
  }

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("chatEditDialog"));
  dialog.setWindowTitle(QStringLiteral("Edit and resend"));
  auto* layout = new QVBoxLayout(&dialog);
  auto* editor = new QPlainTextEdit(&dialog);
  editor->setObjectName(QStringLiteral("chatEditMessage"));
  editor->setPlainText(text);
  editor->setMinimumHeight(160);
  layout->addWidget(editor, 1);
  auto* buttons = new QWidget(&dialog);
  auto* button_row = new QHBoxLayout(buttons);
  button_row->setContentsMargins(0, 0, 0, 0);
  auto* send = new QPushButton(QStringLiteral("Send"), buttons);
  send->setObjectName(QStringLiteral("chatEditSend"));
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), buttons);
  QObject::connect(send, &QPushButton::clicked, &dialog,
                   [&dialog]() { dialog.accept(); });
  QObject::connect(cancel, &QPushButton::clicked, &dialog,
                   [&dialog]() { dialog.reject(); });
  button_row->addStretch(1);
  button_row->addWidget(cancel);
  button_row->addWidget(send);
  layout->addWidget(buttons);
  dialog.resize(620, 320);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  SendEditedPrompt(editor->toPlainText(), message.row_id);
}

void MainWindow::RegenerateMessage(const ChatMessage& message) {
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (!snapshot) {
    return;
  }
  if (snapshot->running) {
    supervisor_.Note(supervisor_.active_index(),
                     QStringLiteral("Stop the running turn before retrying"));
    return;
  }
  int target = -1;
  for (int index = 0; index < static_cast<int>(snapshot->messages.size());
       ++index) {
    if (snapshot->messages[static_cast<std::size_t>(index)].row_id ==
        message.row_id) {
      target = index;
      break;
    }
  }
  if (target < 0) {
    return;
  }
  for (int index = target - 1; index >= 0; --index) {
    const ChatMessage& candidate =
        snapshot->messages[static_cast<std::size_t>(index)];
    if (candidate.role != MessageRole::kUser || candidate.row_id <= 0) {
      continue;
    }
    QString text;
    for (const MessageBlock& block : candidate.blocks) {
      if (block.kind == BlockKind::kTool) {
        continue;
      }
      if (!text.isEmpty()) {
        text += QLatin1Char('\n');
      }
      text += block.text;
    }
    SendEditedPrompt(text.isEmpty() ? candidate.raw_text : text,
                     candidate.row_id);
    return;
  }
  supervisor_.Note(supervisor_.active_index(),
                   QStringLiteral("No user turn to regenerate"));
}

void MainWindow::ApplyAudioOutputDevice(const QString& device_id) {
#ifdef HERMES_HAS_MULTIMEDIA
  if (read_aloud_output_ == nullptr) {
    return;
  }
  QAudioDevice selected = QMediaDevices::defaultAudioOutput();
  if (!device_id.isEmpty()) {
    const QByteArray id = device_id.toUtf8();
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
      if (!device.isNull() && device.id() == id) {
        selected = device;
        break;
      }
    }
  }
  read_aloud_output_->setDevice(selected);
#else
  Q_UNUSED(device_id);
#endif
}

void MainWindow::ReadAloud(const ChatMessage&, const QString& text) {
  if (text.trimmed().isEmpty()) {
    status_bar_->ShowTransient(
        QStringLiteral("There is nothing to read aloud."));
    return;
  }
#ifdef HERMES_HAS_MULTIMEDIA
  status_bar_->ShowTransient(QStringLiteral("Preparing speech…"));
  const int active = supervisor_.active_index();
  supervisor_.SpeakText(
      active, text, [this](const QJsonObject& result, const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Read aloud failed: %1").arg(error));
          return;
        }
        QByteArray bytes;
        QString mime;
        QString decode_error;
        if (!DecodeAudioDataUrl(
                result.value(QStringLiteral("data_url")).toString(), &bytes,
                &mime, &decode_error)) {
          status_bar_->ShowTransient(
              QStringLiteral("Read aloud failed: %1").arg(decode_error));
          return;
        }
        read_aloud_player_->stop();
        read_aloud_file_ = std::make_unique<QTemporaryFile>(
            QDir::tempPath() + QStringLiteral("/hermes-read-aloud-XXXXXX") +
            AudioFileSuffixForMime(mime));
        read_aloud_file_->setAutoRemove(true);
        if (!read_aloud_file_->open() ||
            read_aloud_file_->write(bytes) != bytes.size()) {
          read_aloud_file_.reset();
          status_bar_->ShowTransient(QStringLiteral(
              "Read aloud failed: could not stage the audio file."));
          return;
        }
        read_aloud_file_->flush();
        read_aloud_player_->setSource(
            QUrl::fromLocalFile(read_aloud_file_->fileName()));
        read_aloud_player_->play();
        status_bar_->ShowTransient(QStringLiteral("Reading aloud…"));
      });
#else
  status_bar_->ShowTransient(
      QStringLiteral("Read aloud needs Qt Multimedia in this build."));
#endif
}

void MainWindow::PinMessage(const ChatMessage&, const QString& text) {
  QString body = text.trimmed();
  if (body.isEmpty()) {
    status_bar_->ShowTransient(QStringLiteral("There is nothing to pin."));
    return;
  }
  // Keep the prefix bounded: the desktop pins a transcript excerpt, not an
  // unbounded document, and every future prompt carries this text.
  constexpr int kPinnedBodyLimit = 4000;
  if (body.size() > kPinnedBodyLimit) {
    body = body.left(kPinnedBodyLimit) + QStringLiteral("\n…");
  }
  for (auto it = pinned_context_.begin(); it != pinned_context_.end(); ++it) {
    if (*it == body) {
      pinned_context_.erase(it);
      composer_->SetPinnedContextCount(
          static_cast<int>(pinned_context_.size()));
      status_bar_->ShowTransient(QStringLiteral("Unpinned reply · %1 pinned")
                                     .arg(pinned_context_.size()));
      return;
    }
  }
  if (pinned_context_.size() >= 8) {
    status_bar_->ShowTransient(
        QStringLiteral("Unpin a reply before adding another (8 max)"));
    return;
  }
  pinned_context_.push_back(body);
  composer_->SetPinnedContextCount(static_cast<int>(pinned_context_.size()));
  status_bar_->ShowTransient(
      QStringLiteral("Pinned reply to context · %1 pinned")
          .arg(pinned_context_.size()));
}

void MainWindow::PinLastAssistantMessage() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot == nullptr) {
    return;
  }
  for (auto it = snapshot->messages.rbegin(); it != snapshot->messages.rend();
       ++it) {
    if (it->role != MessageRole::kAssistant) {
      continue;
    }
    const QString text = AssistantMessageText(*it);
    if (!text.isEmpty()) {
      PinMessage(*it, text);
      return;
    }
  }
  status_bar_->ShowTransient(QStringLiteral("No assistant reply to pin yet."));
}

void MainWindow::ClearPinnedContext() {
  if (pinned_context_.empty()) {
    return;
  }
  pinned_context_.clear();
  composer_->SetPinnedContextCount(0);
  status_bar_->ShowTransient(QStringLiteral("Cleared pinned context"));
}

QString MainWindow::ActiveSessionId() const {
  const auto snapshot = supervisor_.ActiveSnapshot();
  return snapshot ? snapshot->selected_session_id : QString();
}

void MainWindow::BranchFromMessage(const ChatMessage& message) {
  const auto snapshot = supervisor_.ActiveSnapshot();
  const QString session_id = ActiveSessionId();
  if (!snapshot || session_id.isEmpty() || message.row_id <= 0) {
    return;
  }
  if (snapshot->running) {
    supervisor_.Note(supervisor_.active_index(),
                     QStringLiteral("Stop the running turn before branching"));
    return;
  }
  int index = -1;
  for (int cursor = 0; cursor < static_cast<int>(snapshot->messages.size());
       ++cursor) {
    if (snapshot->messages[static_cast<std::size_t>(cursor)].row_id ==
        message.row_id) {
      index = cursor;
      break;
    }
  }
  if (index < 0) {
    return;
  }
  const int active = supervisor_.active_index();
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("count"), index + 1},
                     {QStringLiteral("idempotency_key"),
                      QUuid::createUuid().toString(QUuid::WithoutBraces)}};
  supervisor_.RequestRpc(
      active, QStringLiteral("session.branch"), params,
      [this, active, session_id](const QJsonObject& result,
                                 const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Branch failed: %1").arg(error));
          return;
        }
        QString child = result.value(QStringLiteral("session_id")).toString();
        if (child.isEmpty()) {
          child = result.value(QStringLiteral("stored_session_id")).toString();
        }
        supervisor_.SelectSession(active, child.isEmpty() ? session_id : child);
        status_bar_->ShowTransient(QStringLiteral("Branched conversation"));
      });
}

void MainWindow::RewindLastTurn(const ChatMessage& message) {
  const auto snapshot = supervisor_.ActiveSnapshot();
  const QString session_id = ActiveSessionId();
  if (!snapshot || session_id.isEmpty() || message.row_id <= 0) {
    return;
  }
  if (snapshot->running) {
    supervisor_.Note(supervisor_.active_index(),
                     QStringLiteral("Stop the running turn before rewinding"));
    return;
  }
  const int active = supervisor_.active_index();
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("intent"), QStringLiteral("undo")}};
  supervisor_.RequestRpc(
      active, QStringLiteral("session.undo"), params,
      [this, active, session_id](const QJsonObject&, const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Rewind failed: %1").arg(error));
          return;
        }
        supervisor_.SelectSession(active, session_id);
        status_bar_->ShowTransient(QStringLiteral("Rewound the last turn"));
      });
}

void MainWindow::ReactToMessage(const ChatMessage& message,
                                const QString& emoji) {
  const QString session_id = ActiveSessionId();
  if (session_id.isEmpty() || message.row_id <= 0 || emoji.isEmpty()) {
    return;
  }
  const int active = supervisor_.active_index();
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("row_id"), message.row_id},
                     {QStringLiteral("emoji"), emoji}};
  supervisor_.RequestRpc(
      active, QStringLiteral("message.react"), params,
      [this](const QJsonObject&, const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Reaction failed: %1").arg(error));
          return;
        }
        status_bar_->ShowTransient(QStringLiteral("Reaction saved"));
      });
}

void MainWindow::DeleteConversation() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  const QString session_id = ActiveSessionId();
  if (!snapshot || session_id.isEmpty()) {
    return;
  }
  if (snapshot->running) {
    supervisor_.Note(supervisor_.active_index(),
                     QStringLiteral("Stop the running turn before deleting"));
    return;
  }
  supervisor_.DeleteSession(supervisor_.active_index(), session_id);
  NewSession();
  status_bar_->ShowTransient(QStringLiteral("Conversation deleted"));
}

void MainWindow::SendEditedPrompt(const QString& text, qint64 user_row_id) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty() || user_row_id <= 0) {
    return;
  }
  std::vector<qint64> user_rows;
  if (const auto snapshot = supervisor_.ActiveSnapshot()) {
    for (const ChatMessage& message : snapshot->messages) {
      if (message.role == MessageRole::kUser && message.row_id > 0) {
        user_rows.push_back(message.row_id);
      }
    }
  }
  const PromptTruncation truncation =
      ComputePromptTruncation(user_rows, user_row_id);
  supervisor_.SendPromptAt(supervisor_.active_index(), trimmed, user_row_id,
                           truncation.confirm_empty_truncate,
                           truncation.confirm_deep_truncate);
}

QStringList MainWindow::ShortcutAudit() const {
  QStringList rows;
  QSet<QString> seen;
  for (const KeyBinding& binding : key_bindings_) {
    rows.append(QStringLiteral("%1\t%2").arg(binding.action, binding.text));
    if (seen.contains(binding.text)) {
      rows.append(QStringLiteral("DUPLICATE\t%1").arg(binding.text));
    }
    seen.insert(binding.text);
  }
  rows.sort();
  return rows;
}

void MainWindow::RegisterShortcut(const QString& action,
                                  const QKeySequence& sequence,
                                  const std::function<void()>& handler) {
  if (sequence.isEmpty()) {
    return;
  }
  const QString key = sequence.toString(QKeySequence::PortableText);
  if (shortcut_keys_.contains(key)) {
    qWarning("shortcut %s ignored: %s is already bound", qUtf8Printable(action),
             qUtf8Printable(key));
    return;
  }
  shortcut_keys_.insert(key);
  KeyBinding binding;
  binding.action = action;
  binding.sequence = sequence;
  binding.text = key;
  binding.handler = handler;
  key_bindings_.push_back(std::move(binding));
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(watched, event);
  }
  auto* key_event = static_cast<QKeyEvent*>(event);
  if (key_event->isAutoRepeat()) {
    return QWidget::eventFilter(watched, event);
  }
  if (key_event->key() == Qt::Key_Escape && session_search_ != nullptr &&
      !session_search_->Query().isEmpty()) {
    HideFindBar();
    return true;
  }
  const QString pressed = QKeySequence(key_event->keyCombination())
                              .toString(QKeySequence::PortableText);
  if (pressed.isEmpty()) {
    return QWidget::eventFilter(watched, event);
  }
  if (qEnvironmentVariableIsSet("HERMES_UI_DEBUG_KEYS")) {
    qInfo("key pressed=%s", qUtf8Printable(pressed));
  }
  for (const KeyBinding& binding : key_bindings_) {
    if (binding.text != pressed) {
      continue;
    }
    // A bare key (no modifier) must never be stolen from a text field.
    const bool has_modifier =
        binding.sequence[0].keyboardModifiers() != Qt::NoModifier;
    QWidget* focus = QApplication::focusWidget();
    const bool typing = qobject_cast<QLineEdit*>(focus) != nullptr ||
                        qobject_cast<QTextEdit*>(focus) != nullptr ||
                        qobject_cast<QPlainTextEdit*>(focus) != nullptr;
    if (typing && !has_modifier) {
      continue;
    }
    binding.handler();
    return true;
  }
  return QWidget::eventFilter(watched, event);
}

void MainWindow::SubmitText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  const QStringList attachments = composer_->AttachmentPaths();
  const bool slash_command =
      attachments.isEmpty() && trimmed.startsWith(QLatin1Char('/'));
  // A connector directory card owns plain typed text. Attachments and slash
  // commands cannot be an answer (desktop parity), so they settle the card
  // with Continue and then route normally.
  if (attachments.isEmpty() && !slash_command &&
      chat_view_->AnswerConnectionDisplay(trimmed)) {
    composer_->Clear();
    return;
  }
  if (chat_view_->HasConnectionRequest()) {
    chat_view_->ContinueConnection();
  }
  if (slash_command && RunSlashCommand(trimmed)) {
    composer_->Clear();
    return;
  }
  QStringList pinned;
  pinned.reserve(static_cast<int>(pinned_context_.size()));
  for (const QString& body : pinned_context_) {
    pinned.append(body);
  }
  const QString prompt = ComposePinnedPrompt(pinned, trimmed);
  if (attachments.isEmpty()) {
    supervisor_.SendPrompt(supervisor_.active_index(), prompt);
  } else {
    supervisor_.SendPromptWithAttachments(supervisor_.active_index(), prompt,
                                          attachments);
  }
  composer_->Clear();
  const int active = supervisor_.active_index();
  if (active >= 0 && active < static_cast<int>(drafts_.size())) {
    drafts_[static_cast<std::size_t>(active)].clear();
  }
}

bool MainWindow::RunSlashCommand(const QString& text) {
  const int space = text.indexOf(QLatin1Char(' '));
  const QString name =
      (space < 0 ? text : text.left(space)).trimmed().toLower();
  const QString args = space < 0 ? QString() : text.mid(space + 1).trimmed();
  const int active = supervisor_.active_index();
  const auto active_snapshot = supervisor_.ActiveSnapshot();
  const bool running = active_snapshot && active_snapshot->running;

  // Commands the shared gateway refuses as slash.exec run on the session's own
  // verbs instead (the desktop does the same): /steer -> session.steer,
  // /queue -> the composer's next-turn queue, /retry and /undo -> re-prompt.
  if (name == QStringLiteral("/steer") || name == QStringLiteral("/s")) {
    if (args.isEmpty()) {
      supervisor_.Note(active, QStringLiteral("usage: /steer <text>"));
      status_bar_->ShowTransient(QStringLiteral("usage: /steer <text>"));
      return true;
    }
    // The worker owns the decision: steer when a turn holds the session,
    // otherwise send the text as the next prompt. The snapshot's `running`
    // flag lags the gateway by design, so it must not gate this.
    supervisor_.Steer(active, args);
    RecordSteeredPrompt(args);
    status_bar_->ShowTransient(QStringLiteral("Steering…"));
    return true;
  }
  if (name == QStringLiteral("/queue") || name == QStringLiteral("/q")) {
    const int pending =
        active_snapshot ? PendingQueueCount(*active_snapshot) : 0;
    if (args.isEmpty()) {
      supervisor_.Note(active,
                       QStringLiteral("%1 prompt(s) queued").arg(pending));
      status_bar_->ShowTransient(
          QStringLiteral("%1 prompt(s) queued").arg(pending));
      return true;
    }
    if (running) {
      QueuePromptText(args);
    } else {
      supervisor_.SendPrompt(active, args);
    }
    return true;
  }
  if (name == QStringLiteral("/retry")) {
    QString last_user;
    if (active_snapshot) {
      for (const ChatMessage& message : active_snapshot->messages) {
        if (message.role == MessageRole::kUser && !message.blocks.empty()) {
          last_user = message.blocks.front().text;
        }
      }
    }
    if (last_user.isEmpty()) {
      supervisor_.Note(active, QStringLiteral("Nothing to retry"));
      status_bar_->ShowTransient(QStringLiteral("Nothing to retry"));
    } else if (running) {
      supervisor_.Note(
          active,
          QStringLiteral("The turn is still running — use /steer or /queue"));
      status_bar_->ShowTransient(
          QStringLiteral("Turn is still running — use /steer or /queue"));
    } else {
      supervisor_.SendPrompt(active, last_user);
    }
    return true;
  }
  if (name == QStringLiteral("/undo")) {
    QStringList prompts;
    if (active_snapshot) {
      for (const ChatMessage& message : active_snapshot->messages) {
        if (message.role == MessageRole::kUser && !message.blocks.empty()) {
          prompts.append(message.blocks.front().text);
        }
      }
    }
    const int turns = std::max(1, args.toInt());
    const int target = prompts.size() - 1 - turns;
    if (target < 0) {
      supervisor_.Note(active, QStringLiteral("Not enough turns to undo"));
      status_bar_->ShowTransient(QStringLiteral("Not enough turns to undo"));
    } else if (running) {
      supervisor_.Note(active, QStringLiteral("Stop the turn before undoing"));
      status_bar_->ShowTransient(
          QStringLiteral("Stop the turn before undoing"));
    } else {
      supervisor_.SendPrompt(active, prompts.at(target));
      supervisor_.Note(active,
                       QStringLiteral("Re-running the prompt %1 turn(s) back "
                                      "(history is not truncated)")
                           .arg(turns));
      status_bar_->ShowTransient(
          QStringLiteral("Re-running the prompt %1 turn(s) back (history is "
                         "not truncated)")
              .arg(turns));
    }
    return true;
  }

  if (name == QStringLiteral("/handoff")) {
    const QString platform = NormalizeHandoffPlatform(args);
    if (platform.isEmpty()) {
      supervisor_.Note(active, QStringLiteral("usage: /handoff <platform>"));
      status_bar_->ShowTransient(QStringLiteral("usage: /handoff <platform>"));
    } else if (running) {
      supervisor_.Note(
          active, QStringLiteral("Stop the running turn before handing off"));
      status_bar_->ShowTransient(
          QStringLiteral("Stop the running turn before handing off"));
    } else {
      RequestHandoff(platform);
    }
    return true;
  }

  if (name == QStringLiteral("/browser")) {
    ShowBrowserAutomation();
    return true;
  }

  const SlashCommand* command = FindSlashCommand(name);
  if (command == nullptr) {
    // Not one of the local fast paths: if the text is shaped like a command
    // ("/steer ...", "/status"), the gateway registry owns it. Paths such as
    // "/tmp/x is broken" stay plain prompts.
    if (QRegularExpression(QStringLiteral("^/[A-Za-z0-9_-]+$"))
            .match(name)
            .hasMatch()) {
      supervisor_.ExecuteSlash(supervisor_.active_index(), text);
      return true;
    }
    return false;
  }
  const QString canonical = command->name;
  const int index = supervisor_.active_index();
  if (canonical == QStringLiteral("/new")) {
    NewSession();
  } else if (canonical == QStringLiteral("/stop")) {
    supervisor_.Interrupt(index);
  } else if (canonical == QStringLiteral("/reasoning")) {
    if (args.isEmpty()) {
      const auto snapshot = supervisor_.ActiveSnapshot();
      const QStringList levels = {QStringLiteral("low"),
                                  QStringLiteral("medium"),
                                  QStringLiteral("high")};
      const int current =
          snapshot ? levels.indexOf(snapshot->reasoning_effort) : -1;
      supervisor_.SetReasoning(index, levels.at((current + 1) % levels.size()));
    } else {
      supervisor_.SetReasoning(index, args);
    }
  } else if (canonical == QStringLiteral("/model")) {
    ShowSettingsPage(QStringLiteral("model"));
  } else if (canonical == QStringLiteral("/profile")) {
    if (args.isEmpty()) {
      ShowWorkspacePage(QStringLiteral("profiles"));
    } else {
      ActivateProfileByName(args);
    }
  } else if (canonical == QStringLiteral("/title")) {
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (!args.isEmpty() && snapshot &&
        !snapshot->selected_session_id.isEmpty()) {
      supervisor_.PatchSession(index, snapshot->selected_session_id,
                               QJsonObject{{QStringLiteral("title"), args}});
    }
  } else if (canonical == QStringLiteral("/attach")) {
    QString path = args;
    if (path.isEmpty()) {
      path =
          QFileDialog::getOpenFileName(this, QStringLiteral("Attach a file"));
    }
    if (!path.isEmpty()) {
      supervisor_.AttachFile(index, path);
    }
  } else if (canonical == QStringLiteral("/settings")) {
    ShowSettings();
  } else if (canonical == QStringLiteral("/theme")) {
    SetThemeMode(mode_ == ThemeMode::kLight ? ThemeMode::kDark
                                            : ThemeMode::kLight);
  } else if (canonical == QStringLiteral("/search")) {
    ShowFindBar();
  } else if (canonical == QStringLiteral("/help")) {
    ShowSlashHelp();
  } else {
    // Every other registry command (including plugin, skill and quick
    // commands) runs on the gateway: /steer, /queue, /status, /context,
    // /usage, /compress, /undo ... the gateway owns the semantics.
    supervisor_.ExecuteSlash(index, text);
  }
  return true;
}

void MainWindow::CycleSession(int delta) {
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (!snapshot || snapshot->sessions.empty()) {
    return;
  }
  std::vector<QString> ids;
  for (const SessionSummary& session : snapshot->sessions) {
    if (!session.archived) {
      ids.push_back(session.id);
    }
  }
  if (ids.empty()) {
    return;
  }
  auto current =
      std::find(ids.begin(), ids.end(), snapshot->selected_session_id);
  int index =
      current == ids.end() ? 0 : static_cast<int>(current - ids.begin());
  index = (index + delta + static_cast<int>(ids.size())) %
          static_cast<int>(ids.size());
  supervisor_.SelectSession(supervisor_.active_index(),
                            ids[static_cast<std::size_t>(index)]);
  pages_view_->hide();
  sidebar_->SetActivePage(QString());
}

void MainWindow::ShowSlashHelp() {
  std::vector<PaletteCommand> commands;
  for (const SlashCommand& command : SlashCommandCatalog()) {
    const QString name = command.name;
    commands.push_back(
        {name + (command.argument_hint.isEmpty()
                     ? QString()
                     : QStringLiteral(" ") + command.argument_hint),
         command.description, QStringLiteral("Slash commands"), [this, name]() {
           composer_->SetText(name + QLatin1Char(' '));
           composer_->FocusEditor();
         }});
  }
  command_palette_->SetCommands(std::move(commands));
  command_palette_->Open();
}

QString MainWindow::NewSessionCwd() const {
  return ProjectWorkingDirectory(project_state_, project_scope_);
}

void MainWindow::NewSession() {
  const QString project_id =
      project_scope_ == AllProjectsId() ? QString() : project_scope_;
  supervisor_.NewSession(supervisor_.active_index(), NewSessionCwd(),
                         project_id);
  ClearPinnedContext();
}

void MainWindow::SetAutoApprove(bool auto_approve) {
  auto_approve_ = auto_approve;
}

void MainWindow::SetAutoClarify(const QString& answer) {
  auto_clarify_ = answer;
}

void MainWindow::ShowCommandPalette() {
  std::vector<PaletteCommand> commands;
  commands.push_back({QStringLiteral("New chat"),
                      QStringLiteral("Start a fresh session"),
                      QStringLiteral("Actions"), [this]() { NewSession(); }});
  commands.push_back({QStringLiteral("New project"),
                      QStringLiteral("Group sessions under a workspace"),
                      QStringLiteral("Actions"),
                      [this]() { CreateProject(); }});
  if (!dismissed_auto_project_ids_.isEmpty()) {
    commands.push_back(
        {QStringLiteral("Restore hidden projects"),
         QStringLiteral("Show auto workspaces hidden from the sidebar"),
         QStringLiteral("Actions"), [this]() { RestoreHiddenProjects(); }});
  }
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot && snapshot->running) {
    commands.push_back({QStringLiteral("Stop current turn"),
                        QStringLiteral("Interrupt the running response"),
                        QStringLiteral("Actions"), [this]() {
                          supervisor_.Interrupt(supervisor_.active_index());
                        }});
  }
  commands.push_back({QStringLiteral("New Kanban task"),
                      QStringLiteral("Add a card to the dispatcher queue"),
                      QStringLiteral("Actions"), [this]() {
                        ShowWorkspacePage(QStringLiteral("kanban"));
                        pages_view_->Kanban()->NewTask();
                      }});
  commands.push_back({QStringLiteral("Preferences"),
                      QStringLiteral("Appearance, model and reasoning"),
                      QStringLiteral("Actions"), [this]() { ShowSettings(); }});
  commands.push_back({QStringLiteral("Toggle light/dark theme"),
                      QStringLiteral("Switch appearance"),
                      QStringLiteral("Actions"), [this]() {
                        SetThemeMode(mode_ == ThemeMode::kDark
                                         ? ThemeMode::kLight
                                         : ThemeMode::kDark);
                      }});
  commands.push_back({QStringLiteral("Toggle right rail"),
                      QStringLiteral("Context, agents and session outputs"),
                      QStringLiteral("Actions"), [this]() {
                        right_rail_->setVisible(!right_rail_->isVisible());
                      }});
  commands.push_back({QStringLiteral("Quick entry"),
                      QStringLiteral("Mini composer in a new chat"),
                      QStringLiteral("Actions"),
                      [this]() { ShowQuickEntry(); }});
  commands.push_back(
      {QStringLiteral("Open chat in a new window"),
       QStringLiteral("Detach the transcript and composer from the main shell"),
       QStringLiteral("Actions"), [this]() { ShowDetachedChat(); }});
  commands.push_back(
      {QStringLiteral("Toggle desktop pet"),
       QStringLiteral("Pop the mascot out as an always-on-top overlay"),
       QStringLiteral("Actions"), [this]() { TogglePetOverlay(); }});
  commands.push_back(
      {QStringLiteral("Dictate a recording"),
       QStringLiteral("Transcribe WAV/MP3/M4A audio into the composer"),
       QStringLiteral("Actions"), [this]() { Dictate(); }});
  commands.push_back(
      {QStringLiteral("Browser automation"),
       QStringLiteral("Connect to CDP, capture screenshots, click or drag"),
       QStringLiteral("Actions"), [this]() { ShowBrowserAutomation(); }});
  commands.push_back(
      {QStringLiteral("Live voice input"),
       QStringLiteral("Start or stop gateway microphone capture"),
       QStringLiteral("Actions"), [this]() { ToggleLiveVoice(); }});
  commands.push_back(
      {QStringLiteral("Pin last reply to context"),
       QStringLiteral("Prefix the latest assistant reply to the next prompt"),
       QStringLiteral("Actions"), [this]() { PinLastAssistantMessage(); }});
  if (!pinned_context_.empty()) {
    commands.push_back(
        {QStringLiteral("Clear pinned context"),
         QStringLiteral("%1 pinned replies").arg(pinned_context_.size()),
         QStringLiteral("Actions"), [this]() { ClearPinnedContext(); }});
  }
  commands.push_back(
      {QStringLiteral("Focus session search"),
       QStringLiteral("Search every session from the chat header"),
       QStringLiteral("Actions"), [this]() { ShowFindBar(); }});

  // Workspace pages and Settings pages are searchable destinations too: the
  // titlebar magnifier opens this palette, so it must find every page.
  const struct {
    const char* id;
    const char* title;
  } kWorkspacePages[] = {
      {"capabilities", "Capabilities"},
      {"messaging", "Messaging"},
      {"artifacts", "Artifacts"},
      {"kanban", "Kanban"},
      {"cron", "Scheduled tasks"},
      {"profiles", "Bots"},
      {"agents", "Agents"},
      {"starmap", "Star map"},
  };
  for (const auto& page : kWorkspacePages) {
    const QString id = QLatin1String(page.id);
    commands.push_back(
        {QString::fromLatin1(page.title), QStringLiteral("Workspace page"),
         QStringLiteral("Pages"), [this, id]() { ShowWorkspacePage(id); }});
  }
  const struct {
    const char* id;
    const char* title;
  } kSettingsPages[] = {
      {"model", "Model settings"},
      {"chat", "Chat settings"},
      {"appearance", "Appearance settings"},
      {"workspace", "Workspace settings"},
      {"safety", "Safety settings"},
      {"browser", "Browser settings"},
      {"memory", "Memory and context"},
      {"voice", "Voice settings"},
      {"advanced", "Advanced settings"},
      {"providers", "Providers"},
      {"keys", "API keys"},
      {"vault", "Credential vault"},
      {"gateway", "Gateway"},
      {"notifications", "Notifications"},
      {"keybinds", "Keybinds"},
      {"sessions", "Session settings"},
      {"plugins", "Plugins"},
      {"billing", "Billing"},
      {"about", "About and updates"},
  };
  for (const auto& page : kSettingsPages) {
    const QString id = QLatin1String(page.id);
    commands.push_back(
        {QString::fromLatin1(page.title), QStringLiteral("Settings page"),
         QStringLiteral("Pages"), [this, id]() { ShowSettingsPage(id); }});
  }

  for (int index = 0; index < supervisor_.profile_count(); ++index) {
    commands.push_back(
        {QStringLiteral("Switch to %1").arg(supervisor_.ProfileLabel(index)),
         QStringLiteral("Warm profile"), QStringLiteral("Profiles"),
         [this, index]() { ActivateProfile(index); }});
  }

  if (snapshot) {
    const int profile_index = supervisor_.active_index();
    int shown = 0;
    for (const SessionSummary& session : snapshot->sessions) {
      if (shown++ >= 40) {
        break;
      }
      const QString id = session.id;
      commands.push_back(
          {session.title.isEmpty() ? QStringLiteral("Untitled chat")
                                   : session.title,
           session.model.isEmpty() ? QStringLiteral("Session") : session.model,
           QStringLiteral("Sessions"), [this, profile_index, id]() {
             supervisor_.SelectSession(profile_index, id);
             QTimer::singleShot(0, this, [this]() { Refresh(); });
           }});
    }
  }

  command_palette_->SetCommands(std::move(commands));
  command_palette_->Open();
}

void MainWindow::ShowPreferences() { ShowSettings(); }

void MainWindow::ShowSettingsPage(const QString& page) {
  ShowSettings();
  settings_view_->OpenPage(page);
}

void MainWindow::ShowWorkspacePage(const QString& page) {
  if (page == QStringLiteral("artifacts")) {
    SyncArtifactsRoot();
  }
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot) {
    pages_view_->SetSnapshot(*snapshot);
  }
  pages_view_->SetTheme(theme_);
  pages_view_->OpenPage(page);
  pages_view_->show();
  chat_view_->hide();
  if (composer_host_ != nullptr) {
    composer_host_->hide();
  }
  settings_view_->hide();
  sidebar_->SetActivePage(page);
  if (page != QStringLiteral("kanban")) {
    supervisor_.FetchPage(supervisor_.active_index(), page);
  }
}

void MainWindow::ShowFindBar() {
  if (session_search_ == nullptr) {
    return;
  }
  session_search_->FocusSearch();
}

void MainWindow::HideFindBar() {
  if (session_search_ == nullptr) {
    return;
  }
  session_search_->ClearSearch();
  chat_view_->setFocus(Qt::OtherFocusReason);
}

void MainWindow::UpdateFindBar() {
  // The session search bar owns its own status/result row; there is no
  // transcript find overlay left to reconcile here.
}

void MainWindow::ShowQuickEntry() {
  if (supervisor_.profile_count() == 0) {
    return;
  }
  if (quick_entry_ == nullptr) {
    quick_entry_ = new QuickEntryDialog(this);
    quick_entry_->SetSubmitHandler(
        [this](const QString& text) { SubmitQuickEntry(text); });
  }
  quick_entry_->SetSessionLabel(
      QStringLiteral("%1 · quick entry")
          .arg(supervisor_.ProfileLabel(supervisor_.active_index())));
  quick_entry_->show();
  quick_entry_->raise();
  quick_entry_->activateWindow();
  quick_entry_->FocusEditor();
}

void MainWindow::RequestHandoff(const QString& platform) {
  const QString normalized = NormalizeHandoffPlatform(platform);
  const QString session_id = ActiveSessionId();
  if (normalized.isEmpty() || session_id.isEmpty()) {
    status_bar_->ShowTransient(QStringLiteral("usage: /handoff <platform>"));
    return;
  }
  const int active = supervisor_.active_index();
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("platform"), normalized}};
  status_bar_->ShowTransient(
      HandoffStatusText(QStringLiteral("pending"), normalized, QString()));
  supervisor_.RequestRpc(
      active, QStringLiteral("handoff.request"), params,
      [this, active, session_id, normalized](const QJsonObject& result,
                                             const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Handoff failed: %1").arg(error));
          supervisor_.Note(active,
                           QStringLiteral("Handoff failed: %1").arg(error));
          return;
        }
        const QString home =
            result.value(QStringLiteral("home_name")).toString();
        handoff_session_ = session_id;
        handoff_platform_ = normalized;
        handoff_poll_ticks_ = 0;
        supervisor_.Note(
            active, home.isEmpty()
                        ? QStringLiteral("Handoff to %1 queued").arg(normalized)
                        : QStringLiteral("Handoff to %1 queued (%2)")
                              .arg(normalized, home));
        status_bar_->ShowTransient(HandoffStatusText(QStringLiteral("pending"),
                                                     normalized, QString()));
        if (handoff_poll_timer_ == nullptr) {
          handoff_poll_timer_ = new QTimer(this);
          handoff_poll_timer_->setInterval(1000);
          connect(handoff_poll_timer_, &QTimer::timeout, this,
                  [this]() { PollHandoffState(); });
        }
        handoff_poll_timer_->start();
      });
}

void MainWindow::PollHandoffState() {
  if (handoff_session_.isEmpty()) {
    if (handoff_poll_timer_ != nullptr) {
      handoff_poll_timer_->stop();
    }
    return;
  }
  if (++handoff_poll_ticks_ > 60) {
    if (handoff_poll_timer_ != nullptr) {
      handoff_poll_timer_->stop();
    }
    const int active = supervisor_.active_index();
    QJsonObject fail{{QStringLiteral("session_id"), handoff_session_},
                     {QStringLiteral("error"),
                      QStringLiteral("client timed out waiting for handoff")}};
    supervisor_.RequestRpc(active, QStringLiteral("handoff.fail"), fail,
                           [](const QJsonObject&, const QString&) {});
    status_bar_->ShowTransient(HandoffStatusText(QStringLiteral("failed"),
                                                 handoff_platform_,
                                                 QStringLiteral("timed out")));
    handoff_session_.clear();
    return;
  }
  const int active = supervisor_.active_index();
  const QString session_id = handoff_session_;
  const QString platform = handoff_platform_;
  QJsonObject params{{QStringLiteral("session_id"), session_id}};
  supervisor_.RequestRpc(
      active, QStringLiteral("handoff.state"), params,
      [this, session_id, platform](const QJsonObject& result,
                                   const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Handoff state failed: %1").arg(error));
          return;
        }
        const QString state = result.value(QStringLiteral("state")).toString();
        const QString reported_platform =
            result.value(QStringLiteral("platform")).toString(platform);
        const QString message =
            HandoffStatusText(state, reported_platform,
                              result.value(QStringLiteral("error")).toString());
        status_bar_->ShowTransient(message);
        if (state == QStringLiteral("completed") ||
            state == QStringLiteral("failed") || state.isEmpty()) {
          if (handoff_poll_timer_ != nullptr) {
            handoff_poll_timer_->stop();
          }
          if (session_id == handoff_session_) {
            handoff_session_.clear();
          }
          supervisor_.Note(supervisor_.active_index(), message);
        }
      });
}

void MainWindow::ShowDetachedChat() {
  if (detached_chat_ != nullptr) {
    detached_chat_->show();
    detached_chat_->raise();
    detached_chat_->activateWindow();
    return;
  }
  detached_chat_ = new DetachedChatWindow(theme_);
  connect(detached_chat_, &QObject::destroyed, this,
          [this]() { detached_chat_ = nullptr; });
  ChatView* view = detached_chat_->chat_view();
  view->SetEditMessageHandler(
      [this](const ChatMessage& message, const QString& text) {
        EditMessage(message, text);
      });
  view->SetRegenerateMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        RegenerateMessage(message);
      });
  view->SetReadAloudHandler(
      [this](const ChatMessage& message, const QString& text) {
        ReadAloud(message, text);
      });
  view->SetPinMessageHandler(
      [this](const ChatMessage& message, const QString& text) {
        PinMessage(message, text);
      });
  view->SetBranchMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        BranchFromMessage(message);
      });
  view->SetRewindMessageHandler(
      [this](const ChatMessage& message, const QString&) {
        RewindLastTurn(message);
      });
  view->SetReactMessageHandler(
      [this](const ChatMessage& message, const QString& emoji) {
        ReactToMessage(message, emoji);
      });
  view->SetDeleteConversationHandler([this]() { DeleteConversation(); });
  view->SetRequestRespondHandler([this](const QString& request_id,
                                        const QJsonObject& result) {
    supervisor_.RespondRequest(supervisor_.active_index(), request_id, result);
  });

  Composer* composer = detached_chat_->composer();
  composer->SetHiddenModels(
      ParseHiddenModels(NativePref(QStringLiteral("native.model_visibility"))));
  composer->on_stop = [this]() {
    status_bar_->ShowTransient(QStringLiteral("Stopping…"), 2000);
    supervisor_.Interrupt(supervisor_.active_index());
  };
  composer->on_model_selected = [this](const QString& provider,
                                       const QString& model) {
    supervisor_.SetModel(supervisor_.active_index(), provider, model);
  };
  composer->on_use_default_model = [this]() {
    supervisor_.UseDefaultModel(supervisor_.active_index());
  };
  composer->on_refresh_models = [this]() {
    supervisor_.RefreshModelOptions(supervisor_.active_index());
  };
  composer->on_reasoning_selected = [this](const QString& effort) {
    supervisor_.SetReasoning(supervisor_.active_index(), effort);
  };
  connect(composer, &Composer::VoiceToggleRequested, this,
          [this]() { ToggleLiveVoice(); });
  connect(composer, &Composer::Submitted, this,
          [this, composer, view](const QString& text) {
            const QString trimmed = text.trimmed();
            if (trimmed.isEmpty()) {
              return;
            }
            const QStringList attachments = composer->AttachmentPaths();
            const bool slash_command =
                attachments.isEmpty() && trimmed.startsWith(QLatin1Char('/'));
            if (attachments.isEmpty() && !slash_command &&
                view->AnswerConnectionDisplay(trimmed)) {
              composer->Clear();
              return;
            }
            if (view->HasConnectionRequest()) {
              view->ContinueConnection();
            }
            if (slash_command && RunSlashCommand(trimmed)) {
              composer->Clear();
              return;
            }
            QStringList pinned;
            pinned.reserve(static_cast<int>(pinned_context_.size()));
            for (const QString& body : pinned_context_) {
              pinned.append(body);
            }
            const QString prompt = ComposePinnedPrompt(pinned, trimmed);
            if (attachments.isEmpty()) {
              supervisor_.SendPrompt(supervisor_.active_index(), prompt);
            } else {
              supervisor_.SendPromptWithAttachments(supervisor_.active_index(),
                                                    prompt, attachments);
            }
            composer->Clear();
          });
  connect(composer, &Composer::SteerRequested, this,
          [this, composer](const QString& text) {
            supervisor_.Steer(supervisor_.active_index(), text);
            steered_prompts_.append(text);
            while (steered_prompts_.size() > 20) {
              steered_prompts_.removeFirst();
            }
            composer->Clear();
            status_bar_->ShowTransient(QStringLiteral("Steering…"));
          });
  connect(composer, &Composer::QueuedReviewRequested, this,
          [this]() { ShowQueuedPromptsDialog(); });
  connect(composer, &Composer::QueueRequested, this,
          [this, composer](const QString& text) {
            QueuePromptText(text);
            composer->Clear();
          });
  connect(composer, &Composer::FilesDropped, this,
          [this](const QStringList& paths) {
            for (const QString& path : paths) {
              supervisor_.AttachFile(supervisor_.active_index(), path);
            }
          });
  composer->SetPathLister(
      [this](const QString& path, Composer::PathListCallback done) {
        RunPageBackground(
            [this, path]() {
              BackgroundResult result;
              QJsonArray entries;
              if (!supervisor_.ListDirectory(path, &entries, &result.error)) {
                return result;
              }
              result.data.insert(QStringLiteral("entries"), entries);
              return result;
            },
            [done = std::move(done)](const QJsonObject& data,
                                     const QString& error) {
              done(data.value(QStringLiteral("entries")).toArray(), error);
            });
      });

  int queue_badge = queued_prompts_.size();
  if (const auto snapshot = supervisor_.ActiveSnapshot()) {
    queue_badge = PendingQueueCount(*snapshot);
    detached_chat_->SetSnapshot(snapshot);
  }
  composer->SetQueuedPrompts(queue_badge);
  detached_chat_->show();
  detached_chat_->raise();
  detached_chat_->activateWindow();
}

void MainWindow::ShowBrowserAutomation() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  bool use_real_profile = false;
  if (snapshot != nullptr && snapshot->config_loaded) {
    use_real_profile = BrowserUseRealProfile(snapshot->config);
  } else {
    use_real_profile =
        NativePref(QStringLiteral("native.browser_real_profile")) ==
        QStringLiteral("1");
  }
  BrowserConsentState consent;
  consent.use_real_profile = use_real_profile;
  consent.prompt_dismissed =
      NativePref(
          QStringLiteral("native.browser_real_profile_prompt_dismissed")) ==
      QStringLiteral("1");
  consent.muted = browser_consent_muted_;

  bool connect_now = false;
  if (ShouldOfferBrowserConsent(consent)) {
    BrowserConsentDialog dialog{this};
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (dialog.dont_ask_again()) {
      SetNativePref(
          QStringLiteral("native.browser_real_profile_prompt_dismissed"),
          QStringLiteral("1"));
    }
    if (!accepted) {
      // "Not now" declines the real profile, not browser automation itself;
      // the gateway can still attach to an isolated debug browser.
      browser_consent_muted_ = true;
    } else {
      // The desktop stores this in config (`browser.use_real_profile`). Keep a
      // native fallback only for the window between SaveConfig and the refetch.
      SetNativePref(QStringLiteral("native.browser_real_profile"),
                    QStringLiteral("1"));
      supervisor_.SaveConfig(supervisor_.active_index(),
                             BuildBrowserConsentPatch(true));
      connect_now = true;
    }
    if (dialog.dont_ask_again()) {
      SetNativePref(
          QStringLiteral("native.browser_real_profile_prompt_dismissed"),
          QStringLiteral("1"));
    }
    // The desktop stores this in config (`browser.use_real_profile`). Keep a
    // native fallback only for the window between SaveConfig and the refetch.
    SetNativePref(QStringLiteral("native.browser_real_profile"),
                  QStringLiteral("1"));
    supervisor_.SaveConfig(supervisor_.active_index(),
                           BuildBrowserConsentPatch(true));
    connect_now = true;
  }

  if (browser_automation_ == nullptr) {
    BrowserAutomationDialog::Hooks hooks;
    hooks.manage = [this](const QString& action, const QString& url,
                          CdpCallback done) {
      const auto active_snapshot = supervisor_.ActiveSnapshot();
      const QString profile =
          active_snapshot != nullptr ? active_snapshot->name : QString();
      QJsonObject params = BuildBrowserManageParams(action, url, profile);
      supervisor_.RequestRpc(supervisor_.active_index(),
                             QStringLiteral("browser.manage"), params,
                             std::move(done));
    };
    hooks.save_screenshot = [this](const QByteArray& png,
                                   const QString& suggested_name) {
      QString path = QFileDialog::getSaveFileName(
          this, QStringLiteral("Save browser screenshot"),
          QDir::home().filePath(suggested_name),
          QStringLiteral("PNG image (*.png)"));
      if (path.isEmpty()) {
        return;
      }
      if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".png");
      }
      RunPageBackground(
          [png, path]() {
            BackgroundResult result;
            QString error;
            if (!SaveBrowserScreenshot(png, path, &error)) {
              result.error = error;
              return result;
            }
            result.data.insert(QStringLiteral("path"), path);
            return result;
          },
          [this](const QJsonObject& data, const QString& error) {
            if (!error.isEmpty()) {
              status_bar_->ShowTransient(error);
              return;
            }
            status_bar_->ShowTransient(
                QStringLiteral("Saved browser screenshot: %1")
                    .arg(data.value(QStringLiteral("path")).toString()));
          });
    };
    browser_automation_ = new BrowserAutomationDialog(std::move(hooks), this);
    browser_automation_->setAttribute(Qt::WA_DeleteOnClose);
    connect(browser_automation_, &QObject::destroyed, this,
            [this]() { browser_automation_ = nullptr; });
  }
  browser_automation_->show();
  browser_automation_->raise();
  browser_automation_->activateWindow();
  if (connect_now) {
    browser_automation_->ConnectNow();
  }
}

void MainWindow::TogglePetOverlay() {
  if (pet_overlay_ != nullptr && pet_overlay_->isVisible()) {
    HidePetOverlay();
    return;
  }
  if (pet_overlay_ == nullptr) {
    pet_overlay_ = new PetOverlay(theme_);
    pet_overlay_->SetQuickEntryHandler([this]() { ShowQuickEntry(); });
    pet_overlay_->SetHideHandler([this]() { HidePetOverlay(); });
    pet_overlay_->SetSubmitHandler(
        [this](const QString& text) { SubmitQuickEntry(text); });
    pet_poll_timer_ = new QTimer(this);
    pet_poll_timer_->setInterval(30000);
    connect(pet_poll_timer_, &QTimer::timeout, this,
            [this]() { PollPetOverlay(); });
  }
  const QString saved_x = NativePref(QStringLiteral("pet.overlay_x"));
  const QString saved_y = NativePref(QStringLiteral("pet.overlay_y"));
  if (!saved_x.isEmpty() && !saved_y.isEmpty()) {
    pet_overlay_->move(saved_x.toInt(), saved_y.toInt());
  } else if (QScreen* screen = QGuiApplication::primaryScreen()) {
    const QRect available = screen->availableGeometry();
    const QSize size = pet_overlay_->PreferredSize();
    pet_overlay_->move(available.right() - size.width() - 28,
                       available.bottom() - size.height() - 28);
  }
  pet_overlay_->show();
  pet_overlay_->raise();
  if (pet_poll_timer_ != nullptr) {
    pet_poll_timer_->start();
  }
  RefreshPetOverlay();
}

void MainWindow::RefreshPetOverlay() {
  if (pet_overlay_ == nullptr || pet_fetch_pending_) {
    return;
  }
  const int index = supervisor_.active_index();
  if (index < 0) {
    return;
  }
  QJsonObject params;
  if (pet_overlay_->HasSprite() && !pet_revision_.isEmpty()) {
    // The gateway can skip the multi-megabyte sheet when the revision matches.
    params.insert(QStringLiteral("knownRevision"), pet_revision_);
  }
  pet_fetch_pending_ = true;
  supervisor_.RequestRpc(
      index, QStringLiteral("pet.info"), params,
      [this](const QJsonObject& result, const QString& error) {
        pet_fetch_pending_ = false;
        if (pet_overlay_ == nullptr) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Pet unavailable: %1").arg(error));
          return;
        }
        if (!result.value(QStringLiteral("enabled")).toBool(false)) {
          pet_revision_.clear();
          HidePetOverlay();
          status_bar_->ShowTransient(
              QStringLiteral("No desktop pet is configured"));
          return;
        }
        const QString revision =
            result.value(QStringLiteral("spritesheetRevision")).toString();
        const bool unchanged =
            result.value(QStringLiteral("spritesheetUnchanged")).toBool(false);
        pet_revision_ = revision;
        if (!(unchanged && pet_overlay_->HasSprite())) {
          pet_overlay_->SetSpritePayload(result);
        }
        pet_overlay_->show();
        pet_overlay_->raise();
        if (pet_poll_timer_ != nullptr && !pet_poll_timer_->isActive()) {
          pet_poll_timer_->start();
        }
        const auto snapshot = supervisor_.ActiveSnapshot();
        if (snapshot) {
          ApplyPetState(*snapshot);
        }
      });
}

void MainWindow::PollPetOverlay() {
  if (pet_overlay_ == nullptr || !pet_overlay_->isVisible()) {
    if (pet_poll_timer_ != nullptr) {
      pet_poll_timer_->stop();
    }
    return;
  }
  const int index = supervisor_.active_index();
  if (index < 0) {
    return;
  }
  supervisor_.RequestRpc(
      index, QStringLiteral("pet.info.meta"), QJsonObject(),
      [this](const QJsonObject& result, const QString& error) {
        if (pet_overlay_ == nullptr || !error.isEmpty()) {
          return;
        }
        if (!result.value(QStringLiteral("enabled")).toBool(false)) {
          pet_revision_.clear();
          HidePetOverlay();
          return;
        }
        const QString revision =
            result.value(QStringLiteral("spritesheetRevision")).toString();
        if (revision != pet_revision_ || !pet_overlay_->HasSprite()) {
          RefreshPetOverlay();
        }
      });
}

void MainWindow::HidePetOverlay() {
  if (pet_poll_timer_ != nullptr) {
    pet_poll_timer_->stop();
  }
  if (pet_overlay_ != nullptr) {
    pet_overlay_->hide();
  }
}

void MainWindow::ApplyPetState(const ProfileSnapshot& snapshot) {
  if (pet_overlay_ == nullptr || !pet_overlay_->isVisible()) {
    return;
  }
  QString state = QStringLiteral("idle");
  QString status;
  if (snapshot.running) {
    state = QStringLiteral("running");
    status = QStringLiteral("Working…");
  } else if (!snapshot.requests.empty()) {
    state = QStringLiteral("waiting");
    status = snapshot.requests.front().title.isEmpty()
                 ? QStringLiteral("Waiting for you")
                 : snapshot.requests.front().title;
  } else if (snapshot.unread > 0) {
    state = QStringLiteral("review");
    status = QStringLiteral("New reply");
  }
  pet_overlay_->SetState(state);
  pet_overlay_->SetStatus(status);
}

void MainWindow::SubmitQuickEntry(const QString& text) {
  const int active = supervisor_.active_index();
  if (active < 0 || text.trimmed().isEmpty()) {
    return;
  }
  // The desktop quick window has no gateway connection of its own; it hands
  // the prompt to the primary shell, which uses the normal new-chat path.
  const QString project_id =
      project_scope_ == AllProjectsId() ? QString() : project_scope_;
  supervisor_.NewSession(active, NewSessionCwd(), project_id);
  supervisor_.SendPrompt(active, text.trimmed());
  status_bar_->ShowTransient(QStringLiteral("Quick entry started a new chat"));
}

void MainWindow::Dictate() {
  const int active = supervisor_.active_index();
  if (active < 0) {
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("Dictate a recording"), QString(),
      QStringLiteral("Audio (*.wav *.mp3 *.m4a *.mp4 *.ogg *.opus *.flac "
                     "*.webm);;All files (*)"));
  if (path.isEmpty()) {
    return;
  }
  status_bar_->ShowTransient(QStringLiteral("Transcribing recording…"));
  supervisor_.TranscribeAudio(
      active, path, [this](const QJsonObject& result, const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Transcription failed: %1").arg(error));
          return;
        }
        const QString transcript =
            result.value(QStringLiteral("transcript")).toString().trimmed();
        if (transcript.isEmpty()) {
          status_bar_->ShowTransient(QStringLiteral("No speech detected"));
          return;
        }
        QString draft = composer_->Text();
        if (!draft.isEmpty() && !draft.endsWith(QLatin1Char(' ')) &&
            !draft.endsWith(QLatin1Char('\n'))) {
          draft += QLatin1Char(' ');
        }
        composer_->SetText(draft + transcript);
        composer_->FocusEditor();
        status_bar_->ShowTransient(
            QStringLiteral("Transcribed %1 characters").arg(transcript.size()));
      });
}

void MainWindow::ToggleLiveVoice() {
  const int active = supervisor_.active_index();
  if (active < 0) {
    return;
  }
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot != nullptr && snapshot->voice_recording) {
    QJsonObject params{{QStringLiteral("action"), QStringLiteral("stop")}};
    if (!snapshot->selected_session_id.isEmpty()) {
      params.insert(QStringLiteral("session_id"),
                    snapshot->selected_session_id);
    }
    status_bar_->ShowTransient(QStringLiteral("Transcribing voice…"));
    supervisor_.RequestRpc(
        active, QStringLiteral("voice.record"), params,
        [this](const QJsonObject&, const QString& error) {
          if (!error.isEmpty()) {
            status_bar_->ShowTransient(
                QStringLiteral("Voice stop failed: %1").arg(error));
          }
        });
    return;
  }

  // `voice.record` refuses to start unless voice mode is on. Enabling it here
  // is idempotent and keeps the composer button a one-click push-to-talk path
  // instead of a two-step settings dance.
  status_bar_->ShowTransient(QStringLiteral("Starting microphone…"));
  supervisor_.RequestRpc(
      active, QStringLiteral("voice.toggle"),
      QJsonObject{{QStringLiteral("action"), QStringLiteral("on")}},
      [this, active](const QJsonObject&, const QString& error) {
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Live voice is unavailable: %1").arg(error));
          return;
        }
        QJsonObject record{{QStringLiteral("action"), QStringLiteral("start")}};
        const auto current = supervisor_.ActiveSnapshot();
        if (current != nullptr && !current->selected_session_id.isEmpty()) {
          record.insert(QStringLiteral("session_id"),
                        current->selected_session_id);
        }
        supervisor_.RequestRpc(
            active, QStringLiteral("voice.record"), record,
            [this](const QJsonObject& result, const QString& record_error) {
              if (!record_error.isEmpty()) {
                status_bar_->ShowTransient(
                    QStringLiteral("Live voice failed: %1").arg(record_error));
                return;
              }
              const QString status =
                  result.value(QStringLiteral("status")).toString();
              if (status == QStringLiteral("busy")) {
                const QString reason =
                    result.value(QStringLiteral("reason")).toString();
                status_bar_->ShowTransient(
                    reason == QStringLiteral("wake_owned")
                        ? QStringLiteral("The wake-word listener owns the mic")
                        : QStringLiteral("The microphone is busy"));
                return;
              }
              status_bar_->ShowTransient(
                  QStringLiteral("Listening… press the mic again to stop"));
            });
      });
}

void MainWindow::ShowAddProfile() {
  bool ok = false;
  const QString name = QInputDialog::getText(
      this, QStringLiteral("Add profile"),
      QStringLiteral("Profile name (letters, digits, dashes):"),
      QLineEdit::Normal, QString(), &ok);
  if (!ok || name.trimmed().isEmpty()) {
    return;
  }
  QString error;
  if (!supervisor_.CreateProfile(name.trimmed(), &error)) {
    QMessageBox::warning(
        this, QStringLiteral("Add profile failed"),
        error.isEmpty() ? QStringLiteral("Unknown error") : error);
    return;
  }
  drafts_.assign(static_cast<std::size_t>(supervisor_.profile_count()),
                 QString());
  ActivateProfileByName(name.trimmed());
  Refresh();
  UpdateSidebarChips();
}

void MainWindow::PinSessionForProbe(const QString& session_id) {
  supervisor_.PatchSession(supervisor_.active_index(), session_id,
                           QJsonObject{{QStringLiteral("pinned"), true}});
}

void MainWindow::SetComposerTextForProbe(const QString& text) {
  composer_->SetText(text);
  composer_->FocusEditor();
}

void MainWindow::PurgeProbeSessions() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (!snapshot) {
    return;
  }
  for (const SessionSummary& session : snapshot->sessions) {
    const QString title = session.title.trimmed().toLower();
    if (title.startsWith(QStringLiteral("single word ok")) ||
        title.startsWith(QStringLiteral("simple ok reply")) ||
        title == QStringLiteral("untitled")) {
      qInfo("purging probe session %s (%s)", qUtf8Printable(session.id),
            qUtf8Printable(session.title));
      supervisor_.DeleteSession(supervisor_.active_index(), session.id);
    }
  }
}

void MainWindow::RunSteerProbe() {
  // End-to-end steer check without touching real chats: mint a session, run a
  // tiny turn, steer it, then delete the session again.
  NewSession();
  QTimer::singleShot(1800, this, [this]() {
    supervisor_.SendPrompt(supervisor_.active_index(),
                           QStringLiteral("Reply with the single word: ok"));
  });
  QTimer::singleShot(2600, this, [this]() {
    const auto snapshot = supervisor_.ActiveSnapshot();
    qInfo("steer probe running=%d generation snapshot=%d",
          snapshot && snapshot->running ? 1 : 0,
          static_cast<int>(snapshot ? snapshot->messages.size() : 0));
    // Go through the composer's own path so the probe covers the whole
    // /steer route (parse -> dispatch -> session.steer -> notice).
    SubmitText(QStringLiteral("/steer Also reply with the word: steered"));
  });
  QTimer::singleShot(9000, this, [this]() {
    const auto snapshot = supervisor_.ActiveSnapshot();
    if (snapshot && !snapshot->selected_session_id.isEmpty()) {
      qInfo("steer probe deleting session=%s",
            qUtf8Printable(snapshot->selected_session_id));
      supervisor_.DeleteSession(supervisor_.active_index(),
                                snapshot->selected_session_id);
    }
  });
}

void MainWindow::SteerForProbe(const QString& text) {
  supervisor_.Steer(supervisor_.active_index(), text);
}

void MainWindow::RunSlashForProbe(const QString& command) {
  composer_->SetText(command);
  SubmitText(command);
}

void MainWindow::DemoCompleteForProbe() {
  ProfileSnapshot snapshot;
  OnGenerationFinished(snapshot, 12400);
  composer_->SetRunning(false);
  sidebar_->SetRunning(false);
  status_bar_->SetWorkState(false, 0);
}

void MainWindow::SetDemoRunningForProbe(bool running) {
  composer_->SetRunning(running);
  sidebar_->SetRunning(running);
  status_bar_->SetWorkState(running,
                            running ? QDateTime::currentMSecsSinceEpoch() : 0);
}

void MainWindow::PressKeyForProbe(const QString& sequence) {
  QKeySequence key_sequence(sequence);
  if (key_sequence.isEmpty()) {
    return;
  }
  const int combined = key_sequence[0].toCombined();
  const auto key = static_cast<Qt::Key>(combined & 0x01ffffff);
  const Qt::KeyboardModifiers modifiers =
      Qt::KeyboardModifiers((combined & 0xfe000000) >> 24 << 24);
  QWidget* target = QApplication::focusWidget();
  if (target == nullptr) {
    target = this;
  }
  QKeyEvent press(QEvent::KeyPress, key, modifiers);
  QApplication::sendEvent(target, &press);
  QKeyEvent release(QEvent::KeyRelease, key, modifiers);
  QApplication::sendEvent(target, &release);
}

void MainWindow::ShowDemoTranscript() {
  auto snapshot = std::make_shared<ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  ChatMessage user;
  user.role = MessageRole::kUser;
  user.author = QStringLiteral("You");
  user.timestamp = QDateTime::currentDateTime();
  MessageBlock prompt;
  prompt.kind = BlockKind::kParagraph;
  prompt.text =
      QStringLiteral("Can you read the build log and tell me what broke?");
  user.blocks.push_back(prompt);
  snapshot->messages.push_back(user);

  ChatMessage answer;
  answer.role = MessageRole::kAssistant;
  answer.author = QStringLiteral("Hermes");
  answer.timestamp = QDateTime::currentDateTime();
  answer.reasoning = QStringLiteral(
      "The user wants the failing step from the build log.\n"
      "First read the log, then isolate the first error line.\n"
      "The linker complains about a missing symbol, so the fix is in "
      "the target sources, not the test harness.");
  answer.reasoning_streaming = false;
  MessageBlock text;
  text.kind = BlockKind::kParagraph;
  text.text = QStringLiteral(
      "The build fails at link time: `undefined symbol: hermes_ui_main`. "
      "`main.cc` is not in the target's sources.");
  answer.blocks.push_back(text);
  MessageBlock code;
  code.kind = BlockKind::kCode;
  code.language = QStringLiteral("cmake");
  code.text = QStringLiteral(
      "add_executable(hermes_ui\n  src/main.cc   # <- was missing\n)");
  answer.blocks.push_back(code);
  MessageBlock tool;
  tool.kind = BlockKind::kTool;
  tool.text = QStringLiteral("terminal");
  tool.detail = QStringLiteral(
      "$ cmake --build build\n"
      "[ 33%] Building CXX object CMakeFiles/hermes_ui.dir/src/main.cc.o\n"
      "ld: undefined symbol: hermes_ui_main\n"
      "clang: error: linker command failed with exit code 1");
  answer.blocks.push_back(tool);
  MessageBlock follow;
  follow.kind = BlockKind::kParagraph;
  follow.text = QStringLiteral("Adding that file back makes the target link.");
  answer.blocks.push_back(follow);
  snapshot->messages.push_back(answer);

  demo_mode_ = true;
  chat_view_->SetProfileName(snapshot->label);
  chat_view_->SetSnapshot(snapshot);
}

void MainWindow::ClickForProbe(const QString& target_name) {
  const auto targets = chat_view_->ClickTargets();
  if (targets.empty()) {
    return;
  }
  QRectF target = targets.front().rect;
  for (const ClickTarget& candidate : targets) {
    if (candidate.name == target_name ||
        candidate.name.startsWith(target_name)) {
      target = candidate.rect;
      break;
    }
  }
  const QPointF center = target.center();
  QWidget* transcript = chat_view_->widget();
  if (transcript == nullptr) {
    return;
  }
  QMouseEvent press(QEvent::MouseButtonPress, center,
                    transcript->mapToGlobal(center), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(transcript, &press);
}

void MainWindow::SelectSessionForProbe(const QString& session_id) {
  supervisor_.SelectSession(supervisor_.active_index(), session_id);
  ClearPinnedContext();
}

void MainWindow::AttachFileForProbe(const QString& local_path) {
  supervisor_.AttachFile(supervisor_.active_index(), local_path);
}

void MainWindow::HandleSessionAction(const QString& session_id,
                                     const QString& action) {
  const int index = supervisor_.active_index();
  if (action == QStringLiteral("pin")) {
    supervisor_.PatchSession(index, session_id,
                             QJsonObject{{QStringLiteral("pinned"), true}});
  } else if (action == QStringLiteral("unpin")) {
    supervisor_.PatchSession(index, session_id,
                             QJsonObject{{QStringLiteral("pinned"), false}});
  } else if (action == QStringLiteral("archive")) {
    supervisor_.PatchSession(index, session_id,
                             QJsonObject{{QStringLiteral("archived"), true}});
  } else if (action == QStringLiteral("unarchive")) {
    supervisor_.PatchSession(index, session_id,
                             QJsonObject{{QStringLiteral("archived"), false}});
  } else if (action == QStringLiteral("rename")) {
    bool ok = false;
    QString title;
    for (const SessionSummary& session :
         supervisor_.ActiveSnapshot()->sessions) {
      if (session.id == session_id) {
        title = session.title;
        break;
      }
    }
    const QString renamed = QInputDialog::getText(
        this, QStringLiteral("Rename session"), QStringLiteral("Title:"),
        QLineEdit::Normal, title, &ok);
    if (!ok || renamed.trimmed().isEmpty()) {
      return;
    }
    supervisor_.PatchSession(
        index, session_id,
        QJsonObject{{QStringLiteral("title"), renamed.trimmed()}});
  } else if (action == QStringLiteral("delete")) {
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Delete session"),
        QStringLiteral("Delete this session and its messages?"));
    if (answer != QMessageBox::Yes) {
      return;
    }
    supervisor_.DeleteSession(index, session_id);
  }
  Refresh();
}

void MainWindow::ShowSettings() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot) {
    settings_view_->SetSnapshot(*snapshot);
  }
  settings_view_->SetTheme(theme_);
  settings_view_->setGeometry(rect());
  settings_view_->Open();
  settings_view_->raise();
  if (pages_view_ != nullptr) {
    pages_view_->hide();
  }
  sidebar_->SetActivePage(QString());
}

void MainWindow::SetThemeMode(ThemeMode mode) {
  mode_ = mode;
  // Choosing light/dark is a choice about the built-in tokens, so it drops a
  // pre-made preset.
  theme_preset_.clear();
  SetNativePref(QStringLiteral("native.theme.preset"), QString());
  theme_ = mode_ == ThemeMode::kDark ? Theme::Dark() : Theme::Light();
  default_ui_family_ = theme_.ui_family;
  default_mono_family_ = theme_.mono_family;
  user_bubble_base_ = theme_.user_bubble;
  ApplyAppearanceToTheme(appearance_, user_bubble_base_, default_ui_family_,
                         default_mono_family_, &theme_);
  ApplyTheme();
}

void MainWindow::SetThemePreset(const QString& preset_id) {
  if (preset_id.isEmpty()) {
    SetThemeMode(theme_.background.lightness() > 128 ? ThemeMode::kLight
                                                     : ThemeMode::kDark);
    return;
  }
  const ThemePreset preset = ThemePresetById(preset_id);
  if (preset.id.isEmpty()) {
    qWarning("unknown theme preset %s", qUtf8Printable(preset_id));
    return;
  }
  theme_preset_ = preset.id;
  theme_ = preset.theme;
  theme_.ui_family =
      QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  theme_.mono_family = QStringLiteral("JetBrains Mono");
  theme_.icon_family = QStringLiteral("codicon");
  default_ui_family_ = theme_.ui_family;
  default_mono_family_ = theme_.mono_family;
  user_bubble_base_ = theme_.user_bubble;
  ApplyAppearanceToTheme(appearance_, user_bubble_base_, default_ui_family_,
                         default_mono_family_, &theme_);
  if (preset.group == QStringLiteral("light")) {
    mode_ = ThemeMode::kLight;
  } else {
    mode_ = ThemeMode::kDark;
  }
  SetNativePref(QStringLiteral("native.theme.preset"), theme_preset_);
  ApplyTheme();
}

void MainWindow::SetFontScale(double scale) {
  const double clamped = qBound(0.8, scale, 1.6);
  if (qAbs(clamped - font_scale_) < 0.001) {
    return;
  }
  SetNativePref(QStringLiteral("native.font_scale"),
                QString::number(clamped, 'f', 2));
  ApplyAppearancePreferences();
}

void MainWindow::ApplyAppearancePreferences() {
  appearance_ = LoadAppearancePreferences(
      [](const QString& key) { return NativePref(key); });
  font_scale_ = appearance_.ui_scale;
  ApplyAppearanceToTheme(appearance_, user_bubble_base_, default_ui_family_,
                         default_mono_family_, &theme_);
  setWindowOpacity(appearance_.window_opacity / 100.0);
  if (sidebar_ != nullptr) {
    sidebar_->SetSessionDensity(appearance_.session_density);
  }
  if (chat_view_ != nullptr) {
    chat_view_->SetContentMaxWidth(appearance_.message_width);
    chat_view_->SetShowTimestamps(appearance_.show_timestamps);
    chat_view_->SetReasoningCollapsedByDefault(appearance_.reasoning_collapsed);
    chat_view_->SetAnimationsEnabled(appearance_.animations);
    chat_view_->SetChatFontScale(appearance_.chat_text_scale);
    chat_view_->SetCodeFontScale(appearance_.code_scale);
  }
  ApplyTheme();
}

void MainWindow::ApplyKeybinds(const QHash<QString, QString>& overrides) {
  for (auto it = overrides.constBegin(); it != overrides.constEnd(); ++it) {
    const QKeySequence sequence(it.value());
    if (sequence.isEmpty()) {
      continue;
    }
    for (KeyBinding& binding : key_bindings_) {
      if (binding.action != it.key()) {
        continue;
      }
      binding.sequence = sequence;
      binding.text = sequence.toString(QKeySequence::PortableText);
      shortcut_keys_.insert(binding.text);
      break;
    }
  }
}

void MainWindow::ApplyTheme() {
  ApplyGlobalTheme(theme_);
  title_bar_->SetTheme(theme_);
  sidebar_->SetTheme(theme_);
  chat_view_->SetTheme(theme_);
  chat_view_->SetTheme(theme_);
  command_palette_->SetTheme(theme_);
  settings_view_->SetTheme(theme_);
  if (pages_view_ != nullptr) {
    pages_view_->SetTheme(theme_);
  }
  composer_->SetTheme(theme_);
  if (session_search_ != nullptr) {
    session_search_->SetTheme(theme_);
  }
  if (right_rail_ != nullptr) {
    right_rail_->SetTheme(theme_);
  }
  if (pet_overlay_ != nullptr) {
    pet_overlay_->SetTheme(theme_);
  }
  if (detached_chat_ != nullptr) {
    detached_chat_->SetTheme(theme_);
  }
  status_bar_->SetTheme(theme_);
  update();
}

void MainWindow::ActivateProfile(int index) {
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  const int previous = supervisor_.active_index();
  if (previous >= 0 && previous < static_cast<int>(drafts_.size())) {
    drafts_[static_cast<std::size_t>(previous)] = composer_->Text();
  }

  QElapsedTimer timer;
  timer.start();
  supervisor_.Activate(index);
  // Start the project list/tree before the heavier snapshot rebind below. The
  // list reply paints project rows immediately; the tree then fills counts and
  // session membership without the sidebar showing an empty project section.
  RefreshProjects(true);
  SetNativePref(QStringLiteral("native.active_profile"),
                supervisor_.ProfileName(index));
  const auto snapshot = supervisor_.ActiveSnapshot();
  chat_view_->SetSnapshot(snapshot);
  if (snapshot) {
    chat_view_->SetProfileName(snapshot->label);
    sidebar_->SetSnapshot(*snapshot);
    sidebar_->SetActiveIndex(index);
    composer_->SetModelCatalog(snapshot->model_providers, snapshot->provider,
                               snapshot->model);
    composer_->SetReasoning(snapshot->reasoning_effort);
    composer_->SetUsage(snapshot->usage.context_used,
                        snapshot->usage.context_max);
    UpdateStatus();
    if (right_rail_ != nullptr) {
      right_rail_->SetSnapshot(*snapshot);
    }
    composer_->SetMentionRoot(snapshot->home);
    UpdateComposerHistory(*snapshot);
    ApplyRunIndicators(*snapshot);
    pinned_context_.clear();
    last_voice_transcript_serial_ = snapshot->voice_transcript_serial;
    ApplyVoiceState(*snapshot);
    if (settings_view_ != nullptr && settings_view_->IsOpen()) {
      settings_view_->SetSnapshot(*snapshot);
    }
    if (pages_view_ != nullptr && pages_view_->isVisible()) {
      pages_view_->SetSnapshot(*snapshot);
      supervisor_.FetchPage(supervisor_.active_index(),
                            pages_view_->CurrentPage());
    }
    NotifyPendingRequests(*snapshot);
    UpdateRequest(*snapshot);
    QString title = QStringLiteral("Hermes");
    for (const SessionSummary& session : snapshot->sessions) {
      if (session.id == snapshot->selected_session_id) {
        title = session.title;
        break;
      }
    }
    title_bar_->SetTitle(title);
  }
  composer_->SetText(drafts_[static_cast<std::size_t>(index)]);
  sidebar_->SetActiveIndex(index);
  chat_view_->ScrollToBottom();
  chat_view_->repaint();
  last_switch_ms_ = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
  last_revision_ = snapshot ? snapshot->revision : -1;
  last_active_ = -1;
  UpdateSidebarChips();
  UpdateStatus();

  if (snapshot) {
    qInfo("switch profile=%s total_ms=%.3f", qUtf8Printable(snapshot->label),
          last_switch_ms_);
  }
}

void MainWindow::ActivateProfileByName(const QString& name) {
  QStringList profiles;
  for (int index = 0; index < supervisor_.profile_count(); ++index) {
    profiles.push_back(supervisor_.ProfileName(index));
  }
  const int index = ProfileIndexByName(profiles, name);
  if (index >= 0) {
    ActivateProfile(index);
  }
}

void MainWindow::Refresh() {
  if (demo_mode_) {
    return;
  }
  // Completion notifications must fire for every profile, not only the one
  // currently on screen. The 60 ms poll is cheap: it reads shared snapshots.
  CheckProfileCompletions();
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (snapshot && snapshot->revision != last_revision_) {
    chat_view_->SetSnapshot(snapshot);
    sidebar_->SetSnapshot(*snapshot);
    composer_->SetModelCatalog(snapshot->model_providers, snapshot->provider,
                               snapshot->model);
    composer_->SetReasoning(snapshot->reasoning_effort);
    composer_->SetUsage(snapshot->usage.context_used,
                        snapshot->usage.context_max);
    UpdateStatus();
    if (right_rail_ != nullptr) {
      right_rail_->SetSnapshot(*snapshot);
    }
    composer_->SetMentionRoot(snapshot->home);
    UpdateComposerHistory(*snapshot);
    ApplyRunIndicators(*snapshot);
    ApplyVoiceState(*snapshot);
    ApplyGatewayEvents(*snapshot);
    ApplyPetState(*snapshot);
    if (detached_chat_ != nullptr) {
      detached_chat_->SetSnapshot(snapshot);
    }
    while (last_attachment_serial_ < snapshot->attachment_serial &&
           last_attachment_serial_ < snapshot->attachments.size()) {
      const QString path = snapshot->attachments.at(last_attachment_serial_);
      composer_->AddAttachment(QFileInfo(path).fileName(), path);
      ++last_attachment_serial_;
    }
    if (settings_view_ != nullptr && settings_view_->IsOpen()) {
      settings_view_->SetSnapshot(*snapshot);
    }
    if (pages_view_ != nullptr && pages_view_->isVisible()) {
      pages_view_->SetSnapshot(*snapshot);
    }
    UpdateRequest(*snapshot);
    QString title = QStringLiteral("Hermes");
    for (const SessionSummary& session : snapshot->sessions) {
      if (session.id == snapshot->selected_session_id) {
        title = session.title;
        break;
      }
    }
    title_bar_->SetTitle(title);
    last_revision_ = snapshot->revision;
    UpdateStatus();
  }
  UpdateSidebarChips();
  RefreshProjects();
}

void MainWindow::UpdateSidebarChips() {
  if (supervisor_.profile_count() == 0) {
    return;
  }
  int unread_sum = 0;
  int connected_count = 0;
  QString running_signature;
  std::vector<ProfileChip> chips;
  chips.reserve(static_cast<std::size_t>(supervisor_.profile_count()));
  for (int index = 0; index < supervisor_.profile_count(); ++index) {
    const auto snapshot = supervisor_.SnapshotAt(index);
    ProfileChip chip;
    chip.label = supervisor_.ProfileLabel(index);
    chip.color = supervisor_.ProfileColor(index);
    chip.is_default = supervisor_.IsDefault(index);
    chip.unread = snapshot ? snapshot->unread : 0;
    chip.connected = snapshot ? snapshot->connected : false;
    const bool streaming =
        snapshot && !snapshot->messages.empty() &&
        (snapshot->messages.back().streaming ||
         snapshot->messages.back().reasoning_streaming);
    chip.running = snapshot && (snapshot->running || streaming);
    running_signature.append(chip.running ? QLatin1Char('1')
                                          : QLatin1Char('0'));
    unread_sum += chip.unread;
    if (chip.connected) {
      ++connected_count;
    }
    chips.push_back(chip);
  }
  const int active = supervisor_.active_index();
  if (unread_sum == last_unread_sum_ && active == last_active_ &&
      connected_count == last_connected_ &&
      running_signature == last_running_signature_) {
    return;
  }
  last_unread_sum_ = unread_sum;
  last_active_ = active;
  last_connected_ = connected_count;
  last_running_signature_ = running_signature;
  sidebar_->SetProfileChips(chips, active);
}

void MainWindow::RecordSteeredPrompt(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  // Re-injecting a history row moves it to the front instead of adding a
  // duplicate entry every time.
  steered_prompts_.removeAll(trimmed);
  steered_prompts_.append(trimmed);
  while (steered_prompts_.size() > 20) {
    steered_prompts_.removeFirst();
  }
}

int MainWindow::PendingQueueCount(const ProfileSnapshot& snapshot) const {
  int count = queued_prompts_.size();
  for (const PendingAdmission& admission : snapshot.pending_admissions) {
    // A started admission belongs to the live turn; the desktop queue panel
    // deliberately hides it even though it can still be interrupted.
    if (admission.status != QStringLiteral("started")) {
      ++count;
    }
  }
  return count;
}

void MainWindow::SyncComposerQueueBadge(int override_count) {
  int count = queued_prompts_.size();
  if (override_count >= 0) {
    count = override_count;
  } else if (const auto snapshot = supervisor_.ActiveSnapshot()) {
    count = PendingQueueCount(*snapshot);
  }
  if (composer_ != nullptr) {
    composer_->SetQueuedPrompts(count);
  }
  if (detached_chat_ != nullptr && detached_chat_->composer() != nullptr) {
    detached_chat_->composer()->SetQueuedPrompts(count);
  }
}

void MainWindow::QueuePromptText(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  const int active = supervisor_.active_index();
  const auto snapshot = supervisor_.ActiveSnapshot();
  const bool running = snapshot && SelectedTurnRunning(*snapshot);
  int pending = snapshot ? PendingQueueCount(*snapshot) : 0;
  if (running) {
    supervisor_.QueuePrompt(active, trimmed);
    ++pending;
  } else {
    supervisor_.SendPrompt(active, trimmed);
  }
  supervisor_.Note(active,
                   QStringLiteral("Queued for the next turn (%1 in queue): %2")
                       .arg(pending)
                       .arg(trimmed));
  status_bar_->ShowTransient(
      QStringLiteral("Queued (%1) for the next turn").arg(pending));
  SyncComposerQueueBadge(running ? pending : -1);
}

void MainWindow::ShowQueuedPromptsDialog() {
  PromptHistoryDialog dialog(theme_, &queued_prompts_, &steered_prompts_, this);
  const int profile_index = supervisor_.active_index();
  const auto snapshot = supervisor_.SnapshotAt(profile_index);
  const QString session_id =
      snapshot ? snapshot->selected_session_id : QString();
  const QString profile = snapshot ? snapshot->name : QString();
  const auto collect_prompts =
      [this, profile_index,
       session_id]() -> std::vector<PromptHistoryDialog::ServerPrompt> {
    std::vector<PromptHistoryDialog::ServerPrompt> prompts;
    const auto current = supervisor_.SnapshotAt(profile_index);
    if (current == nullptr || current->selected_session_id != session_id) {
      return prompts;
    }
    for (const PendingAdmission& admission : current->pending_admissions) {
      if (admission.status == QStringLiteral("started")) {
        continue;
      }
      PromptHistoryDialog::ServerPrompt prompt;
      prompt.admission_id = admission.admission_id;
      prompt.input_id = admission.input_id;
      prompt.text = admission.text;
      prompt.status = admission.status;
      prompt.outcome = admission.outcome;
      prompt.controls = admission.controls;
      prompt.execution_generation = admission.execution_generation;
      prompt.steering = admission.steering;
      prompts.push_back(std::move(prompt));
    }
    return prompts;
  };
  const auto prompt_signature =
      [](const std::vector<PromptHistoryDialog::ServerPrompt>& prompts) {
        QStringList parts;
        for (const auto& prompt : prompts) {
          parts << QStringLiteral("%1\x1f%2\x1f%3\x1f%4\x1f%5\x1f%6")
                       .arg(prompt.admission_id, prompt.status, prompt.text,
                            prompt.outcome, prompt.controls.join(','),
                            prompt.steering ? QStringLiteral("1")
                                            : QStringLiteral("0"));
        }
        return parts.join(QLatin1Char('\n'));
      };
  std::vector<PromptHistoryDialog::ServerPrompt> server_prompts =
      collect_prompts();
  auto signature = std::make_shared<QString>(prompt_signature(server_prompts));
  dialog.SetServerPrompts(std::move(server_prompts));
  dialog.on_changed = [this]() { SyncComposerQueueBadge(); };
  dialog.on_load = [this](const QString& text) {
    composer_->SetText(text);
    composer_->setFocus();
  };
  auto queue_done = [this, profile_index, session_id](
                        const QString& method, const QJsonObject& params,
                        PromptHistoryDialog::ActionDone done) {
    const int index = profile_index;
    if (index < 0 || session_id.isEmpty()) {
      done(false, QStringLiteral("No session is selected."));
      return;
    }
    supervisor_.RequestRpc(
        index, method, params,
        [done, method](const QJsonObject&, const QString& error) {
          if (!error.isEmpty()) {
            done(false, QStringLiteral("%1: %2").arg(method, error));
            return;
          }
          done(true, QString());
        });
  };
  dialog.on_replace_server =
      [this, queue_done, profile_index, session_id, profile](
          const PromptHistoryDialog::ServerPrompt& prompt, const QString& text,
          PromptHistoryDialog::ActionDone done) {
        const QJsonObject params = BuildPromptReplaceQueuedParams(
            session_id, prompt.admission_id,
            QUuid::createUuid().toString(QUuid::WithoutBraces), text, profile);
        const QString admission_id = prompt.admission_id;
        queue_done(QStringLiteral("prompt.replace_queued"), params,
                   [this, profile_index, admission_id, done](
                       bool ok, const QString& message) {
                     if (ok) {
                       supervisor_.ForgetPendingAdmission(profile_index,
                                                          admission_id);
                     }
                     done(ok, message);
                   });
      };
  auto cancel_server = [this, queue_done, profile_index, session_id, profile](
                           const PromptHistoryDialog::ServerPrompt& prompt,
                           PromptHistoryDialog::ActionDone done) {
    const QString admission_id = prompt.admission_id;
    const auto forget_and_finish = [this, profile_index, admission_id, done](
                                       bool ok, const QString& message) {
      if (ok) {
        supervisor_.ForgetPendingAdmission(profile_index, admission_id);
      }
      done(ok, message);
    };
    if (prompt.status == QStringLiteral("unknown") &&
        prompt.execution_generation >= 0) {
      const QJsonObject params =
          BuildPromptResolveUnknownParams(session_id, prompt.admission_id,
                                          prompt.execution_generation, profile);
      queue_done(QStringLiteral("prompt.resolve_unknown"), params,
                 forget_and_finish);
      return;
    }
    const QJsonObject params =
        BuildPromptCancelParams(session_id, prompt.admission_id, profile);
    queue_done(QStringLiteral("prompt.cancel"), params, forget_and_finish);
  };
  dialog.on_cancel_server = cancel_server;
  // Injecting is an explicit jump to a fresh turn. Follow it even when the
  // transcript was scrolled up so the new user bubble is visible immediately.
  const auto follow_injected =
      [this](PromptHistoryDialog::ActionDone done) {
        return [this, done = std::move(done)](bool ok,
                                              const QString& message) {
          if (ok) {
            chat_view_->SetFollowBottom(true);
            chat_view_->ScrollToBottom();
          }
          done(ok, message);
        };
      };
  dialog.on_send_server =
      [this, cancel_server, follow_injected, profile_index](
          const PromptHistoryDialog::ServerPrompt& prompt,
          PromptHistoryDialog::ActionDone done) {
        const QString text = prompt.text;
        const bool queued =
            !prompt.steering && prompt.status != QStringLiteral("terminal");
        if (!queued) {
          supervisor_.SendPrompt(profile_index, text, follow_injected(done));
          return;
        }
        // Remove the gateway-owned admission before submitting the same text
        // as a fresh prompt; otherwise it would run once from the queue and
        // again from this send.
        cancel_server(
            prompt,
            [this, profile_index, text, follow_injected, done](
                bool ok, const QString& message) {
              if (!ok) {
                done(false, message);
                return;
              }
              supervisor_.SendPrompt(profile_index, text,
                                     follow_injected(done));
            });
      };
  dialog.on_send_local = [this, follow_injected, profile_index](
                             const QString& text,
                             PromptHistoryDialog::ActionDone done) {
    supervisor_.SendPrompt(profile_index, text, follow_injected(done));
  };

  // The backend queue changes while the modal is open (a queued prompt drains,
  // a steer settles, or another client cancels). Refresh by content so a
  // processed row disappears without wiping an unrelated edit.
  auto* sync_timer = new QTimer(&dialog);
  sync_timer->setInterval(400);
  QObject::connect(sync_timer, &QTimer::timeout, &dialog,
                   [&dialog, collect_prompts, prompt_signature, signature]() {
                     std::vector<PromptHistoryDialog::ServerPrompt> prompts =
                         collect_prompts();
                     const QString next = prompt_signature(prompts);
                     if (next == *signature) {
                       return;
                     }
                     *signature = next;
                     dialog.SetServerPrompts(std::move(prompts));
                   });
  sync_timer->start();
  dialog.exec();
  sync_timer->stop();
  SyncComposerQueueBadge();
}

const ProjectInfo* MainWindow::FindProject(const QString& project_id) const {
  for (const ProjectInfo& project : project_state_.projects) {
    if (project.id == project_id) {
      return &project;
    }
  }
  return nullptr;
}

QString MainWindow::ProjectPath(const QString& project_id) const {
  return ProjectWorkingDirectory(project_state_, project_id);
}

void MainWindow::RefreshProjects(bool force) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  const QString profile = supervisor_.ProfileName(index);
  if (project_state_.profile != profile) {
    project_state_ = ProjectsState();
    project_state_.profile = profile;
    project_scope_ = AllProjectsId();
    project_list_pending_ = false;
    project_tree_pending_ = false;
    last_project_fetch_ms_ = 0;
    sidebar_->SetProjects({}, project_scope_, QString());
    LoadProjectCacheForProfile(profile);
  }
  if (project_list_pending_ || project_tree_pending_) {
    return;
  }
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!force) {
    // Startup requests can race the profile socket. Retry quickly until the
    // first successful list/tree; steady-state refreshes stay at 15 s.
    const qint64 interval = project_state_.loaded ? 15000 : 500;
    if (now - last_project_fetch_ms_ < interval) {
      return;
    }
  }
  last_project_fetch_ms_ = now;
  project_list_pending_ = true;
  project_tree_pending_ = true;
  project_state_.loading = true;
  project_state_.error.clear();

  const auto profile_param = [profile](QJsonObject* params) {
    params->insert(QStringLiteral("profile"), profile);
  };

  QJsonObject list_params;
  profile_param(&list_params);
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.list"), list_params,
      [this, index, profile](const QJsonObject& result, const QString& error) {
        if (supervisor_.active_index() != index ||
            supervisor_.ProfileName(index) != profile) {
          return;
        }
        project_list_pending_ = false;
        if (!error.isEmpty()) {
          project_state_.error = error;
          project_state_.available = false;
        } else {
          QString parse_error;
          if (!ParseProjectsList(result, &project_state_, &parse_error)) {
            project_state_.error = parse_error;
          } else {
            project_state_.available = true;
          }
        }
        project_state_.loading = project_list_pending_ || project_tree_pending_;
        // A request issued before the profile socket connects fails with
        // "Gateway is not connected". Keep the short retry interval until a
        // fetch actually succeeds; marking the failed attempt loaded would
        // leave the project section empty for the 15 s steady-state interval.
        project_state_.loaded =
            !project_state_.loading && project_state_.error.isEmpty();
        ApplyProjectState();
        // The first fetch can beat the profile socket. That race is retried
        // below; do not flash a scary "Projects unavailable" status for it.
        if (!project_state_.error.isEmpty() &&
            !project_state_.error.contains(QStringLiteral("not connected"),
                                           Qt::CaseInsensitive)) {
          status_bar_->ShowTransient(QStringLiteral("Projects unavailable: %1")
                                         .arg(project_state_.error));
        }
      });

  QJsonObject tree_params;
  profile_param(&tree_params);
  tree_params.insert(QStringLiteral("preview_limit"), 3);
  tree_params.insert(QStringLiteral("session_limit"), 500);
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.tree"), tree_params,
      [this, index, profile](const QJsonObject& result, const QString& error) {
        if (supervisor_.active_index() != index ||
            supervisor_.ProfileName(index) != profile) {
          return;
        }
        project_tree_pending_ = false;
        if (!error.isEmpty()) {
          project_state_.error = error;
          project_state_.available = false;
        } else {
          QString parse_error;
          if (!ParseProjectsTree(result, &project_state_, &parse_error)) {
            project_state_.error = parse_error;
          } else {
            project_state_.available = true;
          }
        }
        project_state_.loading = project_list_pending_ || project_tree_pending_;
        project_state_.loaded =
            !project_state_.loading && project_state_.error.isEmpty();
        ApplyProjectState();
      });
}

void MainWindow::ApplyProjectState() {
  if (sidebar_ == nullptr) {
    return;
  }
  // `projects.list` normally answers well before the expensive tree scan.
  // Paint those explicit rows immediately so startup shows projects next to
  // the session list instead of waiting for counts and auto-discovery. The
  // authoritative tree replaces them as soon as it lands.
  const bool list_only = project_state_.tree.empty();
  const std::vector<ProjectNode> rows =
      list_only ? ProjectListNodes(project_state_.projects)
                : project_state_.tree;
  // A dismissed auto project is a local layout preference, not a backend
  // delete: keep the full tree in `project_state_` for cache/refresh parity and
  // hide only the rows the user removed.
  if (project_scope_ != AllProjectsId()) {
    const auto scoped = std::find_if(
        rows.begin(), rows.end(),
        [this](const ProjectNode& node) { return node.id == project_scope_; });
    if (scoped == rows.end() ||
        (scoped->is_auto &&
         dismissed_auto_project_ids_.contains(project_scope_))) {
      project_scope_ = AllProjectsId();
    }
  }
  const std::vector<ProjectNode> visible =
      FilterVisibleProjects(rows, dismissed_auto_project_ids_);
  sidebar_->SetProjects(visible, project_scope_, project_state_.active_id,
                        project_state_.available);
  SyncArtifactsRoot();
  SyncRightRailProject();
  ScheduleProjectCacheSave();
}

void MainWindow::SyncArtifactsRoot() {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  supervisor_.SetArtifactsRoot(
      index, ArtifactsWorkingDirectory(project_state_, project_scope_));
}

void MainWindow::SyncRightRailProject() {
  if (right_rail_ == nullptr) {
    return;
  }
  QString project_id = project_scope_;
  if (project_id == AllProjectsId()) {
    project_id = project_state_.active_id;
  }
  QString label = QStringLiteral("Home");
  if (!project_id.isEmpty() && project_id != QStringLiteral("__no_project__")) {
    const auto node = std::find_if(
        project_state_.tree.begin(), project_state_.tree.end(),
        [&project_id](const ProjectNode& row) { return row.id == project_id; });
    if (node != project_state_.tree.end() && !node->label.isEmpty()) {
      label = node->label;
    } else {
      const auto project = std::find_if(project_state_.projects.begin(),
                                        project_state_.projects.end(),
                                        [&project_id](const ProjectInfo& row) {
                                          return row.id == project_id;
                                        });
      if (project != project_state_.projects.end()) {
        label = project->name;
      }
    }
  }
  right_rail_->SetProjectContext(
      project_id, label, ArtifactsWorkingDirectory(project_state_, project_id),
      ProjectGitRepositories(project_state_, project_id));
}

void MainWindow::SetSidebarCollapsed(bool collapsed) {
  if (sidebar_ == nullptr || body_splitter_ == nullptr) {
    return;
  }
  sidebar_collapsed_ = collapsed;
  ApplyPaneLayout(true);
  PersistPaneLayout();
}

void MainWindow::SetRightRailEnabled(bool visible) {
  if (right_rail_ == nullptr || body_splitter_ == nullptr) {
    return;
  }
  right_rail_enabled_ = visible;
  if (visible) {
    right_rail_collapsed_ = false;
  }
  ApplyPaneLayout(true);
  PersistPaneLayout();
}

void MainWindow::ApplyPaneLayout(bool restore_widths) {
  if (body_splitter_ == nullptr || sidebar_ == nullptr ||
      right_rail_ == nullptr) {
    return;
  }
  applying_pane_layout_ = true;
  sidebar_->SetCollapsed(sidebar_collapsed_);
  right_rail_->SetCollapsed(right_rail_collapsed_);
  right_rail_->setVisible(right_rail_enabled_);
  const int left =
      sidebar_collapsed_ ? 46 : std::max(180, std::min(440, sidebar_width_));
  const int right = right_rail_collapsed_
                        ? 44
                        : std::max(260, std::min(800, right_rail_width_));
  if (restore_widths) {
    const int total = body_splitter_->width() > 0 ? body_splitter_->width()
                                                  : std::max(980, width());
    const int handles = body_splitter_->handleWidth() * 2;
    const int chat = std::max(
        320, total - left - handles - (right_rail_enabled_ ? right : 0));
    QList<int> sizes;
    sizes << left << chat << (right_rail_enabled_ ? right : 0);
    body_splitter_->setSizes(sizes);
  }
  title_bar_->SetSidebarWidth(left);
  applying_pane_layout_ = false;
}

void MainWindow::PersistPaneLayout() {
  if (body_splitter_ == nullptr) {
    return;
  }
  SetNativePref(QStringLiteral("layout.sidebar_width"),
                QString::number(sidebar_width_));
  SetNativePref(QStringLiteral("layout.right_rail_width"),
                QString::number(right_rail_width_));
  SetNativePref(QStringLiteral("layout.sidebar_collapsed"),
                sidebar_collapsed_ ? QStringLiteral("1") : QStringLiteral("0"));
  SetNativePref(
      QStringLiteral("layout.right_rail_visible"),
      right_rail_enabled_ ? QStringLiteral("1") : QStringLiteral("0"));
  SetNativePref(
      QStringLiteral("layout.right_rail_collapsed"),
      right_rail_collapsed_ ? QStringLiteral("1") : QStringLiteral("0"));
}

void MainWindow::OnPaneSplitterMoved() {
  if (applying_pane_layout_ || !pane_layout_ready_) {
    return;
  }
  if (body_splitter_ == nullptr || sidebar_ == nullptr) {
    return;
  }
  const QList<int> sizes = body_splitter_->sizes();
  if (sizes.size() >= 3) {
    if (!sidebar_collapsed_ && sizes[0] > 0) {
      sidebar_width_ = sizes[0];
    }
    if (right_rail_enabled_ && !right_rail_collapsed_ && sizes[2] > 0) {
      right_rail_width_ = sizes[2];
    }
  }
  title_bar_->SetSidebarWidth(sidebar_->width());
  if (pane_prefs_timer_ != nullptr) {
    pane_prefs_timer_->start();
  }
}

void MainWindow::LoadProjectCacheForProfile(const QString& profile) {
  const quint64 generation = ++project_cache_generation_;
  QPointer<MainWindow> self(this);
  RunBackground(
      this,
      [profile]() {
        BackgroundResult result;
        CachedProjectState cache;
        if (LoadProjectCache(profile, &cache)) {
          result.data.insert(QStringLiteral("cache"),
                             ProjectCacheToJson(cache));
        }
        return result;
      },
      [self, profile, generation](const BackgroundResult& result) {
        if (!self || !result.error.isEmpty() ||
            self->project_cache_generation_ != generation) {
          return;
        }
        const QJsonObject& data = result.data;
        const int index = self->supervisor_.active_index();
        if (index < 0 || self->supervisor_.ProfileName(index) != profile ||
            self->project_state_.profile != profile) {
          return;
        }
        // Network data may have landed first. Never let a slower disk read
        // roll the sidebar back to an older snapshot.
        if (self->project_state_.loaded ||
            !self->project_state_.projects.empty() ||
            !self->project_state_.tree.empty()) {
          return;
        }
        CachedProjectState cache;
        if (!ProjectCacheFromJson(
                data.value(QStringLiteral("cache")).toObject(), &cache) ||
            cache.profile != profile) {
          return;
        }
        self->project_state_ = cache.projects;
        self->project_state_.profile = profile;
        self->project_state_.loading = true;
        self->project_state_.error.clear();
        self->project_scope_ = cache.scope;
        self->ApplyProjectState();
      });
}

void MainWindow::ScheduleProjectCacheSave() {
  if (project_cache_timer_ == nullptr || !project_state_.loaded ||
      project_state_.profile.isEmpty()) {
    return;
  }
  project_cache_dirty_ = true;
  if (!project_cache_timer_->isActive()) {
    project_cache_timer_->start();
  }
}

void MainWindow::PersistProjectCacheNow() {
  if (!project_cache_dirty_ || !project_state_.loaded ||
      project_state_.profile.isEmpty()) {
    return;
  }
  project_cache_dirty_ = false;
  CachedProjectState cache;
  cache.profile = project_state_.profile;
  cache.scope = project_scope_;
  cache.projects = project_state_;
  RunBackground(
      this,
      [cache]() {
        BackgroundResult result;
        SaveProjectCache(cache, &result.error);
        return result;
      },
      [](const BackgroundResult&) {});
}

void MainWindow::SetProjectScope(const QString& project_id,
                                 bool ensure_session) {
  const QString next_scope =
      project_id.isEmpty() ? AllProjectsId() : project_id;
  const bool scope_changed = next_scope != project_scope_;
  project_scope_ = next_scope;
  ApplyProjectState();
  if (project_scope_ == AllProjectsId()) {
    pending_auto_project_session_.clear();
    return;
  }
  // Only explicit, persisted projects become the profile's durable active
  // workspace. Auto projects and the Home bucket are view scope only.
  const auto node = std::find_if(
      project_state_.tree.begin(), project_state_.tree.end(),
      [this](const ProjectNode& row) { return row.id == project_scope_; });
  if (node != project_state_.tree.end() && !node->is_auto &&
      !node->is_no_project) {
    SetActiveProject(project_scope_);
  }
  if (ensure_session && scope_changed) {
    EnsureSessionForProjectScope();
  }
}

void MainWindow::EnsureSessionForProjectScope() {
  if (project_scope_.isEmpty() || project_scope_ == AllProjectsId()) {
    return;
  }
  const auto snapshot = supervisor_.ActiveSnapshot();
  if (!snapshot) {
    return;
  }

  const QString current = snapshot->selected_session_id;
  QString current_project_id;
  QStringList recency;
  recency.reserve(static_cast<int>(snapshot->sessions.size()));
  for (const SessionSummary& session : snapshot->sessions) {
    if (session.id == current) {
      current_project_id = session.project_id;
    }
    if (!session.archived) {
      recency.push_back(session.id);
    }
  }

  const ProjectEntryDecision decision = ResolveProjectEntry(
      project_state_, project_scope_, recency, current, current_project_id);
  if (decision.action == ProjectEntryAction::kSelectSession) {
    supervisor_.SelectSession(supervisor_.active_index(), decision.session_id);
    ClearPinnedContext();
    pending_auto_project_session_.clear();
    return;
  }
  if (decision.action != ProjectEntryAction::kStartSession) {
    pending_auto_project_session_.clear();
    return;
  }

  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (pending_auto_project_session_ == project_scope_ &&
      now - pending_auto_project_session_ms_ < 5000) {
    return;
  }
  pending_auto_project_session_ = project_scope_;
  pending_auto_project_session_ms_ = now;
  NewSession();
}

void MainWindow::SetActiveProject(const QString& project_id) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  QJsonObject params = BuildProjectIdParams(project_id);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  project_state_.active_id = project_id;
  ApplyProjectState();
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.set_active"), params,
      [this, index](const QJsonObject& result, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not set active project: %1").arg(error));
          return;
        }
        const QString active =
            result.value(QStringLiteral("active_id")).toString();
        project_state_.active_id = active;
        ApplyProjectState();
      });
}

void MainWindow::CreateProject(const QString& initial_name,
                               const QStringList& initial_folders,
                               const QString& adopt_from) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  ProjectDialog dialog(theme_, nullptr, this);
  if (!adopt_from.isEmpty()) {
    dialog.SetCreatePrefill(initial_name, initial_folders);
    dialog.setWindowTitle(QStringLiteral("Save as project"));
  }
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  const QString profile = supervisor_.ProfileName(index);
  const bool use = dialog.use_after_create();
  QJsonObject params = BuildProjectCreateParams(dialog.name(), dialog.folders(),
                                                dialog.primary_path(),
                                                dialog.description(), use);
  params.insert(QStringLiteral("profile"), profile);
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.create"), params,
      [this, index, profile, use, adopt_from](const QJsonObject& result,
                                              const QString& error) {
        if (supervisor_.active_index() != index ||
            supervisor_.ProfileName(index) != profile) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not create project: %1").arg(error));
          return;
        }
        const QJsonObject project =
            result.value(QStringLiteral("project")).toObject();
        const QString id = project.value(QStringLiteral("id")).toString();
        if (!id.isEmpty() &&
            (use || (!adopt_from.isEmpty() && project_scope_ == adopt_from))) {
          project_scope_ = id;
        }
        status_bar_->ShowTransient(adopt_from.isEmpty()
                                       ? QStringLiteral("Project created")
                                       : QStringLiteral("Project saved"));
        RefreshProjects(true);
      });
}

void MainWindow::EditProject(const ProjectInfo& project) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  ProjectDialog dialog(theme_, &project, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QJsonObject params =
      BuildProjectUpdateParams(dialog.name(), dialog.description());
  params.insert(QStringLiteral("id"), project.id);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.update"), params,
      [this, index](const QJsonObject&, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not update project: %1").arg(error));
          return;
        }
        status_bar_->ShowTransient(QStringLiteral("Project updated"));
        RefreshProjects(true);
      });
}

void MainWindow::DeleteProject(const ProjectInfo& project) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  if (QMessageBox::question(
          this, QStringLiteral("Delete project"),
          QStringLiteral("Delete “%1” and its folder links? Sessions are not "
                         "deleted.")
              .arg(project.name)) != QMessageBox::Yes) {
    return;
  }
  QJsonObject params = BuildProjectIdParams(project.id);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.delete"), params,
      [this, index, project](const QJsonObject&, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not delete project: %1").arg(error));
          return;
        }
        if (project_scope_ == project.id) {
          project_scope_ = AllProjectsId();
        }
        status_bar_->ShowTransient(QStringLiteral("Project deleted"));
        RefreshProjects(true);
      });
}

void MainWindow::AddProjectFolder(const ProjectInfo& project) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  const QString path = QFileDialog::getExistingDirectory(
      this, QStringLiteral("Add folder to %1").arg(project.name));
  if (path.isEmpty()) {
    return;
  }
  const bool primary = project.primary_path.isEmpty();
  QJsonObject params = BuildProjectFolderParams(project.id, path, primary);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.add_folder"), params,
      [this, index](const QJsonObject&, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not add folder: %1").arg(error));
          return;
        }
        status_bar_->ShowTransient(QStringLiteral("Folder added"));
        RefreshProjects(true);
      });
}

void MainWindow::RemoveProjectFolder(const ProjectInfo& project) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count() ||
      project.folders.empty()) {
    return;
  }
  QStringList labels;
  for (const ProjectFolderInfo& folder : project.folders) {
    labels.push_back(ProjectFolderLabel(folder) + QStringLiteral(" — ") +
                     folder.path);
  }
  bool ok = false;
  const QString chosen = QInputDialog::getItem(
      this, QStringLiteral("Remove folder"),
      QStringLiteral("Detach this folder from the project:"), labels, 0, false,
      &ok);
  if (!ok) {
    return;
  }
  const int row = labels.indexOf(chosen);
  if (row < 0 || row >= static_cast<int>(project.folders.size())) {
    return;
  }
  QJsonObject params = BuildProjectFolderParams(
      project.id, project.folders[static_cast<std::size_t>(row)].path);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.remove_folder"), params,
      [this, index](const QJsonObject&, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not remove folder: %1").arg(error));
          return;
        }
        status_bar_->ShowTransient(QStringLiteral("Folder removed"));
        RefreshProjects(true);
      });
}

void MainWindow::SetProjectPrimaryFolder(const ProjectInfo& project) {
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count() ||
      project.folders.empty()) {
    return;
  }
  QStringList labels;
  for (const ProjectFolderInfo& folder : project.folders) {
    labels.push_back(ProjectFolderLabel(folder) + QStringLiteral(" — ") +
                     folder.path);
  }
  bool ok = false;
  const QString chosen = QInputDialog::getItem(
      this, QStringLiteral("Primary folder"),
      QStringLiteral("Use this folder as the project's primary path:"), labels,
      static_cast<int>(
          std::max<qsizetype>(0, labels.indexOf(project.primary_path))),
      false, &ok);
  if (!ok) {
    return;
  }
  const int row = labels.indexOf(chosen);
  if (row < 0 || row >= static_cast<int>(project.folders.size())) {
    return;
  }
  QJsonObject params = BuildProjectFolderParams(
      project.id, project.folders[static_cast<std::size_t>(row)].path);
  params.insert(QStringLiteral("profile"), supervisor_.ProfileName(index));
  supervisor_.RequestRpc(
      index, QStringLiteral("projects.set_primary"), params,
      [this, index](const QJsonObject&, const QString& error) {
        if (supervisor_.active_index() != index) {
          return;
        }
        if (!error.isEmpty()) {
          status_bar_->ShowTransient(
              QStringLiteral("Could not set primary folder: %1").arg(error));
          return;
        }
        status_bar_->ShowTransient(QStringLiteral("Primary folder updated"));
        RefreshProjects(true);
      });
}

void MainWindow::HandleProjectAction(const QString& project_id,
                                     const QString& action) {
  if (action == QStringLiteral("copy_path") ||
      action == QStringLiteral("reveal_path")) {
    QString path;
    for (const ProjectNode& node : project_state_.tree) {
      if (node.id == project_id) {
        path = node.path;
        break;
      }
    }
    if (path.isEmpty()) {
      const ProjectInfo* project = FindProject(project_id);
      if (project != nullptr) {
        path = project->primary_path;
      }
    }
    if (path.isEmpty()) {
      status_bar_->ShowTransient(QStringLiteral("Project has no folder path"));
      return;
    }
    if (action == QStringLiteral("copy_path")) {
      QGuiApplication::clipboard()->setText(path);
      status_bar_->ShowTransient(QStringLiteral("Project path copied"));
    } else {
      QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    return;
  }
  if (action == QStringLiteral("scope")) {
    SetProjectScope(project_id);
    return;
  }
  if (action == QStringLiteral("new")) {
    // The user explicitly asked for a fresh chat; do not let project entry
    // auto-select an existing conversation or mint an extra blank one.
    SetProjectScope(project_id, false);
    NewSession();
    return;
  }
  if (action == QStringLiteral("appearance")) {
    ShowProjectAppearance(project_id);
    return;
  }
  if (action == QStringLiteral("dismiss_auto")) {
    DismissAutoProject(project_id);
    return;
  }
  if (action == QStringLiteral("save_as_project")) {
    const auto node = std::find_if(
        project_state_.tree.begin(), project_state_.tree.end(),
        [&project_id](const ProjectNode& row) { return row.id == project_id; });
    if (node == project_state_.tree.end() || !node->is_auto ||
        node->path.isEmpty()) {
      return;
    }
    CreateProject(node->label, QStringList{node->path}, node->id);
    return;
  }
  const ProjectInfo* project = FindProject(project_id);
  if (project == nullptr) {
    return;
  }
  if (action == QStringLiteral("edit")) {
    EditProject(*project);
  } else if (action == QStringLiteral("add_folder")) {
    AddProjectFolder(*project);
  } else if (action == QStringLiteral("remove_folder")) {
    RemoveProjectFolder(*project);
  } else if (action == QStringLiteral("set_primary")) {
    SetProjectPrimaryFolder(*project);
  } else if (action == QStringLiteral("set_active")) {
    SetActiveProject(project_id);
  } else if (action == QStringLiteral("delete")) {
    DeleteProject(*project);
  }
}

void MainWindow::ShowProjectAppearance(const QString& project_id) {
  const auto node = std::find_if(
      project_state_.tree.begin(), project_state_.tree.end(),
      [&project_id](const ProjectNode& row) { return row.id == project_id; });
  if (node == project_state_.tree.end() || node->is_no_project) {
    return;
  }
  const ProjectInfo* project = FindProject(project_id);
  const bool auto_project = node->is_auto;
  if (auto_project && node->path.isEmpty()) {
    return;
  }
  const QString color = !node->color.isEmpty()
                            ? node->color
                            : (project == nullptr ? QString() : project->color);
  const QString icon = !node->icon.isEmpty()
                           ? node->icon
                           : (project == nullptr ? QString() : project->icon);
  const QString path = node->path;
  const QString label = node->label;
  const int index = supervisor_.active_index();
  if (index < 0 || index >= supervisor_.profile_count()) {
    return;
  }
  const QString profile = supervisor_.ProfileName(index);

  ProjectAppearanceDialog dialog(color, icon, this);
  dialog.on_apply = [this, index, profile, project_id, auto_project, path,
                     label](const QString& next_color,
                            const QString& next_icon) {
    if (supervisor_.active_index() != index ||
        supervisor_.ProfileName(index) != profile) {
      return;
    }
    const auto done = [this, index, profile, project_id, auto_project](
                          const QJsonObject& result, const QString& error) {
      if (supervisor_.active_index() != index ||
          supervisor_.ProfileName(index) != profile) {
        return;
      }
      if (!error.isEmpty()) {
        status_bar_->ShowTransient(
            QStringLiteral("Could not update project appearance: %1")
                .arg(error));
        return;
      }
      QString adopted_id;
      if (auto_project) {
        adopted_id = result.value(QStringLiteral("project"))
                         .toObject()
                         .value(QStringLiteral("id"))
                         .toString();
        if (project_scope_ == project_id) {
          project_scope_ = adopted_id.isEmpty() ? AllProjectsId() : adopted_id;
        }
      }
      status_bar_->ShowTransient(QStringLiteral("Project appearance updated"));
      RefreshProjects(true);
    };
    if (auto_project) {
      // Auto rows have no durable record. Editing the look adopts the repo as
      // a real project, carrying the path and the newly picked fields.
      QJsonObject params =
          BuildProjectCreateParams(label, QStringList{path}, path, QString(),
                                   false, next_icon, next_color);
      params.insert(QStringLiteral("profile"), profile);
      supervisor_.RequestRpc(index, QStringLiteral("projects.create"), params,
                             done);
    } else {
      QJsonObject params = BuildProjectAppearanceParams(next_icon, next_color);
      params.insert(QStringLiteral("id"), project_id);
      params.insert(QStringLiteral("profile"), profile);
      supervisor_.RequestRpc(index, QStringLiteral("projects.update"), params,
                             done);
    }
  };
  dialog.exec();
}

void MainWindow::DismissAutoProject(const QString& project_id) {
  if (project_id.isEmpty() ||
      dismissed_auto_project_ids_.contains(project_id)) {
    return;
  }
  dismissed_auto_project_ids_.push_back(project_id);
  SetNativePref(QStringLiteral("native.dismissed_auto_projects"),
                EncodeDismissedAutoProjectIds(dismissed_auto_project_ids_));
  if (project_scope_ == project_id) {
    project_scope_ = AllProjectsId();
  }
  ApplyProjectState();
  status_bar_->ShowTransient(
      QStringLiteral("Project hidden - restore it from the command palette"));
}

void MainWindow::RestoreHiddenProjects() {
  if (dismissed_auto_project_ids_.isEmpty()) {
    return;
  }
  dismissed_auto_project_ids_.clear();
  SetNativePref(QStringLiteral("native.dismissed_auto_projects"),
                EncodeDismissedAutoProjectIds({}));
  ApplyProjectState();
  status_bar_->ShowTransient(QStringLiteral("Hidden projects restored"));
}

void MainWindow::UpdateStatus() {
  const auto snapshot = supervisor_.ActiveSnapshot();
  const QString label = snapshot ? snapshot->label : QStringLiteral("default");
  const QString switch_text =
      last_switch_ms_ > 0.0
          ? QStringLiteral("switch %1 ms").arg(last_switch_ms_, 0, 'f', 2)
          : QStringLiteral("switch ready");
  const bool healthy =
      snapshot && snapshot->connected && snapshot->error_text.isEmpty();
  QString right = QStringLiteral("%1 - gateway %2")
                      .arg(label, supervisor_.GatewayVersion());
  if (snapshot && !snapshot->error_text.isEmpty()) {
    right = QStringLiteral("error: %1").arg(snapshot->error_text);
  }
  QString usage;
  if (snapshot != nullptr && snapshot->usage.context_max > 0) {
    const auto compact = [](qint64 value) {
      if (value >= 1000000) {
        return QStringLiteral("%1m").arg(value / 1000000.0, 0, 'f', 1);
      }
      if (value >= 1000) {
        return QStringLiteral("%1k").arg(value / 1000.0, 0, 'f',
                                         value < 10000 ? 1 : 0);
      }
      return QString::number(value);
    };
    const qint64 percent = std::min<qint64>(
        100,
        (snapshot->usage.context_used * 100 + snapshot->usage.context_max / 2) /
            snapshot->usage.context_max);
    usage = QStringLiteral("%1 / %2 · %3%%")
                .arg(compact(snapshot->usage.context_used),
                     compact(snapshot->usage.context_max))
                .arg(percent);
    if (!pinned_context_.empty()) {
      usage = QStringLiteral("%1 pinned · %2")
                  .arg(pinned_context_.size())
                  .arg(usage);
    }
  }
  status_bar_->SetUsage(usage);
  status_bar_->SetState(
      QStringLiteral("%1 profiles warm").arg(supervisor_.profile_count()),
      switch_text, right, healthy);
}

void MainWindow::RunPageBackground(
    std::function<BackgroundResult()> work,
    std::function<void(const QJsonObject&, const QString&)> done) {
  const Supervisor::RequestScope scope = supervisor_.CaptureRequestScope();
  RunBackground(
      this,
      [scope, work = std::move(work)]() {
        Supervisor::SetWorkerScope(scope);
        BackgroundResult result = work();
        Supervisor::ClearWorkerScope();
        return result;
      },
      [done = std::move(done)](const BackgroundResult& result) {
        if (done) {
          done(result.data, result.error);
        }
      });
}

void MainWindow::ApplyGatewayEvents(const ProfileSnapshot& snapshot) {
  if (snapshot.gateway_notice_serial < last_notice_serial_) {
    last_notice_serial_ = snapshot.gateway_notice_serial;
  } else if (snapshot.gateway_notice_serial > last_notice_serial_) {
    const GatewayNotice* latest = nullptr;
    for (const GatewayNotice& notice : snapshot.gateway_notices) {
      if (notice.serial > last_notice_serial_ &&
          (latest == nullptr || notice.serial >= latest->serial)) {
        latest = &notice;
      }
    }
    if (latest != nullptr) {
      // Agent notices are session activity, not OS-level alerts; keep them in
      // the app (status bar + Context pane) instead of a tray toast.
      status_bar_->ShowTransient(GatewayNoticeLine(*latest));
    }
    last_notice_serial_ = snapshot.gateway_notice_serial;
  }
  if (snapshot.reaction_serial < last_reaction_serial_) {
    last_reaction_serial_ = snapshot.reaction_serial;
  } else if (snapshot.reaction_serial > last_reaction_serial_) {
    last_reaction_serial_ = snapshot.reaction_serial;
    if (snapshot.reaction_kind == QStringLiteral("vibe")) {
      status_bar_->ShowTransient(QStringLiteral("Hermes sent a vibe 💜"));
    }
  }
}

void MainWindow::UpdateComposerHistory(const ProfileSnapshot& snapshot) {
  const ChatMessage* last_user = nullptr;
  for (auto it = snapshot.messages.rbegin(); it != snapshot.messages.rend();
       ++it) {
    if (it->role == MessageRole::kUser) {
      last_user = &*it;
      break;
    }
  }
  QString tail;
  if (last_user != nullptr) {
    for (const MessageBlock& block : last_user->blocks) {
      if (block.kind == BlockKind::kTool) {
        continue;
      }
      if (!tail.isEmpty()) {
        tail += QLatin1Char('\n');
      }
      tail += block.text;
    }
    if (tail.isEmpty()) {
      tail = last_user->raw_text;
    }
  }
  const qint64 last_row = last_user == nullptr ? -1 : last_user->row_id;
  if (composer_history_ready_ &&
      snapshot.selected_session_id == composer_history_session_ &&
      snapshot.messages.size() == composer_history_message_count_ &&
      last_row == composer_history_last_row_ &&
      tail == composer_history_tail_) {
    return;
  }
  composer_history_ready_ = true;
  composer_history_session_ = snapshot.selected_session_id;
  composer_history_message_count_ = snapshot.messages.size();
  composer_history_last_row_ = last_row;
  composer_history_tail_ = tail;
  composer_->SetHistory(ComposerHistory(snapshot));
}

void MainWindow::ApplyVoiceState(const ProfileSnapshot& snapshot) {
  composer_->SetVoiceState(snapshot.voice_recording, snapshot.voice_partial,
                           snapshot.voice_status);
  QString activity;
  if (!snapshot.voice_partial.trimmed().isEmpty()) {
    activity = snapshot.voice_partial.trimmed();
  } else if (snapshot.voice_recording) {
    activity = QStringLiteral("Listening…");
  } else if (!snapshot.voice_status.isEmpty() &&
             snapshot.voice_status != QStringLiteral("idle")) {
    activity = snapshot.voice_status;
  }
  status_bar_->SetActivity(activity);
  composer_->SetPinnedContextCount(static_cast<int>(pinned_context_.size()));
  if (snapshot.voice_transcript_serial < last_voice_transcript_serial_) {
    // Profile switch or worker restart: adopt the new counter without
    // replaying an old transcript into the composer.
    last_voice_transcript_serial_ = snapshot.voice_transcript_serial;
    return;
  }
  if (snapshot.voice_transcript_serial == last_voice_transcript_serial_) {
    return;
  }
  last_voice_transcript_serial_ = snapshot.voice_transcript_serial;
  const QString transcript = snapshot.voice_transcript.trimmed();
  if (transcript.isEmpty()) {
    return;
  }
  QString draft = composer_->Text();
  if (!draft.isEmpty() && !draft.endsWith(QLatin1Char(' ')) &&
      !draft.endsWith(QLatin1Char('\n'))) {
    draft += QLatin1Char(' ');
  }
  composer_->SetText(draft + transcript);
  composer_->FocusEditor();
  status_bar_->ShowTransient(
      QStringLiteral("Voice transcript inserted (%1 characters)")
          .arg(transcript.size()));
}

void MainWindow::CheckProfileCompletions() {
  const int count = supervisor_.profile_count();
  if (count <= 0) {
    return;
  }
  if (static_cast<int>(last_running_.size()) < count) {
    last_running_.resize(static_cast<std::size_t>(count), false);
  }
  for (int index = 0; index < count; ++index) {
    const std::shared_ptr<const ProfileSnapshot> snapshot =
        supervisor_.SnapshotAt(index);
    if (!snapshot) {
      continue;
    }
    const bool was_running = last_running_[static_cast<std::size_t>(index)];
    const bool streaming = !snapshot->messages.empty() &&
                           (snapshot->messages.back().streaming ||
                            snapshot->messages.back().reasoning_streaming);
    const bool running = snapshot->running || streaming;
    if (running && !was_running) {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      run_started_by_profile_[snapshot->name] = now;
      running_session_by_profile_[snapshot->name] =
          snapshot->running_session_id;
    } else if (!running && was_running) {
      const QString profile = snapshot->name;
      const qint64 started = run_started_by_profile_.count(profile) > 0
                                 ? run_started_by_profile_[profile]
                                 : 0;
      const qint64 elapsed =
          started > 0 ? QDateTime::currentMSecsSinceEpoch() - started : 0;
      const QString finished_session =
          running_session_by_profile_.count(profile) > 0
              ? running_session_by_profile_[profile]
              : snapshot->selected_session_id;
      OnGenerationFinished(*snapshot, elapsed, finished_session, index);
      run_started_by_profile_.erase(profile);
      running_session_by_profile_.erase(profile);
      if (index == supervisor_.active_index()) {
        run_started_ms_ = 0;
      }
    }
    last_running_[static_cast<std::size_t>(index)] = running;
  }
}

void MainWindow::ApplyRunIndicators(const ProfileSnapshot& snapshot) {
  // A profile worker can keep running a turn for a chat the user is no longer
  // looking at. Only mirror "generating" (and its elapsed timer) when the
  // running turn belongs to the selected session.
  const bool selected_running = SelectedTurnRunning(snapshot);
  const bool selected_streaming =
      !snapshot.messages.empty() &&
      (snapshot.messages.back().streaming ||
       snapshot.messages.back().reasoning_streaming);
  const qint64 started = run_started_by_profile_.count(snapshot.name) > 0
                             ? run_started_by_profile_[snapshot.name]
                             : 0;
  composer_->SetRunning(selected_running, started);
  SyncComposerQueueBadge();
  const bool running = snapshot.running || selected_streaming;
  QString running_session_id;
  if (snapshot.running && !snapshot.running_session_id.isEmpty()) {
    running_session_id = snapshot.running_session_id;
  } else if (running) {
    running_session_id = snapshot.selected_session_id;
  }
  sidebar_->SetRunning(running, running_session_id);
  status_bar_->SetWorkState(selected_running, started,
                            PendingQueueCount(snapshot));

  if (snapshot.slash_catalog_serial != last_slash_catalog_serial_ &&
      !snapshot.slash_commands.empty()) {
    last_slash_catalog_serial_ = snapshot.slash_catalog_serial;
    std::vector<SlashCommand> commands;
    commands.reserve(snapshot.slash_commands.size());
    for (const SlashEntry& entry : snapshot.slash_commands) {
      SlashCommand command;
      command.name = entry.name;
      command.aliases = entry.aliases;
      command.description = entry.description;
      // A command with a native fast path keeps the native description: the
      // gateway's /stop kills background processes, ours stops the turn.
      const SlashCommand* native = FindSlashCommand(entry.name);
      if (native != nullptr && !native->description.isEmpty()) {
        command.description = native->description;
      }
      commands.push_back(std::move(command));
    }
    composer_->SetSlashCommands(std::move(commands));
  }
}

void MainWindow::OnGenerationFinished(const ProfileSnapshot& snapshot,
                                      qint64 elapsed_ms,
                                      const QString& finished_session_id,
                                      int profile_index) {
  const QString seconds = QString::number(elapsed_ms / 1000.0, 'f', 1);
  const QString session_id = finished_session_id.isEmpty()
                                 ? snapshot.selected_session_id
                                 : finished_session_id;
  const QString profile_label = profile_index >= 0
                                    ? supervisor_.ProfileLabel(profile_index)
                                    : snapshot.label;
  const bool viewing =
      profile_index == supervisor_.active_index() &&
      session_id == snapshot.selected_session_id && isActiveWindow() &&
      !isMinimized() &&
      !(settings_view_ != nullptr && settings_view_->IsOpen()) &&
      !(pages_view_ != nullptr && pages_view_->isVisible());
  const NotificationPrefs prefs = LoadNotificationPrefs();
  if (completion_sound_enabled_ && !prefs.mute_sounds) {
#ifdef HERMES_HAS_MULTIMEDIA
    if (completion_sound_ != nullptr) {
      completion_sound_->play();
    } else {
      QApplication::beep();
    }
#else
    QApplication::beep();
#endif
  }
  if (completion_flash_enabled_) {
    title_bar_->Flash();
  }
  const NotificationKind kind = snapshot.error_text.isEmpty()
                                    ? NotificationKind::kTurnDone
                                    : NotificationKind::kTurnError;
  const QString session_name = SessionNotificationName(snapshot, session_id);
  const QString throttle_key = NotificationKindId(kind) + QLatin1Char(':') +
                               snapshot.name + QLatin1Char(':') + session_id;
  // Treat a different chat/profile as background even while the window is
  // focused: that is exactly when the old behaviour silently swallowed the
  // completion until the user clicked back.
  const bool backgrounded = !viewing;
  if (notifications_ != nullptr &&
      ShouldDeliverNotification(kind, prefs, backgrounded,
                                !session_id.isEmpty(), false) &&
      notification_throttle_.Allow(throttle_key,
                                   QDateTime::currentMSecsSinceEpoch())) {
    const QString title =
        kind == NotificationKind::kTurnError
            ? QStringLiteral("Turn failed · %1").arg(profile_label)
            : QStringLiteral("Turn complete · %1").arg(profile_label);
    const QString body =
        kind == NotificationKind::kTurnError
            ? QStringLiteral("%1: %2").arg(session_name,
                                           snapshot.error_text.left(180))
            : QStringLiteral("%1 finished in %2s").arg(session_name, seconds);
    QString error;
    notifications_->Show(title, body, prefs.mute_sounds, &error);
  }
  status_bar_->ShowTransient(QStringLiteral("%1 · %2 finished in %3s")
                                 .arg(profile_label, session_name, seconds));
}

void MainWindow::NotifyPendingRequests(const ProfileSnapshot& snapshot) {
  const NotificationPrefs prefs = LoadNotificationPrefs();
  const bool backgrounded = !isActiveWindow() || isMinimized();
  QSet<QString> live;
  for (const PendingRequest& request : snapshot.requests) {
    if (request.id.isEmpty()) {
      continue;
    }
    live.insert(request.id);
    if (notified_requests_.contains(request.id)) {
      continue;
    }
    notified_requests_.insert(request.id);

    NotificationKind kind = NotificationKind::kInput;
    if (request.method == QStringLiteral("approval")) {
      kind = NotificationKind::kApproval;
    } else if (request.method != QStringLiteral("clarify") &&
               request.method != QStringLiteral("sudo") &&
               request.method != QStringLiteral("secret") &&
               !request.method.startsWith(QStringLiteral("vault."))) {
      continue;
    }

    const bool session_active =
        request.session_id.isEmpty() ||
        request.session_id == snapshot.selected_session_id;
    const QString throttle_key =
        NotificationKindId(kind) + QLatin1Char(':') + request.session_id;
    if (notifications_ == nullptr ||
        !ShouldDeliverNotification(kind, prefs, backgrounded, session_active,
                                   false) ||
        !notification_throttle_.Allow(throttle_key,
                                      QDateTime::currentMSecsSinceEpoch())) {
      continue;
    }
    const QString session =
        SessionNotificationName(snapshot, request.session_id);
    const QString title =
        kind == NotificationKind::kApproval
            ? QStringLiteral("Approval needed — %1").arg(session)
            : QStringLiteral("Input needed — %1").arg(session);
    QString body = request.detail.trimmed();
    if (body.isEmpty()) {
      body = request.title.trimmed();
    }
    QString error;
    notifications_->Show(title, body, prefs.mute_sounds, &error);
  }
  notified_requests_.intersect(live);
}

void MainWindow::UpdateRequest(const ProfileSnapshot& snapshot) {
  const PendingRequest* visible = nullptr;
  for (const PendingRequest& request : snapshot.requests) {
    // A request belongs to the session that raised it. A background session's
    // card waits in the worker until the user switches back to that chat.
    if (request.session_id.isEmpty() ||
        request.session_id == snapshot.selected_session_id) {
      visible = &request;
      break;
    }
  }
  if (visible == nullptr) {
    chat_view_->ClearRequest();
    return;
  }
  chat_view_->SetRequest(*visible);
  if (visible->id == auto_answered_id_) {
    return;
  }

  const int profile_index = supervisor_.active_index();
  if (auto_approve_ && visible->method == QStringLiteral("approval")) {
    auto_answered_id_ = visible->id;
    const QString request_id = visible->id;
    QTimer::singleShot(700, this, [this, profile_index, request_id]() {
      QJsonObject result;
      result.insert(QStringLiteral("choice"), QStringLiteral("once"));
      supervisor_.RespondRequest(profile_index, request_id, result);
    });
    return;
  }

  if (!auto_clarify_.isEmpty() &&
      visible->method == QStringLiteral("clarify")) {
    auto_answered_id_ = visible->id;
    const QString request_id = visible->id;
    QJsonObject answers;
    for (const ClarifyQuestion& question : visible->questions) {
      QString answer = auto_clarify_;
      if (answer.isEmpty() && !question.choices.isEmpty()) {
        answer = question.choices.front();
        const QString suffix = QStringLiteral(" (Recommended)");
        if (answer.endsWith(suffix)) {
          answer = answer.left(answer.size() - suffix.size()).trimmed();
        }
      }
      if (answer.isEmpty()) {
        answers.insert(question.qid, QJsonValue::Null);
        continue;
      }
      if (question.multi_select) {
        QJsonArray array;
        array.push_back(answer);
        answers.insert(question.qid,
                       QString::fromUtf8(QJsonDocument(array).toJson(
                           QJsonDocument::Compact)));
      } else {
        answers.insert(question.qid, answer);
      }
    }
    if (answers.isEmpty()) {
      return;
    }
    QJsonObject result;
    result.insert(QStringLiteral("answers"), answers);
    result.insert(QStringLiteral("outcome"), QStringLiteral("submitted"));
    QTimer::singleShot(500, this, [this, profile_index, request_id, result]() {
      supervisor_.RespondRequest(profile_index, request_id, result);
    });
  }
}

void MainWindow::BenchSwitch(int iterations) {
  if (iterations <= 0 || supervisor_.profile_count() < 2) {
    return;
  }
  std::vector<double> samples;
  samples.reserve(static_cast<std::size_t>(iterations));
  for (int i = 0; i < iterations; ++i) {
    const int next =
        (supervisor_.active_index() + 1) % supervisor_.profile_count();
    QElapsedTimer timer;
    timer.start();
    supervisor_.Activate(next);
    chat_view_->SetSnapshot(supervisor_.ActiveSnapshot());
    chat_view_->repaint();
    samples.push_back(static_cast<double>(timer.nsecsElapsed()) / 1000.0);
  }
  std::sort(samples.begin(), samples.end());
  double sum = 0.0;
  for (double sample : samples) {
    sum += sample;
  }
  const double mean = sum / static_cast<double>(samples.size());
  const double p50 = samples[samples.size() / 2];
  const double p95 =
      samples[std::min(samples.size() - 1, samples.size() * 95 / 100)];
  qInfo("bench switches=%d mean_us=%.1f p50_us=%.1f p95_us=%.1f max_us=%.1f",
        iterations, mean, p50, p95, samples.back());
}

QString MainWindow::ActiveProfileLabel() const {
  const auto snapshot = supervisor_.ActiveSnapshot();
  return snapshot ? snapshot->label : QString();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  PersistPaneLayout();
  supervisor_.Stop();
  QWidget::closeEvent(event);
}

void MainWindow::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), theme_.background);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (!pane_layout_ready_ && body_splitter_ != nullptr &&
      body_splitter_->width() > 0) {
    pane_layout_ready_ = true;
    ApplyPaneLayout(true);
  }
  if (command_palette_ != nullptr) {
    command_palette_->setGeometry(rect());
  }
  if (settings_view_ != nullptr) {
    settings_view_->setGeometry(rect());
  }
}

}  // namespace hermes::ui
