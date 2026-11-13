#include "kanban_model.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>

#include <algorithm>

namespace hermes::kanban {
namespace {

QStringList DefaultColumns() {
  // Mirrors ``kanban_workflow.DEFAULT_WORKFLOW`` column order: Scheduled sits
  // between Todo and Ready, and Blocked precedes Review.
  return {QStringLiteral("triage"),  QStringLiteral("todo"),
          QStringLiteral("scheduled"), QStringLiteral("ready"),
          QStringLiteral("running"), QStringLiteral("blocked"),
          QStringLiteral("review"),  QStringLiteral("done"),
          QStringLiteral("archived")};
}

}  // namespace

QString JsonText(const QJsonObject& object, const char* key) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isString()) {
    return value.toString();
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 15);
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  return QString();
}

bool JsonFlag(const QJsonObject& object, const char* key, bool fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isBool()) {
    return value.toBool();
  }
  if (value.isDouble()) {
    return value.toDouble() != 0.0;
  }
  return fallback;
}

int JsonInt(const QJsonObject& object, const char* key, int fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isDouble()) {
    return value.toInt(fallback);
  }
  if (value.isString()) {
    bool ok = false;
    const int parsed = value.toString().toInt(&ok);
    return ok ? parsed : fallback;
  }
  return fallback;
}

double JsonDouble(const QJsonObject& object, const char* key, double fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isDouble()) {
    return value.toDouble();
  }
  if (value.isString()) {
    bool ok = false;
    const double parsed = value.toString().toDouble(&ok);
    return ok ? parsed : fallback;
  }
  return fallback;
}

qint64 JsonInt64(const QJsonObject& object, const char* key, qint64 fallback) {
  const QJsonValue value = object.value(QLatin1String(key));
  if (value.isDouble()) {
    return static_cast<qint64>(value.toDouble());
  }
  if (value.isString()) {
    bool ok = false;
    const qint64 parsed = value.toString().toLongLong(&ok);
    return ok ? parsed : fallback;
  }
  return fallback;
}

QStringList JsonStrings(const QJsonObject& object, const char* key) {
  QStringList out;
  const QJsonValue value = object.value(QLatin1String(key));
  if (!value.isArray()) {
    return out;
  }
  for (const QJsonValue& item : value.toArray()) {
    if (item.isString()) {
      out.push_back(item.toString());
    } else if (item.isObject()) {
      const QJsonObject child = item.toObject();
      QString text = JsonText(child, "id");
      if (text.isEmpty()) {
        text = JsonText(child, "name");
      }
      if (!text.isEmpty()) {
        out.push_back(text);
      }
    }
  }
  return out;
}

KanbanTask ParseTask(const QJsonObject& object) {
  KanbanTask task;
  task.id = JsonText(object, "id");
  task.title = JsonText(object, "title");
  task.body = JsonText(object, "body");
  task.result = JsonText(object, "result");
  task.status = JsonText(object, "status");
  task.assignee = JsonText(object, "assignee");
  task.tenant = JsonText(object, "tenant");
  task.created_at = JsonText(object, "created_at");
  task.latest_summary = JsonText(object, "latest_summary");
  task.started_at = JsonText(object, "started_at");
  task.current_run_started_at = JsonText(object, "current_run_started_at");
  task.last_heartbeat_at = JsonText(object, "last_heartbeat_at");
  task.block_kind = JsonText(object, "block_kind");
  task.block_reason = JsonText(object, "block_reason");
  task.last_failure_error = JsonText(object, "last_failure_error");
  task.workspace_kind = JsonText(object, "workspace_kind");
  task.workspace_path = JsonText(object, "workspace_path");
  task.project_id = JsonText(object, "project_id");
  task.model_override = JsonText(object, "model_override");
  task.provider_override = JsonText(object, "provider_override");
  task.reasoning_effort = JsonText(object, "reasoning_effort");
  task.priority = JsonInt(object, "priority");
  task.comment_count = JsonInt(object, "comment_count");
  task.block_recurrences = JsonInt(object, "block_recurrences");
  task.consecutive_failures = JsonInt(object, "consecutive_failures");
  task.worker_pid = JsonInt(object, "worker_pid");
  task.archived = task.status == QStringLiteral("archived") ||
                  JsonFlag(object, "archived");
  task.skills = JsonStrings(object, "skills");

  const QJsonObject link_counts = object.value("link_counts").toObject();
  task.link_count = JsonInt(link_counts, "parents") + JsonInt(link_counts, "children");

  const QJsonObject progress = object.value("progress").toObject();
  if (!progress.isEmpty()) {
    task.progress_done = JsonInt(progress, "done", 0);
    task.progress_total = JsonInt(progress, "total", 0);
  }

  const QJsonObject warnings = object.value("warnings").toObject();
  task.warning_count = JsonInt(warnings, "count");

  const QJsonObject age = object.value("age").toObject();
  if (!age.isEmpty()) {
    task.created_age_seconds = JsonDouble(age, "created_age_seconds");
    task.started_age_seconds = JsonDouble(age, "started_age_seconds");
    task.time_to_complete_seconds = JsonDouble(age, "time_to_complete_seconds");
  }

  const QJsonObject links = object.value("links").toObject();
  task.parents = JsonStrings(links, "parents");
  task.children = JsonStrings(links, "children");
  if (task.parents.isEmpty()) {
    task.parents = JsonStrings(object, "parents");
  }
  if (task.children.isEmpty()) {
    task.children = JsonStrings(object, "children");
  }

  for (const QJsonValue& value : object.value("diagnostics").toArray()) {
    const QJsonObject diag_obj = value.toObject();
    KanbanDiagnostic diag;
    diag.kind = JsonText(diag_obj, "kind");
    diag.severity = JsonText(diag_obj, "severity");
    diag.title = JsonText(diag_obj, "title");
    diag.detail = JsonText(diag_obj, "detail");
    diag.count = JsonInt(diag_obj, "count", 1);
    diag.last_seen_at = JsonInt64(diag_obj, "last_seen_at");
    for (const QJsonValue& action_value : diag_obj.value("actions").toArray()) {
      const QJsonObject action_obj = action_value.toObject();
      KanbanDiagnosticAction action;
      action.kind = JsonText(action_obj, "kind");
      action.label = JsonText(action_obj, "label");
      action.suggested = JsonFlag(action_obj, "suggested");
      action.payload = action_obj.value("payload").toObject();
      diag.actions.push_back(action);
    }
    task.diagnostics.push_back(diag);
  }
  return task;
}

KanbanBoard ParseBoard(const QJsonObject& object) {
  KanbanBoard board;
  board.board = JsonText(object, "board");
  if (board.board.isEmpty()) {
    board.board = JsonText(object, "slug");
  }
  board.latest_event_id = JsonInt64(object, "latest_event_id");
  board.tenants = JsonStrings(object, "tenants");
  board.assignees = JsonStrings(object, "assignees");

  // Two shapes exist: the plugin returns ``columns: [{name, tasks}]`` while
  // older/embedded snapshots used an object keyed by lane with a flat task list.
  QStringList ordered;
  const QJsonObject columns = object.value("columns").toObject();
  for (auto it = columns.begin(); it != columns.end(); ++it) {
    ordered.push_back(it.key());
    // The plugin's get_board returns ``columns: {lane: [task, ...]}``; the
    // tasks live inside each lane, not in a flat top-level list.
    const QJsonValue lane = it.value();
    const QJsonArray lane_tasks = lane.isArray()
                                      ? lane.toArray()
                                      : lane.toObject().value("tasks").toArray();
    for (const QJsonValue& task : lane_tasks) {
      board.tasks.push_back(ParseTask(task.toObject()));
    }
  }
  const QJsonArray column_array = object.value("columns").toArray();
  for (const QJsonValue& value : column_array) {
    if (value.isString()) {
      ordered.push_back(value.toString());
      continue;
    }
    const QJsonObject column = value.toObject();
    QString name = JsonText(column, "name");
    if (name.isEmpty()) {
      name = JsonText(column, "key");
    }
    if (!name.isEmpty()) {
      ordered.push_back(name);
    }
    for (const QJsonValue& task : column.value("tasks").toArray()) {
      board.tasks.push_back(ParseTask(task.toObject()));
    }
  }
  board.columns = OrderedColumns(ordered);

  // Merge a flat task list when present (older snapshots); de-dupe by id so
  // the lane copy above wins and no card is painted twice.
  for (const QJsonValue& value : object.value("tasks").toArray()) {
    const KanbanTask task = ParseTask(value.toObject());
    bool known = false;
    for (const KanbanTask& existing : board.tasks) {
      if (existing.id == task.id) {
        known = true;
        break;
      }
    }
    if (!known) {
      board.tasks.push_back(task);
    }
  }
  // A column absent from the payload still needs its lane; keep the canonical
  // board order even when the workflow response is unavailable.
  if (board.columns.isEmpty()) {
    board.columns = OrderedColumns({});
  }
  return board;
}

QVector<KanbanBoardMeta> ParseBoards(const QJsonObject& object) {
  QVector<KanbanBoardMeta> out;
  for (const QJsonValue& value : object.value("boards").toArray()) {
    const QJsonObject board_obj = value.toObject();
    KanbanBoardMeta meta;
    meta.slug = JsonText(board_obj, "slug");
    if (meta.slug.isEmpty()) {
      meta.slug = JsonText(board_obj, "board");
    }
    meta.name = JsonText(board_obj, "name");
    if (meta.name.isEmpty()) {
      meta.name = meta.slug;
    }
    meta.description = JsonText(board_obj, "description");
    meta.icon = JsonText(board_obj, "icon");
    meta.color = JsonText(board_obj, "color");
    meta.project_id = JsonText(board_obj, "project_id");
    meta.project_name = JsonText(board_obj, "project_name");
    meta.default_workspace_kind =
        JsonText(board_obj, "default_workspace_kind");
    meta.default_workdir = JsonText(board_obj, "default_workdir");
    meta.total = JsonInt(board_obj, "total");
    meta.is_current = JsonFlag(board_obj, "is_current");
    meta.archived = JsonFlag(board_obj, "archived");
    out.push_back(meta);
  }
  return out;
}

QVector<KanbanProject> ParseProjects(const QJsonObject& object) {
  QVector<KanbanProject> out;
  const QJsonArray projects = object.value("projects").isArray()
                                  ? object.value("projects").toArray()
                                  : object.value("project_list").toArray();
  for (const QJsonValue& value : projects) {
    const QJsonObject project_obj = value.toObject();
    KanbanProject project;
    project.id = JsonText(project_obj, "id");
    project.name = JsonText(project_obj, "name");
    project.primary_path = JsonText(project_obj, "primary_path");
    if (project.primary_path.isEmpty()) {
      project.primary_path = JsonText(project_obj, "path");
    }
    if (project.primary_path.isEmpty()) {
      const QJsonArray folders = project_obj.value("folders").toArray();
      for (const QJsonValue& folder : folders) {
        const QJsonObject folder_obj = folder.toObject();
        if (JsonFlag(folder_obj, "primary", true)) {
          project.primary_path = JsonText(folder_obj, "path");
          break;
        }
      }
    }
    if (!project.id.isEmpty()) {
      out.push_back(project);
    }
  }
  return out;
}

QVector<KanbanProfile> ParseProfiles(const QJsonObject& object) {
  QVector<KanbanProfile> out;
  for (const QJsonValue& value : object.value("profiles").toArray()) {
    const QJsonObject profile_obj = value.toObject();
    KanbanProfile profile;
    profile.name = JsonText(profile_obj, "name");
    profile.model = JsonText(profile_obj, "model");
    profile.provider = JsonText(profile_obj, "provider");
    profile.description = JsonText(profile_obj, "description");
    profile.is_default = JsonFlag(profile_obj, "is_default");
    profile.description_auto = JsonFlag(profile_obj, "description_auto");
    profile.skill_count = JsonInt(profile_obj, "skill_count");
    if (!profile.name.isEmpty()) {
      out.push_back(profile);
    }
  }
  return out;
}

KanbanOrchestration ParseOrchestration(const QJsonObject& object) {
  KanbanOrchestration settings;
  settings.orchestrator_profile = JsonText(object, "orchestrator_profile");
  settings.default_assignee = JsonText(object, "default_assignee");
  settings.resolved_orchestrator_profile =
      JsonText(object, "resolved_orchestrator_profile");
  settings.resolved_default_assignee =
      JsonText(object, "resolved_default_assignee");
  settings.active_profile = JsonText(object, "active_profile");
  settings.auto_decompose = JsonFlag(object, "auto_decompose", true);
  settings.auto_promote_children =
      JsonFlag(object, "auto_promote_children", true);
  return settings;
}

QVector<KanbanComment> ParseComments(const QJsonObject& object) {
  QVector<KanbanComment> out;
  const QJsonArray comments = object.value("comments").isArray()
                                  ? object.value("comments").toArray()
                                  : QJsonArray();
  for (const QJsonValue& value : comments) {
    const QJsonObject comment_obj = value.toObject();
    KanbanComment comment;
    comment.id = JsonText(comment_obj, "id");
    comment.author = JsonText(comment_obj, "author");
    comment.body = JsonText(comment_obj, "body");
    comment.created_at = JsonText(comment_obj, "created_at");
    out.push_back(comment);
  }
  return out;
}

QVector<KanbanEvent> ParseEvents(const QJsonObject& object) {
  QVector<KanbanEvent> out;
  for (const QJsonValue& value : object.value("events").toArray()) {
    const QJsonObject event_obj = value.toObject();
    KanbanEvent event;
    event.id = JsonInt64(event_obj, "id");
    event.task_id = JsonText(event_obj, "task_id");
    event.kind = JsonText(event_obj, "kind");
    event.created_at = JsonText(event_obj, "created_at");
    if (event_obj.value("payload").isString()) {
      event.payload = event_obj.value("payload").toString();
    } else if (event_obj.value("payload").isObject()) {
      event.payload = QString::fromUtf8(
          QJsonDocument(event_obj.value("payload").toObject())
              .toJson(QJsonDocument::Compact));
    }
    out.push_back(event);
  }
  return out;
}

QVector<KanbanRun> ParseRuns(const QJsonObject& object) {
  QVector<KanbanRun> out;
  for (const QJsonValue& value : object.value("runs").toArray()) {
    const QJsonObject run_obj = value.toObject();
    KanbanRun run;
    run.id = JsonText(run_obj, "id");
    run.status = JsonText(run_obj, "status");
    run.profile = JsonText(run_obj, "profile");
    if (run.profile.isEmpty()) {
      run.profile = JsonText(run_obj, "assignee");
    }
    run.started_at = JsonText(run_obj, "started_at");
    run.finished_at = JsonText(run_obj, "finished_at");
    run.summary = JsonText(run_obj, "summary");
    run.error = JsonText(run_obj, "error");
    out.push_back(run);
  }
  return out;
}

QVector<KanbanAttachment> ParseAttachments(const QJsonObject& object) {
  QVector<KanbanAttachment> out;
  for (const QJsonValue& value : object.value("attachments").toArray()) {
    const QJsonObject attachment_obj = value.toObject();
    KanbanAttachment attachment;
    attachment.id = JsonText(attachment_obj, "id");
    attachment.filename = JsonText(attachment_obj, "filename");
    attachment.path = JsonText(attachment_obj, "stored_path");
    if (attachment.path.isEmpty()) {
      attachment.path = JsonText(attachment_obj, "path");
    }
    attachment.content_type = JsonText(attachment_obj, "content_type");
    attachment.size = JsonInt64(attachment_obj, "size");
    attachment.created_at = JsonText(attachment_obj, "created_at");
    out.push_back(attachment);
  }
  return out;
}

KanbanLog ParseLog(const QJsonObject& object) {
  KanbanLog log;
  log.path = JsonText(object, "path");
  log.content = JsonText(object, "content");
  if (log.content.isEmpty()) {
    log.content = JsonText(object, "log");
  }
  log.truncated = JsonFlag(object, "truncated");
  return log;
}

KanbanTaskDetail ParseTaskDetail(const QJsonObject& object) {
  KanbanTaskDetail detail;
  detail.task = ParseTask(object.value("task").toObject());
  detail.comments = ParseComments(object);
  detail.events = ParseEvents(object);
  detail.runs = ParseRuns(object);
  detail.attachments = ParseAttachments(object);

  const QJsonObject links = object.value("links").toObject();
  detail.task.parents = JsonStrings(links, "parents");
  detail.task.children = JsonStrings(links, "children");
  return detail;
}

QStringList OrderedColumns(const QStringList& columns) {
  QStringList source = columns;
  source.removeAll(QStringLiteral("archived"));
  if (source.isEmpty()) {
    source = DefaultColumns();
    source.removeAll(QStringLiteral("archived"));
  }
  QStringList out;
  for (const QString& column : DefaultColumns()) {
    if (column == QStringLiteral("archived")) {
      continue;
    }
    if (source.contains(column)) {
      out.push_back(column);
    }
  }
  // Preserve custom workflow columns the desktop would render after the
  // canonical lanes instead of silently dropping them.
  for (const QString& column : source) {
    if (!out.contains(column)) {
      out.push_back(column);
    }
  }
  if (columns.contains(QStringLiteral("archived"))) {
    out.push_back(QStringLiteral("archived"));
  }
  return out;
}

bool LockedTarget(const QString& status) {
  return status == QStringLiteral("running") ||
         status == QStringLiteral("review") ||
         status == QStringLiteral("scheduled");
}

namespace {

// ``kanban_workflow.DEFAULT_WORKFLOW.manual`` — the drag targets the desktop
// offers for each lane. Keeping this table in the model means the canvas never
// sends a move the workflow would reject.
QStringList WorkflowTargets(const QString& from) {
  if (from == QStringLiteral("triage")) {
    return {QStringLiteral("todo"), QStringLiteral("ready")};
  }
  if (from == QStringLiteral("todo")) {
    return {QStringLiteral("triage"), QStringLiteral("scheduled"),
            QStringLiteral("ready")};
  }
  if (from == QStringLiteral("scheduled")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("ready")};
  }
  if (from == QStringLiteral("ready")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("scheduled"), QStringLiteral("blocked"),
            QStringLiteral("review"), QStringLiteral("done")};
  }
  if (from == QStringLiteral("running")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("scheduled"), QStringLiteral("ready"),
            QStringLiteral("blocked"), QStringLiteral("review"),
            QStringLiteral("done")};
  }
  if (from == QStringLiteral("blocked")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("scheduled"), QStringLiteral("ready"),
            QStringLiteral("done")};
  }
  if (from == QStringLiteral("review")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("ready"), QStringLiteral("done")};
  }
  if (from == QStringLiteral("done") ||
      from == QStringLiteral("archived")) {
    return {QStringLiteral("triage"), QStringLiteral("todo"),
            QStringLiteral("ready")};
  }
  return {};
}

}  // namespace

bool CanMove(const QString& from, const QString& to) {
  if (from.isEmpty() || to.isEmpty() || from == to || LockedTarget(to)) {
    return false;
  }
  const QStringList targets = WorkflowTargets(from);
  // Custom workflow lanes are not in the default table; let the backend decide
  // as long as the destination is not system-owned.
  return targets.isEmpty() || targets.contains(to);
}

QString ColumnLabel(const QString& status) {
  if (status == QStringLiteral("triage")) return QStringLiteral("Triage");
  if (status == QStringLiteral("todo")) return QStringLiteral("Todo");
  if (status == QStringLiteral("ready")) return QStringLiteral("Ready");
  if (status == QStringLiteral("running")) return QStringLiteral("Running");
  if (status == QStringLiteral("review")) return QStringLiteral("Review");
  if (status == QStringLiteral("scheduled")) return QStringLiteral("Scheduled");
  if (status == QStringLiteral("done")) return QStringLiteral("Done");
  if (status == QStringLiteral("blocked")) return QStringLiteral("Blocked");
  if (status == QStringLiteral("archived")) return QStringLiteral("Archived");
  return status.isEmpty() ? QStringLiteral("?") : status;
}

QString LockedReason(const QString& status) {
  if (status == QStringLiteral("running")) {
    return QStringLiteral("Running is owned by the dispatcher");
  }
  if (status == QStringLiteral("review")) {
    return QStringLiteral("Review is completed by a worker");
  }
  if (status == QStringLiteral("scheduled")) {
    return QStringLiteral("Scheduled is set by the scheduler");
  }
  return QString();
}

QJsonObject BuildCreatePayload(const KanbanNewTask& task) {
  QJsonObject body;
  body.insert(QStringLiteral("title"), task.title.trimmed());
  if (!task.body.trimmed().isEmpty()) {
    body.insert(QStringLiteral("body"), task.body);
  }
  if (!task.assignee.trimmed().isEmpty()) {
    body.insert(QStringLiteral("assignee"), task.assignee.trimmed());
  }
  if (!task.tenant.trimmed().isEmpty()) {
    body.insert(QStringLiteral("tenant"), task.tenant.trimmed());
  }
  if (task.priority != 0) {
    body.insert(QStringLiteral("priority"), task.priority);
  }
  if (!task.workspace_kind.trimmed().isEmpty()) {
    body.insert(QStringLiteral("workspace_kind"), task.workspace_kind.trimmed());
  }
  if (!task.workspace_path.trimmed().isEmpty()) {
    body.insert(QStringLiteral("workspace_path"), task.workspace_path.trimmed());
  }
  if (!task.parent_id.trimmed().isEmpty()) {
    body.insert(QStringLiteral("parents"),
                QJsonArray{task.parent_id.trimmed()});
  }
  body.insert(QStringLiteral("triage"),
              task.status == QStringLiteral("triage"));
  if (!task.skills.trimmed().isEmpty()) {
    QStringList skills;
    for (const QString& part : task.skills.split(',')) {
      const QString trimmed = part.trimmed();
      if (!trimmed.isEmpty()) {
        skills.push_back(trimmed);
      }
    }
    if (!skills.isEmpty()) {
      QJsonArray array;
      for (const QString& skill : skills) {
        array.push_back(skill);
      }
      body.insert(QStringLiteral("skills"), array);
    }
  }
  if (task.goal_mode) {
    body.insert(QStringLiteral("goal_mode"), true);
  }
  if (!task.model_override.trimmed().isEmpty()) {
    body.insert(QStringLiteral("model_override"), task.model_override.trimmed());
  }
  if (!task.provider_override.trimmed().isEmpty()) {
    body.insert(QStringLiteral("provider_override"),
                task.provider_override.trimmed());
  }
  if (!task.reasoning_effort.trimmed().isEmpty()) {
    body.insert(QStringLiteral("reasoning_effort"),
                task.reasoning_effort.trimmed());
  }
  return body;
}

QJsonObject BuildMovePayload(const QString& status) {
  QJsonObject body;
  body.insert(QStringLiteral("status"), status);
  return body;
}

QJsonObject BuildPriorityPayload(int priority) {
  QJsonObject body;
  body.insert(QStringLiteral("priority"), priority);
  return body;
}

QJsonObject BuildAssignPayload(const QString& assignee) {
  QJsonObject body;
  body.insert(QStringLiteral("assignee"), assignee);
  return body;
}

QJsonObject BuildBulkPayload(const QStringList& ids, const QString& status,
                             const QString& assignee, int priority,
                             bool archive) {
  QJsonObject body;
  QJsonArray array;
  for (const QString& id : ids) {
    array.push_back(id);
  }
  body.insert(QStringLiteral("ids"), array);
  if (!status.isEmpty()) {
    body.insert(QStringLiteral("status"), status);
  }
  if (!assignee.isEmpty()) {
    body.insert(QStringLiteral("assignee"), assignee);
  }
  if (priority != 0) {
    body.insert(QStringLiteral("priority"), priority);
  }
  if (archive) {
    body.insert(QStringLiteral("archive"), true);
  }
  return body;
}

QString Slugify(const QString& text) {
  QString out = text.trimmed().toLower();
  out.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")),
              QStringLiteral("-"));
  while (out.startsWith('-')) {
    out.remove(0, 1);
  }
  while (out.endsWith('-')) {
    out.chop(1);
  }
  if (out.isEmpty()) {
    out = QStringLiteral("board");
  }
  return out;
}

QString ShortId(const QString& id) {
  const QString trimmed = id.trimmed();
  if (trimmed.size() <= 8) {
    return trimmed;
  }
  return trimmed.left(8);
}

QString RelativeLabel(double age_seconds) {
  if (age_seconds < 0) {
    return QString();
  }
  const qint64 seconds = static_cast<qint64>(age_seconds);
  if (seconds < 60) {
    return QStringLiteral("%1s").arg(seconds);
  }
  const qint64 minutes = seconds / 60;
  if (minutes < 60) {
    return QStringLiteral("%1m").arg(minutes);
  }
  const qint64 hours = minutes / 60;
  if (hours < 48) {
    return QStringLiteral("%1h").arg(hours);
  }
  return QStringLiteral("%1d").arg(hours / 24);
}

QString DurationLabel(double seconds) {
  if (seconds < 0) {
    return QString();
  }
  const qint64 total = static_cast<qint64>(seconds);
  const qint64 hours = total / 3600;
  const qint64 minutes = (total % 3600) / 60;
  const qint64 secs = total % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(secs);
  }
  return QStringLiteral("%1s").arg(secs);
}

QString ProgressLabel(double progress) {
  if (progress < 0) {
    return QString();
  }
  return QString::number(static_cast<int>(progress));
}

QString PriorityLabel(int priority) {
  if (priority >= 4) return QStringLiteral("Urgent");
  if (priority == 3) return QStringLiteral("High");
  if (priority == 2) return QStringLiteral("Medium");
  if (priority == 1) return QStringLiteral("Low");
  return QStringLiteral("None");
}

QString Initials(const QString& name) {
  QString out;
  const QStringList words =
      name.split(QRegularExpression(QStringLiteral("[^A-Za-z0-9]+")),
                 Qt::SkipEmptyParts);
  for (const QString& word : words) {
    if (!word.isEmpty()) {
      out += word.front().toUpper();
      if (out.size() == 2) {
        break;
      }
    }
  }
  if (out.isEmpty() && !name.isEmpty()) {
    out = name.left(1).toUpper();
  }
  return out;
}

QString StatusSummary(const KanbanBoard& board, const KanbanBoardMeta& meta) {
  int live = 0;
  int running = 0;
  for (const KanbanTask& task : board.tasks) {
    if (task.archived) {
      continue;
    }
    ++live;
    if (task.status == QStringLiteral("running")) {
      ++running;
    }
  }
  const QString name = meta.name.isEmpty() ? board.board : meta.name;
  if (running > 0) {
    return QStringLiteral("%1 · %2 cards · %3 running")
        .arg(name)
        .arg(live)
        .arg(running);
  }
  return QStringLiteral("%1 · %2 cards").arg(name).arg(live);
}

}  // namespace hermes::kanban
