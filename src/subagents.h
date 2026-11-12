// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SUBAGENTS_H_
#define HERMES_UI_SUBAGENTS_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace hermes::ui {

enum class SubagentStatus {
  kQueued,
  kRunning,
  kCompleted,
  kFailed,
  kInterrupted,
};

struct SubagentStreamEntry {
  qint64 at_ms = 0;
  QString kind;  // progress, tool, thinking, summary
  QString text;
  bool error = false;
};

// One delegated child as the gateway reports it through subagent.* events.
struct SubagentProgress {
  QString id;
  QString parent_id;
  QString goal;
  // Session that dispatched this child. Steering/interrupt RPCs are scoped to
  // the owner, not the child's stored session.
  QString owner_session_id;
  QString session_id;
  QString delegation_id;
  QString model;
  SubagentStatus status = SubagentStatus::kRunning;
  int task_count = 1;
  int task_index = 0;
  qint64 started_at_ms = 0;
  qint64 updated_at_ms = 0;
  double duration_seconds = 0.0;
  double cost_usd = 0.0;
  qint64 input_tokens = 0;
  qint64 output_tokens = 0;
  int tool_count = 0;
  QStringList files_read;
  QStringList files_written;
  std::vector<SubagentStreamEntry> stream;
  QString summary;
  QString current_tool;
};

// Applies one subagent.* event to the profile's live roster. The reducer is
// pure so worker tests can replay payloads without a gateway connection.
void ApplySubagentEvent(std::vector<SubagentProgress>* rows,
                        const QString& event_type, const QString& session_id,
                        const QJsonObject& payload, qint64 now_ms);

bool SubagentIsTerminal(SubagentStatus status);
QString SubagentStatusText(SubagentStatus status);
// Flat depth-first order with children after their parent.
std::vector<const SubagentProgress*> BuildSubagentTree(
    const std::vector<SubagentProgress>& rows);
int SubagentDepth(const QString& id, const std::vector<SubagentProgress>& rows);
QString FormatSubagentDuration(double seconds);
QString FormatSubagentTokens(qint64 tokens);
QString FormatSubagentAge(qint64 updated_at_ms, qint64 now_ms);

}  // namespace hermes::ui

#endif  // HERMES_UI_SUBAGENTS_H_
