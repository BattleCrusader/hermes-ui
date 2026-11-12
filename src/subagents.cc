// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/subagents.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

constexpr int kMaxStream = 24;
constexpr int kPreviewMax = 220;

QString Compact(QString text, int max = kPreviewMax) {
  text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
  text = text.trimmed();
  if (text.size() > max) {
    text = text.left(max - 1) + QChar(0x2026);
  }
  return text;
}

QString ToolLabel(const QString &name) {
  const QStringList parts = name.split(QLatin1Char('_'), Qt::SkipEmptyParts);
  QStringList words;
  for (QString part : parts) {
    if (!part.isEmpty()) {
      part[0] = part.at(0).toUpper();
      words.append(part);
    }
  }
  return words.isEmpty() ? name : words.join(QLatin1Char(' '));
}

QString FormatTool(const QString &name, const QString &preview) {
  const QString snippet = Compact(preview, 96);
  return snippet.isEmpty()
             ? ToolLabel(name)
             : QStringLiteral("%1(\"%2\")").arg(ToolLabel(name), snippet);
}

SubagentStatus ParseStatus(const QString &text, bool terminal_event) {
  const QString value = text.trimmed().toLower();
  if (value == QStringLiteral("completed")) {
    return SubagentStatus::kCompleted;
  }
  if (value == QStringLiteral("failed") || value == QStringLiteral("timeout") ||
      value == QStringLiteral("error")) {
    return SubagentStatus::kFailed;
  }
  if (value == QStringLiteral("interrupted") ||
      value == QStringLiteral("cancelled") ||
      value == QStringLiteral("canceled")) {
    return SubagentStatus::kInterrupted;
  }
  if (terminal_event) {
    return SubagentStatus::kFailed;
  }
  if (value == QStringLiteral("queued")) {
    return SubagentStatus::kQueued;
  }
  return SubagentStatus::kRunning;
}

QString IdOf(const QJsonObject &payload) {
  const QString explicit_id =
      payload.value(QStringLiteral("subagent_id")).toString();
  if (!explicit_id.isEmpty()) {
    return explicit_id;
  }
  return QStringLiteral("%1:%2:%3")
      .arg(payload.value(QStringLiteral("parent_id"))
               .toString(QStringLiteral("root")))
      .arg(payload.value(QStringLiteral("task_index")).toInt())
      .arg(payload.value(QStringLiteral("goal")).toString());
}

QStringList Strings(const QJsonArray &array) {
  QStringList out;
  for (const QJsonValue &value : array) {
    if (value.isString()) {
      out.append(value.toString());
    }
  }
  return out;
}

void AppendStream(SubagentProgress *row, SubagentStreamEntry entry) {
  // Progress payloads repeat the current tool and preview on every frame, so
  // an exact duplicate is a replay rather than new activity.
  const bool duplicate =
      std::any_of(row->stream.begin(), row->stream.end(),
                  [&entry](const SubagentStreamEntry &existing) {
                    return existing.kind == entry.kind &&
                           existing.text == entry.text &&
                           existing.error == entry.error;
                  });
  if (duplicate) {
    return;
  }
  row->stream.push_back(std::move(entry));
  if (static_cast<int>(row->stream.size()) > kMaxStream) {
    row->stream.erase(row->stream.begin());
  }
}

void AppendPayloadStream(SubagentProgress *row, const QJsonObject &payload,
                         const QString &event_type, qint64 now_ms) {
  const QString tool = payload.value(QStringLiteral("tool_name")).toString();
  const QString preview =
      payload.value(QStringLiteral("tool_preview"))
          .toString(
              payload.value(QStringLiteral("preview"))
                  .toString(payload.value(QStringLiteral("text")).toString()));
  const QString text = Compact(
      payload.value(QStringLiteral("text"))
          .toString(payload.value(QStringLiteral("preview")).toString()));

  for (const QJsonValue &value :
       payload.value(QStringLiteral("output_tail")).toArray()) {
    const QJsonObject tail = value.toObject();
    const QString tail_text = tail.value(QStringLiteral("preview")).toString();
    const QString tail_tool = tail.value(QStringLiteral("tool")).toString();
    const QString line = tail_tool.isEmpty() ? Compact(tail_text)
                                             : FormatTool(tail_tool, tail_text);
    if (line.isEmpty()) {
      continue;
    }
    SubagentStreamEntry entry;
    entry.at_ms = now_ms;
    entry.kind = tail_tool.isEmpty() ? QStringLiteral("progress")
                                     : QStringLiteral("tool");
    entry.text = line;
    entry.error = tail.value(QStringLiteral("is_error")).toBool();
    AppendStream(row, std::move(entry));
  }

  if (!tool.isEmpty()) {
    SubagentStreamEntry entry;
    entry.at_ms = now_ms;
    entry.kind = QStringLiteral("tool");
    entry.text = FormatTool(tool, preview);
    entry.error = payload.value(QStringLiteral("error")).toBool();
    AppendStream(row, std::move(entry));
  }
  if (event_type == QStringLiteral("subagent.progress") && !text.isEmpty()) {
    SubagentStreamEntry entry;
    entry.at_ms = now_ms;
    entry.kind = QStringLiteral("progress");
    entry.text = text;
    entry.error = payload.value(QStringLiteral("error")).toBool();
    AppendStream(row, std::move(entry));
  }
  if (event_type == QStringLiteral("subagent.thinking") && !text.isEmpty()) {
    SubagentStreamEntry entry;
    entry.at_ms = now_ms;
    entry.kind = QStringLiteral("thinking");
    entry.text = text;
    AppendStream(row, std::move(entry));
  }
}

QString SummaryOf(const QJsonObject &payload) {
  QString summary = payload.value(QStringLiteral("summary")).toString();
  if (summary.isEmpty()) {
    summary = payload.value(QStringLiteral("preview"))
                  .toString(payload.value(QStringLiteral("text")).toString());
  }
  if (summary.isEmpty() && payload.value(QStringLiteral("status")).toString() ==
                               QStringLiteral("timeout")) {
    const double seconds =
        payload.value(QStringLiteral("duration_seconds")).toDouble();
    summary = QStringLiteral("Timed out after %1s")
                  .arg(QString::number(seconds, 'f', 1));
  }
  return Compact(summary, 500);
}

} // namespace

bool SubagentIsTerminal(SubagentStatus status) {
  return status == SubagentStatus::kCompleted ||
         status == SubagentStatus::kFailed ||
         status == SubagentStatus::kInterrupted;
}

QString SubagentStatusText(SubagentStatus status) {
  switch (status) {
  case SubagentStatus::kQueued:
    return QStringLiteral("Queued");
  case SubagentStatus::kRunning:
    return QStringLiteral("Running");
  case SubagentStatus::kCompleted:
    return QStringLiteral("Done");
  case SubagentStatus::kFailed:
    return QStringLiteral("Failed");
  case SubagentStatus::kInterrupted:
    return QStringLiteral("Interrupted");
  }
  return QStringLiteral("Unknown");
}

void ApplySubagentEvent(std::vector<SubagentProgress> *rows,
                        const QString &event_type, const QString &session_id,
                        const QJsonObject &payload, qint64 now_ms) {
  if (rows == nullptr || !event_type.startsWith(QStringLiteral("subagent."))) {
    return;
  }
  const QString id = IdOf(payload);
  if (id.isEmpty()) {
    return;
  }
  auto it =
      std::find_if(rows->begin(), rows->end(),
                   [&id](const SubagentProgress &row) { return row.id == id; });
  if (it == rows->end()) {
    SubagentProgress row;
    row.id = id;
    row.goal = QStringLiteral("Subagent");
    row.started_at_ms = now_ms;
    rows->push_back(std::move(row));
    it = rows->end() - 1;
  }

  SubagentProgress &row = *it;
  if (row.session_id.isEmpty()) {
    row.session_id = session_id;
  }
  const QString goal =
      payload.value(QStringLiteral("goal"))
          .toString(payload.value(QStringLiteral("preview")).toString());
  if (!goal.isEmpty()) {
    row.goal = Compact(goal);
  }
  const QString parent = payload.value(QStringLiteral("parent_id")).toString();
  if (!parent.isEmpty()) {
    row.parent_id = parent;
  }
  const QString child_session =
      payload.value(QStringLiteral("child_session_id")).toString();
  if (!child_session.isEmpty()) {
    row.session_id = child_session;
  }
  const QString delegation =
      payload.value(QStringLiteral("delegation_id")).toString();
  if (!delegation.isEmpty()) {
    row.delegation_id = delegation;
  }
  const QString model = payload.value(QStringLiteral("model")).toString();
  if (!model.isEmpty()) {
    row.model = model;
  }
  if (payload.contains(QStringLiteral("task_count"))) {
    row.task_count =
        std::max(1, payload.value(QStringLiteral("task_count")).toInt());
  }
  if (payload.contains(QStringLiteral("task_index"))) {
    row.task_index =
        std::max(0, payload.value(QStringLiteral("task_index")).toInt());
  }
  row.status = ParseStatus(payload.value(QStringLiteral("status")).toString(),
                           event_type == QStringLiteral("subagent.complete"));
  if (payload.contains(QStringLiteral("duration_seconds"))) {
    row.duration_seconds =
        payload.value(QStringLiteral("duration_seconds")).toDouble();
  }
  if (payload.contains(QStringLiteral("cost_usd"))) {
    row.cost_usd = payload.value(QStringLiteral("cost_usd")).toDouble();
  }
  if (payload.contains(QStringLiteral("input_tokens"))) {
    row.input_tokens = static_cast<qint64>(
        payload.value(QStringLiteral("input_tokens")).toDouble());
  }
  if (payload.contains(QStringLiteral("output_tokens"))) {
    row.output_tokens = static_cast<qint64>(
        payload.value(QStringLiteral("output_tokens")).toDouble());
  }
  if (payload.contains(QStringLiteral("tool_count"))) {
    row.tool_count = payload.value(QStringLiteral("tool_count")).toInt();
  }
  const QStringList files_read =
      Strings(payload.value(QStringLiteral("files_read")).toArray());
  if (!files_read.isEmpty()) {
    row.files_read = files_read;
  }
  const QStringList files_written =
      Strings(payload.value(QStringLiteral("files_written")).toArray());
  if (!files_written.isEmpty()) {
    row.files_written = files_written;
  }

  AppendPayloadStream(&row, payload, event_type, now_ms);
  const QString summary = SummaryOf(payload);
  if (!summary.isEmpty()) {
    row.summary = summary;
  }
  if (SubagentIsTerminal(row.status) && !row.summary.isEmpty()) {
    SubagentStreamEntry entry;
    entry.at_ms = now_ms;
    entry.kind = QStringLiteral("summary");
    entry.text = row.summary;
    entry.error = row.status == SubagentStatus::kFailed;
    AppendStream(&row, std::move(entry));
  }

  const QString tool = payload.value(QStringLiteral("tool_name")).toString();
  row.current_tool =
      SubagentIsTerminal(row.status)
          ? QString()
          : (tool.isEmpty() ? row.current_tool : ToolLabel(tool));
  row.updated_at_ms = now_ms;
}

int SubagentDepth(const QString &id,
                  const std::vector<SubagentProgress> &rows) {
  int depth = 0;
  QString current = id;
  std::set<QString> seen;
  while (!current.isEmpty() && depth < 16) {
    if (!seen.insert(current).second) {
      break;
    }
    const auto it = std::find_if(
        rows.begin(), rows.end(),
        [&current](const SubagentProgress &row) { return row.id == current; });
    if (it == rows.end() || it->parent_id.isEmpty()) {
      break;
    }
    current = it->parent_id;
    ++depth;
  }
  return depth;
}

std::vector<const SubagentProgress *>
BuildSubagentTree(const std::vector<SubagentProgress> &rows) {
  std::map<QString, std::vector<const SubagentProgress *>> children;
  std::vector<const SubagentProgress *> roots;
  for (const SubagentProgress &row : rows) {
    const bool has_parent =
        !row.parent_id.isEmpty() &&
        std::any_of(rows.begin(), rows.end(),
                    [&row](const SubagentProgress &candidate) {
                      return candidate.id == row.parent_id;
                    });
    if (has_parent) {
      children[row.parent_id].push_back(&row);
    } else {
      roots.push_back(&row);
    }
  }
  auto by_activity = [](const SubagentProgress *left,
                        const SubagentProgress *right) {
    const bool left_live = !SubagentIsTerminal(left->status);
    const bool right_live = !SubagentIsTerminal(right->status);
    if (left_live != right_live) {
      return left_live;
    }
    return left->updated_at_ms > right->updated_at_ms;
  };
  std::sort(roots.begin(), roots.end(), by_activity);

  std::vector<const SubagentProgress *> flat;
  std::set<QString> visited;
  const auto walk = [&](const auto &self, const SubagentProgress *row) -> void {
    if (row == nullptr || !visited.insert(row->id).second) {
      return;
    }
    flat.push_back(row);
    auto child_it = children.find(row->id);
    if (child_it == children.end()) {
      return;
    }
    std::sort(child_it->second.begin(), child_it->second.end(), by_activity);
    for (const SubagentProgress *child : child_it->second) {
      self(self, child);
    }
  };
  for (const SubagentProgress *root : roots) {
    walk(walk, root);
  }
  return flat;
}

QString FormatSubagentDuration(double seconds) {
  if (seconds <= 0.0) {
    return QString();
  }
  if (seconds < 60.0) {
    return QStringLiteral("%1s").arg(QString::number(seconds, 'f', 1));
  }
  const int minutes = static_cast<int>(seconds) / 60;
  const int remainder = static_cast<int>(std::llround(seconds)) % 60;
  return QStringLiteral("%1m %2s").arg(minutes).arg(remainder);
}

QString FormatSubagentTokens(qint64 tokens) {
  if (tokens <= 0) {
    return QString();
  }
  if (tokens < 1000) {
    return QString::number(tokens);
  }
  if (tokens < 1000000) {
    return QStringLiteral("%1k").arg(QString::number(tokens / 1000.0, 'f', 1));
  }
  return QStringLiteral("%1M").arg(QString::number(tokens / 1000000.0, 'f', 1));
}

QString FormatSubagentAge(qint64 updated_at_ms, qint64 now_ms) {
  const qint64 seconds = std::max<qint64>(0, (now_ms - updated_at_ms) / 1000);
  if (seconds < 2) {
    return QStringLiteral("now");
  }
  if (seconds < 60) {
    return QStringLiteral("%1s ago").arg(seconds);
  }
  const qint64 minutes = seconds / 60;
  if (minutes < 60) {
    return QStringLiteral("%1m ago").arg(minutes);
  }
  return QStringLiteral("%1h ago").arg(minutes / 60);
}

} // namespace hermes::ui
