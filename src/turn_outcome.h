// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_TURN_OUTCOME_H_
#define HERMES_UI_TURN_OUTCOME_H_

#include <algorithm>
#include <cstddef>
#include <utility>

#include "src/model.h"

namespace hermes::ui {

// A completion notification is only useful while it is still news. A notice
// that has sat behind a modal, an event-loop stall, or a reconnect replay must
// not fire just because the user happened to submit a new prompt.
inline bool CompletionNoticeIsCurrent(const ProfileSnapshot& snapshot,
                                      const TurnCompletionNotice& notice,
                                      qint64 now_ms) {
  // The same chat already started another turn, so this notice belongs to the
  // turn before it. That turn was visible when it ended; announcing it while
  // the replacement is still starting reads as a completion before a begin.
  if (snapshot.running && !snapshot.running_session_id.isEmpty() &&
      snapshot.running_session_id == notice.session_id) {
    return false;
  }
  constexpr qint64 kMaxNoticeAgeMs = 10000;
  return notice.created_ms <= 0 ||
         now_ms - notice.created_ms <= kMaxNoticeAgeMs;
}

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
inline void BeginTurn(ProfileSnapshot* snapshot, const QString& prompt = {}) {
  if (snapshot == nullptr) {
    return;
  }
  snapshot->running = true;
  snapshot->running_session_id = snapshot->selected_session_id;
  snapshot->turn_failed = false;
  snapshot->turn_error_text.clear();
  // Empty clears the old prompt so a turn whose text the UI never saw cannot
  // reuse the previous banner. A steer never calls BeginTurn, so it can never
  // become the prompt named by the next completion.
  snapshot->turn_prompt_text = prompt.trimmed();
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

inline void RecordTurnCompletion(ProfileSnapshot* snapshot,
                                 const QString& session_id,
                                 const QString& prompt, const QString& failure,
                                 qint64 elapsed_ms) {
  if (snapshot == nullptr || session_id.isEmpty()) {
    return;
  }
  TurnCompletionNotice notice;
  notice.serial = ++snapshot->turn_completion_serial;
  notice.session_id = session_id;
  notice.prompt = prompt.trimmed();
  notice.failed = !failure.isEmpty();
  notice.error = failure;
  notice.elapsed_ms = std::max<qint64>(0, elapsed_ms);
  notice.created_ms = QDateTime::currentMSecsSinceEpoch();
  snapshot->turn_completions.push_back(std::move(notice));
  // A long-lived worker must not grow this queue without bound if the shell
  // polls slowly. The UI only needs the newest handful.
  constexpr std::size_t kMaxCompletionNotices = 16;
  if (snapshot->turn_completions.size() > kMaxCompletionNotices) {
    snapshot->turn_completions.erase(
        snapshot->turn_completions.begin(),
        snapshot->turn_completions.end() - kMaxCompletionNotices);
  }
}

// Completes a turn for a chat that is not currently selected. The transcript
// stays put, but the profile's running claim is cleared when it belongs to the
// finished session, and the shell gets one notification row either way.
inline void CompleteBackgroundTurn(ProfileSnapshot* snapshot,
                                   const QString& session_id,
                                   const QString& prompt,
                                   const QString& failure,
                                   qint64 elapsed_ms, bool notify = true) {
  if (snapshot == nullptr || session_id.isEmpty()) {
    return;
  }
  if (snapshot->running &&
      (snapshot->running_session_id.isEmpty() ||
       snapshot->running_session_id == session_id)) {
    CompleteTurn(snapshot, failure);
  }
  if (notify) {
    RecordTurnCompletion(snapshot, session_id, prompt, failure, elapsed_ms);
  }
}

}  // namespace hermes::ui

#endif  // HERMES_UI_TURN_OUTCOME_H_
