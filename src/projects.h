// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROJECTS_H_
#define HERMES_UI_PROJECTS_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace hermes::ui {

// Projects are per-profile multi-folder workspaces served by the gateway's
// `projects.*` JSON-RPC surface. The native sidebar renders `projects.tree`
// and the management dialogs read/write `projects.list` + the mutator verbs.
inline QString AllProjectsId() { return QStringLiteral("__all_projects__"); }

struct ProjectFolderInfo {
  QString path;
  QString label;
  bool is_primary = false;
};

// One `repo` row from `projects.tree`. The gateway resolves declared project
// folders and session working directories to repository roots before the tree
// is emitted, so this is the authoritative list of repositories for a project.
struct ProjectRepoInfo {
  QString id;
  QString label;
  QString path;
};

struct ProjectInfo {
  QString id;
  QString slug;
  QString name;
  QString description;
  QString icon;
  QString color;
  QString board_slug;
  QString primary_path;
  bool archived = false;
  qint64 created_at = 0;
  std::vector<ProjectFolderInfo> folders;
};

// The sidebar's project -> repo -> lane tree is flattened here: the native
// sidebar only needs the project identity, counts and the authoritative
// session id set. Repo/lane details stay available to future panes through
// the raw JSON in the gateway response.
struct ProjectNode {
  QString id;
  QString label;
  QString path;
  QString color;
  QString icon;
  bool is_auto = false;
  bool is_no_project = false;
  int session_count = 0;
  double last_active = 0.0;
  qint64 total_tokens = 0;
  double total_cost_usd = 0.0;
  int repo_count = 0;
  std::vector<ProjectRepoInfo> repos;
  // Every session id the backend assigned to this project. This is the
  // authoritative set the sidebar filters with; previewSessions alone is
  // intentionally truncated.
  QStringList session_ids;
};

struct ProjectsState {
  bool loaded = false;
  bool loading = false;
  bool available = true;
  QString error;
  QString profile;
  QString active_id;
  std::vector<ProjectInfo> projects;
  std::vector<ProjectNode> tree;
};

// Parsers are pure so the worker/UI handoff can be tested without a gateway.
bool ParseProjectsList(const QJsonObject& payload, ProjectsState* state,
                       QString* error = nullptr);
bool ParseProjectsTree(const QJsonObject& payload, ProjectsState* state,
                       QString* error = nullptr);
void ParseProjectInfo(const QJsonObject& row, ProjectInfo* project);
void ParseProjectNode(const QJsonObject& row, ProjectNode* node);

// Request builders shared by the create/edit dialogs and the sidebar actions.
QJsonObject BuildProjectCreateParams(const QString& name,
                                     const QStringList& folders,
                                     const QString& primary_path,
                                     const QString& description, bool use,
                                     const QString& icon = QString(),
                                     const QString& color = QString());
QJsonObject BuildProjectUpdateParams(const QString& name,
                                     const QString& description);
// `icon` / `color` are always present: '' clears the stored look.
QJsonObject BuildProjectAppearanceParams(const QString& icon,
                                         const QString& color);
QJsonObject BuildProjectFolderParams(const QString& id, const QString& path,
                                     bool is_primary = false);
QJsonObject BuildProjectIdParams(const QString& id);

// Auto-derived projects can be hidden from the sidebar without a gateway
// delete. The dismissed id set is a local layout preference (paths survive
// reconnects); explicit rows never match.
std::vector<ProjectNode> FilterVisibleProjects(
    const std::vector<ProjectNode>& projects, const QStringList& dismissed_ids);
QString EncodeDismissedAutoProjectIds(const QStringList& ids);
QStringList DecodeDismissedAutoProjectIds(const QString& encoded);

// The folder a new chat should start in for the selected project scope. The
// gateway tree's resolved path wins; `projects.list` is the fallback while the
// tree is still loading. Unknown/All-projects scopes return an empty cwd.
QString ProjectWorkingDirectory(const ProjectsState& state,
                                const QString& project_id);

// True when the fetched project tree assigns this session to a real project.
// The synthetic Home bucket also lists unclaimed sessions, so it cannot
// invalidate a local `session.create` project tag; otherwise a fresh chat would
// vanish from its project until the backend finished claiming it.
bool ProjectTreeContainsSession(const std::vector<ProjectNode>& tree,
                                const QString& session_id);

// What entering a project should do to the active conversation.
enum class ProjectEntryAction {
  kKeepCurrent,
  kSelectSession,
  kStartSession,
};

struct ProjectEntryDecision {
  ProjectEntryAction action = ProjectEntryAction::kKeepCurrent;
  QString session_id;
};

// Decide how project entry should change the active conversation. The current
// session stays when the project already owns it (including a local placeholder
// tagged with `current_project_id`). Otherwise the first matching row in
// `sessions_by_recency` is selected, then the tree's own id order. A project
// with no known sessions asks the caller to start a fresh chat; a tree row that
// claims history but publishes no ids keeps the current chat rather than
// inventing a replacement.
ProjectEntryDecision ResolveProjectEntry(const ProjectsState& state,
                                         const QString& project_id,
                                         const QStringList& sessions_by_recency,
                                         const QString& current_session_id,
                                         const QString& current_project_id);

// Default folder for the workspace file browser: the active project's
// working directory, or the user's home when no project is selected.
QString ArtifactsWorkingDirectory(const ProjectsState& state,
                                  const QString& project_id);

// Every repository the gateway assigned to a project. Uses the tree's resolved
// repo roots first, then declared folders/worktree paths as a pre-tree
// fallback. The right rail renders one Git action group per entry.
std::vector<ProjectRepoInfo> ProjectGitRepositories(const ProjectsState& state,
                                                    const QString& project_id);

// Human-friendly one-line detail for a project row/editor.
QString ProjectSummaryLine(const ProjectInfo& project);
QString ProjectFolderLabel(const ProjectFolderInfo& folder);

}  // namespace hermes::ui

#endif  // HERMES_UI_PROJECTS_H_
