// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_TURN_OUTCOME_H_
#define HERMES_UI_TURN_OUTCOME_H_

#include "src/model.h"

namespace hermes::ui {

// A background sync failure (session list, message refresh, control socket)
// belongs in the status bar, not in the next completion notification. Only an
// error that actually ends a live turn should set the turn-failure fields.
inline void ApplyProfileError(ProfileSnapshot* snapshot, const QString& error,
                              bool ends_turn) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->error_text = error;
  snapshot->status_text = QStringLiteral("error");
  if (!ends_turn) {
    return;
  }
  const bool streaming =
      !snapshot->messages.empty() &&
      (snapshot->messages.back().streaming ||
       snapshot->messages.back().reasoning_streaming);
  const bool live_turn = snapshot->running || streaming;
  if (!live_turn) {
    return;
  }
  for (ChatMessage& message : snapshot->messages) {
    message.streaming = false;
    message.reasoning_streaming = false;
  }
  snapshot->turn_failed = true;
  snapshot->turn_error_text =
      error.trimmed().isEmpty() ? QStringLiteral("The turn failed") : error;
  snapshot->running = false;
  snapshot->running_session_id.clear();
}

// Starts a new turn with a clean outcome. A stale failure from the previous
// turn must never bleed into the next completion notification.
inline void BeginTurn(ProfileSnapshot* snapshot) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->running = true;
  snapshot->running_session_id = snapshot->selected_session_id;
  snapshot->turn_failed = false;
  snapshot->turn_error_text.clear();
  snapshot->error_text.clear();
  snapshot->status_text = QStringLiteral("working");
}

// Folds a `message.complete` into the snapshot. An empty `failure` is success;
// anything else is a real turn failure and is safe to notify on.
inline void CompleteTurn(ProfileSnapshot* snapshot, const QString& failure) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->running = false;
  snapshot->running_session_id.clear();
  if (failure.isEmpty()) {
    snapshot->turn_failed = false;
    snapshot->turn_error_text.clear();
    snapshot->error_text.clear();
    snapshot->status_text = QStringLiteral("ready");
    return;
  }
  snapshot->turn_failed = true;
  snapshot->turn_error_text = failure;
  snapshot->error_text = failure;
  snapshot->status_text = QStringLiteral("error");
}

}  // namespace hermes::ui

#endif  // HERMES_UI_TURN_OUTCOME_H_
