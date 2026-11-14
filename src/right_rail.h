// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_RIGHT_RAIL_H_
#define HERMES_UI_RIGHT_RAIL_H_

#include <QDateTime>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QWidget>
#include <functional>
#include <map>
#include <vector>

#include "src/artifacts.h"
#include "src/model.h"
#include "src/projects.h"
#include "src/terminal_widget.h"
#include "src/theme.h"

class QFileSystemModel;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QSvgWidget;
class QTextBrowser;
class QTimer;
class QTreeView;
class QVBoxLayout;

namespace hermes::ui {

class AgentHeaderButton;

// One artifact-ish block lifted out of the transcript, newest first. Generated
// HTML/SVG/code records carry their version history; file cards (`:::artifact`
// and images) carry a path for the workspace preview instead.
struct SessionArtifact {
  QString label;
  QString value;
  QString kind;
  QString id;
  QString language;
  bool generated = false;
  int version_index = 0;
  std::vector<ArtifactVersion> versions;
};

std::vector<SessionArtifact> CollectSessionArtifacts(
    const std::vector<ChatMessage>& messages);
QString CompactUsageTokens(qint64 value);

// One user prompt in the transcript's timeline. The gateway's
// `/api/sessions/{id}/timeline` returns the same row_id/preview/prompt shape;
// the native rail derives it from the loaded transcript so it stays useful
// offline and needs no second read.
struct TimelineEntry {
  qint64 row_id = 0;
  QString preview;
  QDateTime timestamp;
  // Full simplified prompt for the wrapped hover tooltip; `preview` stays
  // bounded so one enormous prompt cannot make the timeline unreadable.
  QString full_text;
};
std::vector<TimelineEntry> CollectTimelineEntries(
    const std::vector<ChatMessage>& messages);

// Chat right rail: project files/preview, Git ship actions, the embedded
// terminal, context accounting, delegated agents and session outputs. Only the
// Git and terminal sections mutate state; transcript mutations stay in the
// transcript or their owning workspace page.
class RightRail : public QWidget {
  Q_OBJECT

 public:
  explicit RightRail(const Theme& theme, QWidget* parent = nullptr);
  ~RightRail() override;

  void SetTheme(const Theme& theme);
  void SetSnapshot(const ProfileSnapshot& snapshot);
  void SetCollapsed(bool collapsed);
  bool IsCollapsed() const { return collapsed_; }
  // The project rail follows the active project scope. `root` is a local
  // directory; the project and git panes never infer it from chat messages.
  // `repositories` is the project's full resolved repo list from
  // `projects.tree`; an empty list falls back to `root` when it is a worktree.
  void SetProjectContext(const QString& project_id, const QString& label,
                         const QString& root,
                         const std::vector<ProjectRepoInfo>& repositories = {});
  void SetVisibleForTest(bool visible) { setVisible(visible); }
  // Accordion sections replace the old tab strip. Expanding is idempotent and
  // scrolls the section into view; the collapsed bar uses the same path.
  void ExpandSection(const QString& id);
  // Test/probe hook: run a shell command in the embedded terminal and let the
  // normal PTY path render it.
  void RunTerminalCommandForTest(const QString& command);

  std::function<void(const SessionArtifact& artifact)> on_artifact_open;
  std::function<void(const SessionArtifact& artifact,
                     const QString& suggested_name)>
      on_artifact_save;
  std::function<void(qint64 row_id)> on_timeline_open;
  // Commit-message generation goes through the gateway's stateless one-shot
  // LLM path. The shell owns the RPC because the rail has no Supervisor.
  using GenerateCommitMessageCallback = std::function<void(
      const QString& diff, const QString& recent_commits, const QString& avoid,
      std::function<void(const QString& text, const QString& error)> done)>;
  GenerateCommitMessageCallback on_generate_commit_message;
  // Fired only when the user clicks the rail's own collapse/expand controls;
  // the shell then persists the pane width.
  std::function<void(bool collapsed)> on_collapsed_changed;
  // Live delegated children can be steered or stopped from the rail. Keep the
  // callbacks on the rail so the UI updates in place instead of rebuilding the
  // agent cards (and stealing focus from the steer field) on every snapshot.
  using SubagentControlDone =
      std::function<void(const QJsonObject&, const QString&)>;
  std::function<void(const QString& session_id, const QString& subagent_id,
                     const QString& text, SubagentControlDone done)>
      on_steer_subagent;
  std::function<void(const QString& session_id, const QString& subagent_id,
                     SubagentControlDone done)>
      on_interrupt_subagent;

 private:
  struct GitRepoPane;
  void RebuildContext(const ProfileSnapshot& snapshot);
  void RebuildAgents(const ProfileSnapshot& snapshot);
  void RebuildTodos(const ProfileSnapshot& snapshot);
  void RebuildTimeline(const ProfileSnapshot& snapshot);
  void RebuildTimelineRows();
  void RebuildArtifacts(const ProfileSnapshot& snapshot);
  void RestoreArtifactScroll(int pane_value, int document_value,
                             quint64 generation);
  void ShowArtifactPreview(const SessionArtifact& artifact);
  void ShowArtifactVersion(int index);
  void RenderArtifactPreview();
  void CloseArtifactPreview();
  void SyncProjectViews();
  void RebuildGitRepositories(const std::vector<ProjectRepoInfo>& repositories);
  void RefreshGitStatuses();
  void RefreshGitStatus(const QString& path, quint64 serial);
  void RebuildGitChanges(GitRepoPane* repository, const QString& status_output);
  void SetGitRepoExpanded(GitRepoPane* repository, bool expanded);
  void RefreshGitignore(GitRepoPane* repository);
  void RunGit(const QString& root, const QStringList& arguments,
              std::function<void(bool ok, const QString& output)> done);
  void StageGitPaths(const QString& path, quint64 serial,
                     const QStringList& paths, bool stage);
  void UnstageGitPaths(const QString& path, quint64 serial,
                       const QStringList& paths, bool all);
  void StageAllGit(const QString& path, quint64 serial, bool stage);
  void AddGitignorePattern(const QString& path, quint64 serial,
                           const QString& pattern);
  void RemoveGitignorePattern(const QString& path, quint64 serial,
                              const QString& pattern);
  void ShowGitDiff(const QString& path, quint64 serial,
                   const QStringList& paths, const QString& display_path,
                   bool staged, bool untracked);
  void CommitStagedGit(
      const QString& path, quint64 serial, const QString& message,
      std::function<void(bool ok, const QString& output)> done);
  void CommitGit(const QString& path, quint64 serial);
  void PushGit(const QString& path, quint64 serial);
  void GenerateCommitMessage(const QString& path, quint64 serial);
  void GatherCommitContext(
      const QString& root,
      std::function<void(bool ok, const QString& diff,
                         const QString& recent_commits, const QString& error)>
          done);
  void FinishCommitGeneration(const QString& path, quint64 serial);
  struct GitRepoPane {
    QString id;
    QString label;
    QString path;
    QString suffix;
    quint64 serial = 0;
    QWidget* card = nullptr;
    QPushButton* header = nullptr;
    QWidget* body = nullptr;
    QLabel* root = nullptr;
    QLabel* branch = nullptr;
    QLabel* status = nullptr;
    QWidget* changes_host = nullptr;
    QVBoxLayout* changes_layout = nullptr;
    QWidget* diff_host = nullptr;
    QLabel* diff_title = nullptr;
    QPlainTextEdit* diff_view = nullptr;
    QPushButton* diff_close = nullptr;
    quint64 diff_generation = 0;
    QLineEdit* ignore_input = nullptr;
    QPushButton* ignore_add = nullptr;
    QWidget* ignore_host = nullptr;
    QVBoxLayout* ignore_layout = nullptr;
    QPlainTextEdit* message = nullptr;
    QLabel* message_status = nullptr;
    QPushButton* refresh = nullptr;
    QPushButton* generate = nullptr;
    QPushButton* commit = nullptr;
    QPushButton* push = nullptr;
    bool ai_generating = false;
    quint64 ai_generation = 0;
    bool expanded = true;
  };
  GitRepoPane* FindGitRepository(const QString& path, quint64 serial = 0);
  void EnsureTerminal();
  void RestartTerminal();
  void ShowProjectPreview(const QString& path);

  struct Section {
    QString id;
    QString title;
    QPushButton* header = nullptr;
    QWidget* content = nullptr;
  };
  void AddSection(const QString& id, const QString& title, QWidget* content);
  void SetSectionExpanded(const QString& id, bool expanded);
  void SetSectionTitle(const QString& id, const QString& title);
  Section* FindSection(const QString& id);

  struct AgentCard {
    QString id;
    QString owner_session_id;
    QWidget* card = nullptr;
    AgentHeaderButton* header = nullptr;
    QWidget* details = nullptr;
    QLabel* meta = nullptr;
    QWidget* stream_host = nullptr;
    QVBoxLayout* stream_layout = nullptr;
    QLabel* feedback = nullptr;
    QWidget* controls = nullptr;
    QLineEdit* steer_input = nullptr;
    QPushButton* steer = nullptr;
    QPushButton* stop = nullptr;
    QString stream_signature;
    bool expanded = false;
  };
  AgentCard* FindAgentCard(const QString& id);
  void CreateAgentCard(const QString& id);
  void UpdateAgentCard(const SubagentProgress& agent, int depth,
                       const ProfileSnapshot& snapshot, qint64 now_ms);
  void SetAgentExpanded(const QString& id, bool expanded);
  void SteerAgent(const QString& id);
  void InterruptAgent(const QString& id);

  Theme theme_;
  QWidget* workspace_ = nullptr;
  QWidget* collapsed_bar_ = nullptr;
  QScrollArea* sections_scroll_ = nullptr;
  QWidget* sections_host_ = nullptr;
  QVBoxLayout* sections_layout_ = nullptr;
  std::vector<Section> sections_;
  QWidget* project_pane_ = nullptr;
  QLabel* project_root_label_ = nullptr;
  QFileSystemModel* project_model_ = nullptr;
  QTreeView* project_tree_ = nullptr;
  QTextBrowser* project_preview_ = nullptr;
  QString project_preview_path_;
  QWidget* git_pane_ = nullptr;
  QLabel* git_root_label_ = nullptr;
  QWidget* git_repositories_host_ = nullptr;
  QVBoxLayout* git_repositories_layout_ = nullptr;
  QLabel* git_empty_ = nullptr;
  std::vector<GitRepoPane> git_repositories_;
  QWidget* terminal_pane_ = nullptr;
  TerminalWidget* terminal_ = nullptr;
  QPushButton* terminal_copy_ = nullptr;
  QPushButton* terminal_paste_ = nullptr;
  QPushButton* terminal_clear_ = nullptr;
  QPushButton* terminal_restart_ = nullptr;
  QString project_id_;
  QString project_label_;
  QString project_root_;
  std::vector<ProjectRepoInfo> project_repositories_;
  quint64 git_repository_serial_ = 0;
  bool collapsed_ = false;
  QWidget* context_pane_ = nullptr;
  QWidget* agents_pane_ = nullptr;
  QWidget* todos_pane_ = nullptr;
  QWidget* timeline_pane_ = nullptr;
  QWidget* artifacts_pane_ = nullptr;
  QLabel* context_summary_ = nullptr;
  QProgressBar* context_bar_ = nullptr;
  QLabel* context_detail_ = nullptr;
  QLabel* context_control_ = nullptr;
  QLabel* context_notice_ = nullptr;
  QLabel* context_session_ = nullptr;
  QLabel* agents_empty_ = nullptr;
  QWidget* agents_list_ = nullptr;
  std::map<QString, AgentCard> agent_cards_;
  QSet<QString> expanded_agents_;
  QSet<QString> announced_agents_;
  QSet<QString> collapsed_git_repo_paths_;
  QLabel* todos_empty_ = nullptr;
  QLabel* todos_summary_ = nullptr;
  QWidget* todos_list_ = nullptr;
  QLineEdit* timeline_search_ = nullptr;
  QLabel* timeline_empty_ = nullptr;
  QWidget* timeline_list_ = nullptr;
  std::vector<TimelineEntry> timeline_entries_;
  QString todos_signature_;
  QString timeline_render_signature_;
  QLabel* artifacts_empty_ = nullptr;
  QWidget* artifacts_list_ = nullptr;
  QWidget* artifact_preview_ = nullptr;
  QLabel* artifact_preview_title_ = nullptr;
  QLabel* artifact_preview_body_ = nullptr;
  QTextBrowser* artifact_preview_document_ = nullptr;
  QSvgWidget* artifact_preview_svg_ = nullptr;
  QPushButton* artifact_version_prev_ = nullptr;
  QLabel* artifact_version_label_ = nullptr;
  QPushButton* artifact_version_next_ = nullptr;
  QPushButton* artifact_preview_copy_ = nullptr;
  QPushButton* artifact_preview_save_ = nullptr;
  QPushButton* artifact_preview_source_ = nullptr;
  QPushButton* artifact_preview_browser_ = nullptr;
  QPushButton* artifact_preview_open_ = nullptr;
  SessionArtifact preview_artifact_;
  QString artifact_home_;
  bool artifact_show_source_ = false;
  quint64 artifact_scroll_generation_ = 0;
  bool artifacts_rendered_ = false;
  QString artifact_render_signature_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_RIGHT_RAIL_H_
