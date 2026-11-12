// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/cache_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

namespace hermes::ui {
namespace {

constexpr int kCacheVersion = 1;
// A transcript can be up to 500 rows. Keep the cache comfortably below that:
// the goal is a useful first paint, not a second history database.
constexpr int kCacheMessageLimit = 120;
constexpr qint64 kMaxCacheBytes = 4 * 1024 * 1024;

QString ProfileKey(const QString& profile) {
  const QString value = profile.isEmpty() ? QStringLiteral("default") : profile;
  return QString::fromLatin1(
      QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256)
          .toHex()
          .left(20));
}

QString CachePath(const QString& prefix, const QString& profile) {
  return QDir(UiCacheRoot())
      .filePath(QStringLiteral("%1-%2.json").arg(prefix, ProfileKey(profile)));
}

bool SaveJson(const QString& path, const QJsonObject& root, QString* error) {
  QDir directory = QFileInfo(path).dir();
  if (!directory.exists() && !directory.mkpath(QStringLiteral("."))) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not create cache directory");
    }
    return false;
  }
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  if (file.write(bytes) != bytes.size()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  return true;
}

bool LoadJson(const QString& path, QJsonObject* root) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return false;
  }
  *root = document.object();
  return true;
}

QJsonArray StringArray(const QStringList& values) {
  QJsonArray array;
  for (const QString& value : values) {
    array.push_back(value);
  }
  return array;
}

QStringList ReadStringArray(const QJsonValue& value) {
  QStringList values;
  for (const QJsonValue& entry : value.toArray()) {
    const QString text = entry.toString();
    if (!text.isEmpty()) {
      values.push_back(text);
    }
  }
  return values;
}

QJsonObject SessionToJson(const SessionSummary& session) {
  QJsonObject row;
  row.insert(QStringLiteral("id"), session.id);
  row.insert(QStringLiteral("title"), session.title);
  row.insert(QStringLiteral("preview"), session.preview);
  row.insert(QStringLiteral("source"), session.source);
  row.insert(QStringLiteral("project_id"), session.project_id);
  row.insert(QStringLiteral("model"), session.model);
  row.insert(QStringLiteral("provider"), session.provider);
  row.insert(QStringLiteral("reasoning_effort"), session.reasoning_effort);
  row.insert(QStringLiteral("started_at"), session.started_at_secs);
  row.insert(QStringLiteral("last_active"), session.last_active_secs);
  row.insert(QStringLiteral("message_count"), session.message_count);
  row.insert(QStringLiteral("pinned"), session.pinned);
  row.insert(QStringLiteral("archived"), session.archived);
  row.insert(QStringLiteral("unread"), session.unread);
  row.insert(QStringLiteral("parent_session_id"), session.parent_session_id);
  row.insert(QStringLiteral("branched_from"), session.branched_from);
  row.insert(QStringLiteral("reset_from"), session.reset_from);
  row.insert(QStringLiteral("continuation_kind"), session.continuation_kind);
  row.insert(QStringLiteral("revision"), session.revision);
  return row;
}

bool SessionFromJson(const QJsonObject& row, SessionSummary* session) {
  session->id = row.value(QStringLiteral("id")).toString();
  if (session->id.isEmpty()) {
    return false;
  }
  session->title = row.value(QStringLiteral("title")).toString();
  session->preview = row.value(QStringLiteral("preview")).toString();
  session->source = row.value(QStringLiteral("source")).toString();
  session->project_id = row.value(QStringLiteral("project_id")).toString();
  session->model = row.value(QStringLiteral("model")).toString();
  session->provider = row.value(QStringLiteral("provider")).toString();
  session->reasoning_effort =
      row.value(QStringLiteral("reasoning_effort")).toString();
  session->started_at_secs =
      static_cast<qint64>(row.value(QStringLiteral("started_at")).toDouble());
  session->last_active_secs =
      static_cast<qint64>(row.value(QStringLiteral("last_active")).toDouble());
  session->message_count = row.value(QStringLiteral("message_count")).toInt();
  session->pinned = row.value(QStringLiteral("pinned")).toBool();
  session->archived = row.value(QStringLiteral("archived")).toBool();
  session->unread = row.value(QStringLiteral("unread")).toBool();
  session->parent_session_id =
      row.value(QStringLiteral("parent_session_id")).toString();
  session->branched_from =
      row.value(QStringLiteral("branched_from")).toString();
  session->reset_from = row.value(QStringLiteral("reset_from")).toString();
  session->continuation_kind =
      row.value(QStringLiteral("continuation_kind")).toString();
  session->revision = row.value(QStringLiteral("revision")).toInt();
  return true;
}

QJsonObject BlockToJson(const MessageBlock& block) {
  QJsonObject row;
  row.insert(QStringLiteral("kind"), static_cast<int>(block.kind));
  row.insert(QStringLiteral("text"), block.text);
  row.insert(QStringLiteral("language"), block.language);
  row.insert(QStringLiteral("detail"), block.detail);
  row.insert(QStringLiteral("id"), block.id);
  return row;
}

MessageBlock BlockFromJson(const QJsonObject& row) {
  MessageBlock block;
  const int kind = row.value(QStringLiteral("kind")).toInt();
  if (kind >= static_cast<int>(BlockKind::kParagraph) &&
      kind <= static_cast<int>(BlockKind::kFootnote)) {
    block.kind = static_cast<BlockKind>(kind);
  }
  block.text = row.value(QStringLiteral("text")).toString();
  block.language = row.value(QStringLiteral("language")).toString();
  block.detail = row.value(QStringLiteral("detail")).toString();
  block.id = row.value(QStringLiteral("id")).toString();
  return block;
}

QJsonObject MessageToJson(const ChatMessage& message) {
  QJsonObject row;
  row.insert(QStringLiteral("id"), message.id);
  row.insert(QStringLiteral("row_id"), message.row_id);
  row.insert(QStringLiteral("role"), static_cast<int>(message.role));
  row.insert(QStringLiteral("author"), message.author);
  row.insert(
      QStringLiteral("timestamp"),
      message.timestamp.isValid() ? message.timestamp.toMSecsSinceEpoch() : 0);
  QJsonArray blocks;
  for (const MessageBlock& block : message.blocks) {
    blocks.push_back(BlockToJson(block));
  }
  row.insert(QStringLiteral("blocks"), blocks);
  row.insert(QStringLiteral("reasoning"), message.reasoning);
  row.insert(QStringLiteral("raw_text"), message.raw_text);
  QJsonArray reactions;
  for (const ChatReaction& reaction : message.reactions) {
    QJsonObject entry;
    entry.insert(QStringLiteral("emoji"), reaction.emoji);
    entry.insert(QStringLiteral("author"), reaction.author);
    reactions.push_back(entry);
  }
  row.insert(QStringLiteral("reactions"), reactions);
  return row;
}

ChatMessage MessageFromJson(const QJsonObject& row) {
  ChatMessage message;
  message.id = row.value(QStringLiteral("id")).toString();
  message.row_id =
      static_cast<qint64>(row.value(QStringLiteral("row_id")).toDouble());
  const int role = row.value(QStringLiteral("role")).toInt();
  if (role == static_cast<int>(MessageRole::kUser) ||
      role == static_cast<int>(MessageRole::kAssistant) ||
      role == static_cast<int>(MessageRole::kSystem)) {
    message.role = static_cast<MessageRole>(role);
  }
  message.author = row.value(QStringLiteral("author")).toString();
  const qint64 timestamp =
      static_cast<qint64>(row.value(QStringLiteral("timestamp")).toDouble());
  if (timestamp > 0) {
    message.timestamp = QDateTime::fromMSecsSinceEpoch(timestamp);
  }
  for (const QJsonValue& value :
       row.value(QStringLiteral("blocks")).toArray()) {
    message.blocks.push_back(BlockFromJson(value.toObject()));
  }
  message.reasoning = row.value(QStringLiteral("reasoning")).toString();
  message.raw_text = row.value(QStringLiteral("raw_text")).toString();
  for (const QJsonValue& value :
       row.value(QStringLiteral("reactions")).toArray()) {
    const QJsonObject entry = value.toObject();
    ChatReaction reaction;
    reaction.emoji = entry.value(QStringLiteral("emoji")).toString();
    reaction.author = entry.value(QStringLiteral("author")).toString();
    if (!reaction.emoji.isEmpty()) {
      message.reactions.push_back(std::move(reaction));
    }
  }
  // Cached rows are hydrated history, not a live turn. A crash during a
  // streaming reply must not leave a spinner that never completes.
  message.streaming = false;
  message.reasoning_streaming = false;
  message.duration_secs = -1;
  return message;
}

QJsonObject FolderToJson(const ProjectFolderInfo& folder) {
  QJsonObject row;
  row.insert(QStringLiteral("path"), folder.path);
  row.insert(QStringLiteral("label"), folder.label);
  row.insert(QStringLiteral("is_primary"), folder.is_primary);
  return row;
}

ProjectFolderInfo FolderFromJson(const QJsonObject& row) {
  ProjectFolderInfo folder;
  folder.path = row.value(QStringLiteral("path")).toString();
  folder.label = row.value(QStringLiteral("label")).toString();
  folder.is_primary = row.value(QStringLiteral("is_primary")).toBool();
  return folder;
}

QJsonObject ProjectInfoToJson(const ProjectInfo& project) {
  QJsonObject row;
  row.insert(QStringLiteral("id"), project.id);
  row.insert(QStringLiteral("slug"), project.slug);
  row.insert(QStringLiteral("name"), project.name);
  row.insert(QStringLiteral("description"), project.description);
  row.insert(QStringLiteral("icon"), project.icon);
  row.insert(QStringLiteral("color"), project.color);
  row.insert(QStringLiteral("board_slug"), project.board_slug);
  row.insert(QStringLiteral("primary_path"), project.primary_path);
  row.insert(QStringLiteral("archived"), project.archived);
  row.insert(QStringLiteral("created_at"), project.created_at);
  QJsonArray folders;
  for (const ProjectFolderInfo& folder : project.folders) {
    folders.push_back(FolderToJson(folder));
  }
  row.insert(QStringLiteral("folders"), folders);
  return row;
}

ProjectInfo ProjectInfoFromJson(const QJsonObject& row) {
  ProjectInfo project;
  project.id = row.value(QStringLiteral("id")).toString();
  project.slug = row.value(QStringLiteral("slug")).toString();
  project.name = row.value(QStringLiteral("name")).toString();
  project.description = row.value(QStringLiteral("description")).toString();
  project.icon = row.value(QStringLiteral("icon")).toString();
  project.color = row.value(QStringLiteral("color")).toString();
  project.board_slug = row.value(QStringLiteral("board_slug")).toString();
  project.primary_path = row.value(QStringLiteral("primary_path")).toString();
  project.archived = row.value(QStringLiteral("archived")).toBool();
  project.created_at =
      static_cast<qint64>(row.value(QStringLiteral("created_at")).toDouble());
  for (const QJsonValue& value :
       row.value(QStringLiteral("folders")).toArray()) {
    ProjectFolderInfo folder = FolderFromJson(value.toObject());
    if (!folder.path.isEmpty()) {
      project.folders.push_back(std::move(folder));
    }
  }
  if (project.primary_path.isEmpty()) {
    for (const ProjectFolderInfo& folder : project.folders) {
      if (folder.is_primary) {
        project.primary_path = folder.path;
        break;
      }
    }
  }
  return project;
}

QJsonObject ProjectNodeToJson(const ProjectNode& node) {
  QJsonObject row;
  row.insert(QStringLiteral("id"), node.id);
  row.insert(QStringLiteral("label"), node.label);
  row.insert(QStringLiteral("path"), node.path);
  row.insert(QStringLiteral("color"), node.color);
  row.insert(QStringLiteral("icon"), node.icon);
  row.insert(QStringLiteral("is_auto"), node.is_auto);
  row.insert(QStringLiteral("is_no_project"), node.is_no_project);
  row.insert(QStringLiteral("session_count"), node.session_count);
  row.insert(QStringLiteral("last_active"), node.last_active);
  row.insert(QStringLiteral("total_tokens"), node.total_tokens);
  row.insert(QStringLiteral("total_cost_usd"), node.total_cost_usd);
  row.insert(QStringLiteral("repo_count"), node.repo_count);
  row.insert(QStringLiteral("session_ids"), StringArray(node.session_ids));
  return row;
}

ProjectNode ProjectNodeFromJson(const QJsonObject& row) {
  ProjectNode node;
  node.id = row.value(QStringLiteral("id")).toString();
  node.label = row.value(QStringLiteral("label")).toString();
  node.path = row.value(QStringLiteral("path")).toString();
  node.color = row.value(QStringLiteral("color")).toString();
  node.icon = row.value(QStringLiteral("icon")).toString();
  node.is_auto = row.value(QStringLiteral("is_auto")).toBool();
  node.is_no_project = row.value(QStringLiteral("is_no_project")).toBool();
  node.session_count = row.value(QStringLiteral("session_count")).toInt();
  node.last_active = row.value(QStringLiteral("last_active")).toDouble();
  node.total_tokens =
      static_cast<qint64>(row.value(QStringLiteral("total_tokens")).toDouble());
  node.total_cost_usd = row.value(QStringLiteral("total_cost_usd")).toDouble();
  node.repo_count = row.value(QStringLiteral("repo_count")).toInt();
  node.session_ids = ReadStringArray(row.value(QStringLiteral("session_ids")));
  return node;
}

}  // namespace

QString UiCacheRoot() {
  const QString override = qEnvironmentVariable("HERMES_UI_CACHE");
  if (!override.isEmpty()) {
    return override;
  }
  return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
      .filePath(QStringLiteral("state"));
}

QString SessionCachePath(const QString& profile) {
  return CachePath(QStringLiteral("sessions"), profile);
}

QString ProjectCachePath(const QString& profile) {
  return CachePath(QStringLiteral("projects"), profile);
}

bool LoadSessionCache(const QString& profile, CachedSessionState* cache) {
  if (cache == nullptr) {
    return false;
  }
  QJsonObject root;
  if (!LoadJson(SessionCachePath(profile), &root)) {
    return false;
  }
  if (root.value(QStringLiteral("version")).toInt() != kCacheVersion) {
    return false;
  }
  return SessionCacheFromJson(root, cache) && cache->profile == profile;
}

bool SaveSessionCache(const CachedSessionState& cache, QString* error) {
  if (cache.profile.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No profile for session cache");
    }
    return false;
  }
  // Drop the oldest rows before serializing, then trim again if the encoded
  // transcript is still too large. The newest message is always preserved.
  CachedSessionState trimmed = cache;
  if (trimmed.messages.size() > static_cast<std::size_t>(kCacheMessageLimit)) {
    trimmed.messages.erase(trimmed.messages.begin(),
                           trimmed.messages.end() -
                               static_cast<std::ptrdiff_t>(kCacheMessageLimit));
  }
  trimmed.saved_at_ms = QDateTime::currentMSecsSinceEpoch();
  QJsonObject root = SessionCacheToJson(trimmed);
  QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  while (bytes.size() > kMaxCacheBytes && trimmed.messages.size() > 8) {
    const std::size_t drop =
        std::max<std::size_t>(1, trimmed.messages.size() / 4);
    trimmed.messages.erase(
        trimmed.messages.begin(),
        trimmed.messages.begin() + static_cast<std::ptrdiff_t>(drop));
    root = SessionCacheToJson(trimmed);
    bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
  }
  return SaveJson(SessionCachePath(cache.profile), root, error);
}

bool LoadProjectCache(const QString& profile, CachedProjectState* cache) {
  if (cache == nullptr) {
    return false;
  }
  QJsonObject root;
  if (!LoadJson(ProjectCachePath(profile), &root)) {
    return false;
  }
  if (root.value(QStringLiteral("version")).toInt() != kCacheVersion) {
    return false;
  }
  return ProjectCacheFromJson(root, cache) && cache->profile == profile;
}

bool SaveProjectCache(const CachedProjectState& cache, QString* error) {
  if (cache.profile.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No profile for project cache");
    }
    return false;
  }
  CachedProjectState trimmed = cache;
  trimmed.saved_at_ms = QDateTime::currentMSecsSinceEpoch();
  return SaveJson(ProjectCachePath(cache.profile), ProjectCacheToJson(trimmed),
                  error);
}

QJsonObject SessionCacheToJson(const CachedSessionState& cache) {
  QJsonObject root;
  root.insert(QStringLiteral("version"), kCacheVersion);
  root.insert(QStringLiteral("profile"), cache.profile);
  root.insert(QStringLiteral("saved_at"), cache.saved_at_ms);
  root.insert(QStringLiteral("selected_session_id"), cache.selected_session_id);
  QJsonArray sessions;
  for (const SessionSummary& session : cache.sessions) {
    sessions.push_back(SessionToJson(session));
  }
  root.insert(QStringLiteral("sessions"), sessions);
  QJsonArray messages;
  for (const ChatMessage& message : cache.messages) {
    messages.push_back(MessageToJson(message));
  }
  root.insert(QStringLiteral("messages"), messages);
  return root;
}

bool SessionCacheFromJson(const QJsonObject& root, CachedSessionState* cache) {
  if (cache == nullptr || !root.value(QStringLiteral("sessions")).isArray() ||
      !root.value(QStringLiteral("messages")).isArray()) {
    return false;
  }
  CachedSessionState parsed;
  parsed.profile = root.value(QStringLiteral("profile")).toString();
  parsed.selected_session_id =
      root.value(QStringLiteral("selected_session_id")).toString();
  parsed.saved_at_ms =
      static_cast<qint64>(root.value(QStringLiteral("saved_at")).toDouble());
  for (const QJsonValue& value :
       root.value(QStringLiteral("sessions")).toArray()) {
    SessionSummary session;
    if (SessionFromJson(value.toObject(), &session)) {
      parsed.sessions.push_back(std::move(session));
    }
  }
  for (const QJsonValue& value :
       root.value(QStringLiteral("messages")).toArray()) {
    if (!value.isObject()) {
      continue;
    }
    ChatMessage message = MessageFromJson(value.toObject());
    if (message.blocks.empty() && message.reasoning.isEmpty() &&
        message.raw_text.isEmpty()) {
      continue;
    }
    parsed.messages.push_back(std::move(message));
  }
  *cache = std::move(parsed);
  return true;
}

QJsonObject ProjectCacheToJson(const CachedProjectState& cache) {
  QJsonObject root;
  root.insert(QStringLiteral("version"), kCacheVersion);
  root.insert(QStringLiteral("profile"), cache.profile);
  root.insert(QStringLiteral("saved_at"), cache.saved_at_ms);
  root.insert(QStringLiteral("scope"), cache.scope);
  root.insert(QStringLiteral("loaded"), cache.projects.loaded);
  root.insert(QStringLiteral("active_id"), cache.projects.active_id);
  QJsonArray projects;
  for (const ProjectInfo& project : cache.projects.projects) {
    projects.push_back(ProjectInfoToJson(project));
  }
  root.insert(QStringLiteral("projects"), projects);
  QJsonArray tree;
  for (const ProjectNode& node : cache.projects.tree) {
    tree.push_back(ProjectNodeToJson(node));
  }
  root.insert(QStringLiteral("tree"), tree);
  return root;
}

bool ProjectCacheFromJson(const QJsonObject& root, CachedProjectState* cache) {
  if (cache == nullptr || !root.value(QStringLiteral("projects")).isArray() ||
      !root.value(QStringLiteral("tree")).isArray()) {
    return false;
  }
  CachedProjectState parsed;
  parsed.profile = root.value(QStringLiteral("profile")).toString();
  parsed.scope = root.value(QStringLiteral("scope")).toString();
  if (parsed.scope.isEmpty()) {
    parsed.scope = AllProjectsId();
  }
  parsed.saved_at_ms =
      static_cast<qint64>(root.value(QStringLiteral("saved_at")).toDouble());
  parsed.projects.profile = parsed.profile;
  parsed.projects.loaded = root.value(QStringLiteral("loaded")).toBool();
  parsed.projects.available = true;
  parsed.projects.active_id =
      root.value(QStringLiteral("active_id")).toString();
  for (const QJsonValue& value :
       root.value(QStringLiteral("projects")).toArray()) {
    ProjectInfo project = ProjectInfoFromJson(value.toObject());
    if (!project.id.isEmpty()) {
      parsed.projects.projects.push_back(std::move(project));
    }
  }
  for (const QJsonValue& value : root.value(QStringLiteral("tree")).toArray()) {
    ProjectNode node = ProjectNodeFromJson(value.toObject());
    if (!node.id.isEmpty()) {
      parsed.projects.tree.push_back(std::move(node));
    }
  }
  *cache = std::move(parsed);
  return true;
}

}  // namespace hermes::ui
