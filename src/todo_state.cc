// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/todo_state.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QSet>
#include <QStringList>
#include <algorithm>
#include <utility>
#include <vector>

namespace hermes::ui {

bool ParseTodoState(const QJsonObject& payload, std::vector<TodoItem>* todos,
                    int* revision) {
  if (todos == nullptr || revision == nullptr) {
    return false;
  }
  const QJsonValue list = payload.value(QStringLiteral("todos"));
  const QJsonValue rev = payload.value(QStringLiteral("revision"));
  if (!list.isArray() || (!rev.isDouble() && !rev.isString())) {
    return false;
  }
  bool ok = false;
  const int parsed_revision =
      rev.isDouble() ? rev.toInt() : rev.toString().toInt(&ok);
  if (rev.isString() && !ok) {
    return false;
  }
  std::vector<TodoItem> parsed;
  for (const QJsonValue& value : list.toArray()) {
    const QJsonObject row = value.toObject();
    TodoItem item;
    item.id = row.value(QStringLiteral("id")).toString();
    item.content = row.value(QStringLiteral("content")).toString().trimmed();
    item.status =
        row.value(QStringLiteral("status")).toString().trimmed().toLower();
    if (item.status != QStringLiteral("pending") &&
        item.status != QStringLiteral("in_progress") &&
        item.status != QStringLiteral("completed") &&
        item.status != QStringLiteral("cancelled")) {
      item.status = QStringLiteral("pending");
    }
    item.parent = row.value(QStringLiteral("parent")).toString().trimmed();
    if (item.content.isEmpty()) {
      continue;
    }
    parsed.push_back(std::move(item));
  }
  *todos = std::move(parsed);
  *revision = std::max(0, parsed_revision);
  return true;
}

QString TodoStatusMarker(const QString& status) {
  if (status == QStringLiteral("completed")) {
    return QStringLiteral("[x]");
  }
  if (status == QStringLiteral("in_progress")) {
    return QStringLiteral("[>]");
  }
  if (status == QStringLiteral("cancelled")) {
    return QStringLiteral("[~]");
  }
  return QStringLiteral("[ ]");
}

int TodoDepth(const TodoItem& item, const std::vector<TodoItem>& todos) {
  QSet<QString> seen;
  seen.insert(item.id);
  QString parent = item.parent;
  int depth = 0;
  while (!parent.isEmpty() && depth < 32) {
    if (seen.contains(parent)) {
      return 0;
    }
    seen.insert(parent);
    const auto found = std::find_if(
        todos.begin(), todos.end(),
        [&parent](const TodoItem& row) { return row.id == parent; });
    if (found == todos.end()) {
      return 0;
    }
    ++depth;
    parent = found->parent;
  }
  return depth;
}

QString TodoSummaryText(const std::vector<TodoItem>& todos) {
  int active = 0;
  int done = 0;
  int cancelled = 0;
  for (const TodoItem& item : todos) {
    if (item.status == QStringLiteral("pending") ||
        item.status == QStringLiteral("in_progress")) {
      ++active;
    } else if (item.status == QStringLiteral("completed")) {
      ++done;
    } else {
      ++cancelled;
    }
  }
  QStringList parts;
  if (active > 0) {
    parts.push_back(QStringLiteral("%1 active").arg(active));
  }
  if (done > 0) {
    parts.push_back(QStringLiteral("%1 done").arg(done));
  }
  if (cancelled > 0) {
    parts.push_back(QStringLiteral("%1 cancelled").arg(cancelled));
  }
  return parts.isEmpty() ? QStringLiteral("No tasks")
                         : parts.join(QStringLiteral(" · "));
}

}  // namespace hermes::ui
