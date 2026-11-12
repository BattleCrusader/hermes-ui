// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_BACKGROUND_TASK_H_
#define HERMES_UI_BACKGROUND_TASK_H_

#include <QFutureWatcher>
#include <QJsonObject>
#include <QObject>
#include <QtConcurrent/QtConcurrentRun>
#include <functional>
#include <utility>

namespace hermes::ui {

// Result envelope for work that touches the gateway or filesystem. Keeping the
// error beside the payload lets page code stay on the UI thread while all I/O
// happens on the global thread pool.
struct BackgroundResult {
  QJsonObject data;
  QString error;

  bool ok() const { return error.isEmpty(); }
};

using BackgroundDone = std::function<void(const BackgroundResult &)>;

// Runs `work` on the global thread pool, then invokes `done` on the thread that
// owns `context`. The watcher is parented to `context`, so a window close drops
// the completion instead of calling into a destroyed page.
void RunBackground(QObject *context, std::function<BackgroundResult()> work,
                   BackgroundDone done);

} // namespace hermes::ui

#endif // HERMES_UI_BACKGROUND_TASK_H_
