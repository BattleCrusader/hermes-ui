#ifndef HERMES_UI_KANBAN_MODEL_H_
#define HERMES_UI_KANBAN_MODEL_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// Plain data mirror of /api/plugins/kanban/*. The desktop Kanban plugin is the
// source of truth for these shapes; keeping parsing out of the widgets makes the
// rules (locked columns, create payload, formatting) unit-testable.
namespace hermes::kanban {

struct KanbanDiagnosticAction {
  QString kind;
  QString label;
  bool suggested = false;
  QJsonObject payload;
};

struct KanbanDiagnostic {
  QString kind;
  QString severity;
  QString title;
  QString detail;
  int count = 1;
  qint64 last_seen_at = 0;
  QVector<KanbanDiagnosticAction> actions;
};

struct KanbanTask {
  QString id;
  QString title;
  QString body;
  QString result;
  QString status;
  QString assignee;
  QString tenant;
  QString created_at;
  QString latest_summary;
  QString started_at;
  QString current_run_started_at;
  QString last_heartbeat_at;
  QString block_kind;
  QString block_reason;
  QString last_failure_error;
  QString workspace_kind;
  QString workspace_path;
  QString project_id;
  QString model_override;
  QString provider_override;
  QString reasoning_effort;
  int priority = 0;
  int comment_count = 0;
  int link_count = 0;
  int warning_count = 0;
  int block_recurrences = 0;
  int consecutive_failures = 0;
  int worker_pid = 0;
  int progress_done = -1;
  int progress_total = 0;
  bool archived = false;
  double created_age_seconds = -1.0;
  double started_age_seconds = -1.0;
  double time_to_complete_seconds = -1.0;
  QStringList parents;
  QStringList children;
  QStringList skills;
  QVector<KanbanDiagnostic> diagnostics;
};

struct KanbanBoard {
  QString board;
  QStringList columns;
  QStringList tenants;
  QStringList assignees;
  qint64 latest_event_id = 0;
  QVector<KanbanTask> tasks;
};

struct KanbanBoardMeta {
  QString slug;
  QString name;
  QString description;
  QString icon;
  QString color;
  QString project_id;
  QString project_name;
  QString default_workspace_kind;
  QString default_workdir;
  int total = 0;
  bool is_current = false;
  bool archived = false;
};

struct KanbanProject {
  QString id;
  QString name;
  QString primary_path;
};

struct KanbanProfile {
  QString name;
  QString model;
  QString provider;
  QString description;
  bool is_default = false;
  bool description_auto = false;
  int skill_count = 0;
};

struct KanbanOrchestration {
  QString orchestrator_profile;
  QString default_assignee;
  QString resolved_orchestrator_profile;
  QString resolved_default_assignee;
  QString active_profile;
  bool auto_decompose = true;
  bool auto_promote_children = true;
};

struct KanbanComment {
  QString id;
  QString author;
  QString body;
  QString created_at;
};

struct KanbanEvent {
  qint64 id = 0;
  QString task_id;
  QString kind;
  QString created_at;
  QString payload;
};

struct KanbanRun {
  QString id;
  QString status;
  QString profile;
  QString started_at;
  QString finished_at;
  QString summary;
  QString error;
};

struct KanbanAttachment {
  QString id;
  QString filename;
  QString path;
  QString content_type;
  qint64 size = 0;
  QString created_at;
};

struct KanbanLog {
  QString path;
  QString content;
  bool truncated = false;
};

struct KanbanTaskDetail {
  KanbanTask task;
  QVector<KanbanComment> comments;
  QVector<KanbanEvent> events;
  QVector<KanbanRun> runs;
  QVector<KanbanAttachment> attachments;
};

struct KanbanNewTask {
  QString title;
  QString body;
  QString status = QStringLiteral("triage");
  QString assignee;
  QString tenant;
  QString workspace_kind;
  QString workspace_path;
  QString parent_id;
  QString model_override;
  QString provider_override;
  QString reasoning_effort;
  QString skills;
  int priority = 0;
  bool goal_mode = false;
};

// --- Parsing ---------------------------------------------------------------

QString JsonText(const QJsonObject& object, const char* key);
bool JsonFlag(const QJsonObject& object, const char* key, bool fallback = false);
int JsonInt(const QJsonObject& object, const char* key, int fallback = 0);
double JsonDouble(const QJsonObject& object, const char* key, double fallback = -1.0);
qint64 JsonInt64(const QJsonObject& object, const char* key, qint64 fallback = 0);
QStringList JsonStrings(const QJsonObject& object, const char* key);

KanbanTask ParseTask(const QJsonObject& object);
KanbanBoard ParseBoard(const QJsonObject& object);
QVector<KanbanBoardMeta> ParseBoards(const QJsonObject& object);
QVector<KanbanProject> ParseProjects(const QJsonObject& object);
QVector<KanbanProfile> ParseProfiles(const QJsonObject& object);
KanbanOrchestration ParseOrchestration(const QJsonObject& object);
KanbanTaskDetail ParseTaskDetail(const QJsonObject& object);
QVector<KanbanComment> ParseComments(const QJsonObject& object);
QVector<KanbanEvent> ParseEvents(const QJsonObject& object);
QVector<KanbanRun> ParseRuns(const QJsonObject& object);
QVector<KanbanAttachment> ParseAttachments(const QJsonObject& object);
KanbanLog ParseLog(const QJsonObject& object);

// --- Workflow rules --------------------------------------------------------

// The board's own column order, with archived removed. Falls back to the
// backend's default workflow so an empty/old payload still renders.
QStringList OrderedColumns(const QStringList& columns);
// running/review/scheduled are system-owned: cards can leave but never enter.
bool LockedTarget(const QString& status);
bool CanMove(const QString& from, const QString& to);
QString ColumnLabel(const QString& status);
QString LockedReason(const QString& status);

// --- Mutations -------------------------------------------------------------

QJsonObject BuildCreatePayload(const KanbanNewTask& task);
QJsonObject BuildMovePayload(const QString& status);
QJsonObject BuildPriorityPayload(int priority);
QJsonObject BuildAssignPayload(const QString& assignee);
QJsonObject BuildBulkPayload(const QStringList& ids, const QString& status,
                             const QString& assignee, int priority,
                             bool archive);
QString Slugify(const QString& text);

// --- Formatting ------------------------------------------------------------

QString ShortId(const QString& id);
QString RelativeLabel(double age_seconds);
QString DurationLabel(double seconds);
QString ProgressLabel(double progress);
QString PriorityLabel(int priority);
QString Initials(const QString& name);
QString StatusSummary(const KanbanBoard& board, const KanbanBoardMeta& meta);

}  // namespace hermes::kanban

#endif  // HERMES_UI_KANBAN_MODEL_H_
