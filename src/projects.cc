// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/projects.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>
#include <algorithm>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

void AddSessionId(QStringList* ids, const QString& id) {
  if (!id.isEmpty() && !ids->contains(id)) {
    ids->push_back(id);
  }
}

void AppendSessionIdsFromRows(const QJsonArray& rows, QStringList* ids) {
  for (const QJsonValue& value : rows) {
    AddSessionId(ids, value.toObject().value(QStringLiteral("id")).toString());
  }
}

}  // namespace

bool ParseProjectsList(const QJsonObject& payload, ProjectsState* state,
                       QString* error) {
  if (state == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("No project state target");
    }
    return false;
  }
  if (!payload.value(QStringLiteral("projects")).isArray()) {
    if (error != nullptr) {
      *error = QStringLiteral("projects.list returned no project list");
    }
    return false;
  }
  state->projects.clear();
  const QJsonArray rows = payload.value(QStringLiteral("projects")).toArray();
  state->projects.reserve(static_cast<std::size_t>(rows.size()));
  for (const QJsonValue& value : rows) {
    ProjectInfo project;
    ParseProjectInfo(value.toObject(), &project);
    if (!project.id.isEmpty()) {
      state->projects.push_back(std::move(project));
    }
  }
  state->active_id = payload.value(QStringLiteral("active_id")).toString();
  return true;
}

bool ParseProjectsTree(const QJsonObject& payload, ProjectsState* state,
                       QString* error) {
  if (state == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("No project state target");
    }
    return false;
  }
  if (!payload.value(QStringLiteral("projects")).isArray()) {
    if (error != nullptr) {
      *error = QStringLiteral("projects.tree returned no project tree");
    }
    return false;
  }
  state->tree.clear();
  const QJsonArray rows = payload.value(QStringLiteral("projects")).toArray();
  state->tree.reserve(static_cast<std::size_t>(rows.size()));
  for (const QJsonValue& value : rows) {
    ProjectNode node;
    ParseProjectNode(value.toObject(), &node);
    if (!node.id.isEmpty()) {
      state->tree.push_back(std::move(node));
    }
  }
  // The tree carries the durable active pointer too. `projects.list` remains
  // its primary source, but a tree-only refresh must not erase it.
  const QString active = payload.value(QStringLiteral("active_id")).toString();
  if (!active.isEmpty()) {
    state->active_id = active;
  }
  return true;
}

void ParseProjectInfo(const QJsonObject& row, ProjectInfo* project) {
  if (project == nullptr) {
    return;
  }
  project->id = row.value(QStringLiteral("id")).toString();
  project->slug = row.value(QStringLiteral("slug")).toString();
  project->name = row.value(QStringLiteral("name")).toString();
  project->description = row.value(QStringLiteral("description")).toString();
  project->icon = row.value(QStringLiteral("icon")).toString();
  project->color = row.value(QStringLiteral("color")).toString();
  project->board_slug = row.value(QStringLiteral("board_slug")).toString();
  project->primary_path = row.value(QStringLiteral("primary_path")).toString();
  project->archived = row.value(QStringLiteral("archived")).toBool();
  project->created_at =
      static_cast<qint64>(row.value(QStringLiteral("created_at")).toDouble());
  project->folders.clear();
  const QJsonArray folders = row.value(QStringLiteral("folders")).toArray();
  project->folders.reserve(static_cast<std::size_t>(folders.size()));
  for (const QJsonValue& value : folders) {
    const QJsonObject folder_row = value.toObject();
    ProjectFolderInfo folder;
    folder.path = folder_row.value(QStringLiteral("path")).toString();
    folder.label = folder_row.value(QStringLiteral("label")).toString();
    folder.is_primary = folder_row.value(QStringLiteral("is_primary")).toBool();
    if (!folder.path.isEmpty()) {
      project->folders.push_back(std::move(folder));
    }
  }
  if (project->primary_path.isEmpty()) {
    const auto primary = std::find_if(
        project->folders.begin(), project->folders.end(),
        [](const ProjectFolderInfo& folder) { return folder.is_primary; });
    if (primary != project->folders.end()) {
      project->primary_path = primary->path;
    }
  }
}

void ParseProjectNode(const QJsonObject& row, ProjectNode* node) {
  if (node == nullptr) {
    return;
  }
  node->id = row.value(QStringLiteral("id")).toString();
  node->label = row.value(QStringLiteral("label")).toString();
  if (node->label.isEmpty()) {
    node->label = row.value(QStringLiteral("name")).toString();
  }
  node->path = row.value(QStringLiteral("path")).toString();
  node->color = row.value(QStringLiteral("color")).toString();
  node->icon = row.value(QStringLiteral("icon")).toString();
  node->is_auto = row.value(QStringLiteral("isAuto")).toBool();
  node->is_no_project = row.value(QStringLiteral("isNoProject")).toBool();
  node->session_count = row.value(QStringLiteral("sessionCount")).toInt();
  node->last_active = row.value(QStringLiteral("lastActive")).toDouble();
  node->total_tokens =
      static_cast<qint64>(row.value(QStringLiteral("totalTokens")).toDouble());
  node->total_cost_usd = row.value(QStringLiteral("totalCostUsd")).toDouble();

  QStringList ids;
  for (const QJsonValue& value :
       row.value(QStringLiteral("sessionIds")).toArray()) {
    AddSessionId(&ids, value.toString());
  }
  AppendSessionIdsFromRows(
      row.value(QStringLiteral("previewSessions")).toArray(), &ids);
  const QJsonArray repos = row.value(QStringLiteral("repos")).toArray();
  node->repo_count = repos.size();
  node->repos.clear();
  node->repos.reserve(static_cast<std::size_t>(repos.size()));
  for (const QJsonValue& repo_value : repos) {
    const QJsonObject repo = repo_value.toObject();
    ProjectRepoInfo info;
    info.id = repo.value(QStringLiteral("id")).toString();
    info.label = repo.value(QStringLiteral("label")).toString();
    info.path = repo.value(QStringLiteral("path")).toString();
    if (info.path.isEmpty()) {
      info.path = info.id;
    }
    if (!info.path.isEmpty()) {
      node->repos.push_back(std::move(info));
    }
    AppendSessionIdsFromRows(repo.value(QStringLiteral("sessions")).toArray(),
                             &ids);
    for (const QJsonValue& group_value :
         repo.value(QStringLiteral("groups")).toArray()) {
      AppendSessionIdsFromRows(
          group_value.toObject().value(QStringLiteral("sessions")).toArray(),
          &ids);
    }
  }
  node->session_ids = std::move(ids);
}

QJsonObject BuildProjectCreateParams(const QString& name,
                                     const QStringList& folders,
                                     const QString& primary_path,
                                     const QString& description, bool use,
                                     const QString& icon,
                                     const QString& color) {
  QJsonObject params;
  params.insert(QStringLiteral("name"), name);
  QJsonArray folder_rows;
  for (const QString& folder : folders) {
    const QString path = folder.trimmed();
    if (!path.isEmpty()) {
      folder_rows.push_back(path);
    }
  }
  params.insert(QStringLiteral("folders"), folder_rows);
  if (!primary_path.trimmed().isEmpty()) {
    params.insert(QStringLiteral("primary_path"), primary_path.trimmed());
  }
  if (!description.trimmed().isEmpty()) {
    params.insert(QStringLiteral("description"), description.trimmed());
  }
  if (!icon.trimmed().isEmpty()) {
    params.insert(QStringLiteral("icon"), icon.trimmed());
  }
  if (!color.trimmed().isEmpty()) {
    params.insert(QStringLiteral("color"), color.trimmed());
  }
  params.insert(QStringLiteral("use"), use);
  return params;
}

QJsonObject BuildProjectUpdateParams(const QString& name,
                                     const QString& description) {
  QJsonObject params;
  if (!name.trimmed().isEmpty()) {
    params.insert(QStringLiteral("name"), name.trimmed());
  }
  // The gateway treats an absent key as "leave untouched" and '' as a clear.
  params.insert(QStringLiteral("description"), description.trimmed());
  return params;
}

QJsonObject BuildProjectAppearanceParams(const QString& icon,
                                         const QString& color) {
  QJsonObject params;
  params.insert(QStringLiteral("icon"), icon.trimmed());
  params.insert(QStringLiteral("color"), color.trimmed());
  return params;
}

QJsonObject BuildProjectFolderParams(const QString& id, const QString& path,
                                     bool is_primary) {
  QJsonObject params;
  params.insert(QStringLiteral("id"), id);
  params.insert(QStringLiteral("path"), path);
  if (is_primary) {
    params.insert(QStringLiteral("is_primary"), true);
  }
  return params;
}

QJsonObject BuildProjectIdParams(const QString& id) {
  QJsonObject params;
  params.insert(QStringLiteral("id"), id);
  return params;
}

std::vector<ProjectNode> FilterVisibleProjects(
    const std::vector<ProjectNode>& projects,
    const QStringList& dismissed_ids) {
  if (dismissed_ids.isEmpty()) {
    return projects;
  }
  std::vector<ProjectNode> visible;
  visible.reserve(projects.size());
  for (const ProjectNode& node : projects) {
    if (node.is_auto && dismissed_ids.contains(node.id)) {
      continue;
    }
    visible.push_back(node);
  }
  return visible;
}

QString EncodeDismissedAutoProjectIds(const QStringList& ids) {
  QJsonArray rows;
  for (const QString& id : ids) {
    if (!id.trimmed().isEmpty()) {
      rows.push_back(id);
    }
  }
  return QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}

QStringList DecodeDismissedAutoProjectIds(const QString& encoded) {
  const QJsonDocument document = QJsonDocument::fromJson(encoded.toUtf8());
  QStringList ids;
  for (const QJsonValue& value : document.array()) {
    const QString id = value.toString();
    if (!id.isEmpty() && !ids.contains(id)) {
      ids.push_back(id);
    }
  }
  return ids;
}

QString ProjectSummaryLine(const ProjectInfo& project) {
  const int folders = static_cast<int>(project.folders.size());
  QString line = folders == 1 ? QStringLiteral("1 folder")
                              : QStringLiteral("%1 folders").arg(folders);
  if (!project.primary_path.isEmpty()) {
    line += QStringLiteral(" · ") + project.primary_path;
  }
  if (project.archived) {
    line += QStringLiteral(" · archived");
  }
  return line;
}

QString ProjectFolderLabel(const ProjectFolderInfo& folder) {
  if (!folder.label.trimmed().isEmpty()) {
    return folder.label.trimmed();
  }
  const QString trimmed = folder.path;
  const int slash = std::max(trimmed.lastIndexOf(QLatin1Char('/')),
                             trimmed.lastIndexOf(QLatin1Char('\\')));
  return slash >= 0 ? trimmed.mid(slash + 1) : trimmed;
}

QString ProjectWorkingDirectory(const ProjectsState& state,
                                const QString& project_id) {
  if (project_id.isEmpty() || project_id == AllProjectsId()) {
    return QString();
  }
  const auto node = std::find_if(
      state.tree.begin(), state.tree.end(),
      [&project_id](const ProjectNode& row) { return row.id == project_id; });
  if (node != state.tree.end() && !node->path.isEmpty()) {
    return node->path;
  }
  const auto project = std::find_if(
      state.projects.begin(), state.projects.end(),
      [&project_id](const ProjectInfo& row) { return row.id == project_id; });
  if (project != state.projects.end()) {
    return project->primary_path;
  }
  return QString();
}

QString ArtifactsWorkingDirectory(const ProjectsState& state,
                                  const QString& project_id) {
  const QString project_path = ProjectWorkingDirectory(state, project_id);
  return project_path.isEmpty() ? QDir::homePath() : project_path;
}

namespace {

QString RepoPathKey(const QString& path) {
  return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QString RepoLabelForPath(const QString& path) {
  const QString name = QFileInfo(path).fileName();
  return name.isEmpty() ? path : name;
}

void AppendRepo(std::vector<ProjectRepoInfo>* repos, QSet<QString>* seen,
                const ProjectRepoInfo& repo) {
  const QString trimmed = repo.path.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  const QString key = RepoPathKey(trimmed);
  if (key.isEmpty() || seen->contains(key)) {
    return;
  }
  seen->insert(key);
  ProjectRepoInfo resolved = repo;
  resolved.path = trimmed;
  if (resolved.label.trimmed().isEmpty()) {
    resolved.label = RepoLabelForPath(trimmed);
  }
  repos->push_back(std::move(resolved));
}

}  // namespace

std::vector<ProjectRepoInfo> ProjectGitRepositories(const ProjectsState& state,
                                                    const QString& project_id) {
  std::vector<ProjectRepoInfo> repos;
  QSet<QString> seen;
  if (project_id.isEmpty() || project_id == AllProjectsId()) {
    return repos;
  }

  const auto node = std::find_if(
      state.tree.begin(), state.tree.end(),
      [&project_id](const ProjectNode& row) { return row.id == project_id; });
  if (node != state.tree.end()) {
    for (const ProjectRepoInfo& repo : node->repos) {
      AppendRepo(&repos, &seen, repo);
    }
    if (!repos.empty()) {
      return repos;
    }
  }

  const auto project = std::find_if(
      state.projects.begin(), state.projects.end(),
      [&project_id](const ProjectInfo& row) { return row.id == project_id; });
  if (project != state.projects.end()) {
    for (const ProjectFolderInfo& folder : project->folders) {
      const QString path = folder.path.trimmed();
      if (path.isEmpty() ||
          !QFileInfo(QDir(path).filePath(QStringLiteral(".git"))).exists()) {
        continue;
      }
      ProjectRepoInfo repo;
      repo.id = path;
      repo.label = ProjectFolderLabel(folder);
      repo.path = path;
      AppendRepo(&repos, &seen, repo);
    }
    if (!repos.empty()) {
      return repos;
    }
  }

  // Before the tree/list has loaded, the selected project's working directory
  // is still a useful single-repository fallback when it is a worktree.
  QString fallback = node != state.tree.end() ? node->path : QString();
  if (fallback.isEmpty() && project != state.projects.end()) {
    fallback = project->primary_path;
  }
  if (!fallback.isEmpty() &&
      QFileInfo(QDir(fallback).filePath(QStringLiteral(".git"))).exists()) {
    ProjectRepoInfo repo;
    repo.id = fallback;
    repo.label = RepoLabelForPath(fallback);
    repo.path = fallback;
    AppendRepo(&repos, &seen, repo);
  }
  return repos;
}

}  // namespace hermes::ui
