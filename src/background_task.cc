// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/background_task.h"

#include <utility>

namespace hermes::ui {

void RunBackground(QObject* context, std::function<BackgroundResult()> work,
                   BackgroundDone done) {
  if (context == nullptr || !work) {
    if (done) {
      done(BackgroundResult{
          {}, QStringLiteral("Background task has no owner or work.")});
    }
    return;
  }
  auto* watcher = new QFutureWatcher<BackgroundResult>(context);
  QObject::connect(watcher, &QFutureWatcher<BackgroundResult>::finished,
                   context, [watcher, done = std::move(done)]() mutable {
                     const BackgroundResult result = watcher->result();
                     watcher->deleteLater();
                     if (done) {
                       done(result);
                     }
                   });
  watcher->setFuture(QtConcurrent::run(std::move(work)));
}

}  // namespace hermes::ui
