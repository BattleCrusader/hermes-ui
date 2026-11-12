// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_TODO_STATE_H_
#define HERMES_UI_TODO_STATE_H_

#include <QJsonObject>
#include <QString>
#include <vector>

namespace hermes::ui {

// One item from the gateway's `todo.updated` snapshot. The todo tool keeps
// list order as priority; `parent` nests a subtask under another item id.
struct TodoItem {
  QString id;
  QString content;
  QString status; // pending, in_progress, completed, cancelled
  QString parent;
};

// Parses `{todos:[...], revision:N}`. Returns false when the payload is not a
// todo snapshot (no array, non-numeric revision) so callers can ignore junk
// events instead of clearing a live list.
bool ParseTodoState(const QJsonObject &payload, std::vector<TodoItem> *todos,
                    int *revision);
QString TodoStatusMarker(const QString &status);
// Zero for a root item. Dangling or cyclic parent links are treated as roots.
int TodoDepth(const TodoItem &item, const std::vector<TodoItem> &todos);
QString TodoSummaryText(const std::vector<TodoItem> &todos);

} // namespace hermes::ui

#endif // HERMES_UI_TODO_STATE_H_
