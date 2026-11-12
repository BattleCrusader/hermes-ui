// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PENDING_PROMPT_TRACKER_H_
#define HERMES_UI_PENDING_PROMPT_TRACKER_H_

#include <QString>
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

// Follows gateway-owned pending admissions and decides when a queued prompt
// has actually entered a turn. A queued row becoming `started`/`steering` is
// the authoritative "processed" signal; a row that simply disappears is kept
// briefly so a `message.start` that beats the pending snapshot can still show
// it. Cancelled/replaced admissions are explicitly forgotten.
class PendingPromptTracker {
 public:
  // The first snapshot after selecting/resuming a session is a baseline.
  // Rows already started there belong to history, not a new announcement.
  void Reset(const std::vector<PendingAdmission>& current = {}) {
    current_.clear();
    for (const PendingAdmission& admission : current) {
      current_[admission.admission_id] = admission;
    }
    awaiting_.clear();
    announced_.clear();
  }

  // Returns rows that left `queued` for `started`/`steering` since the last
  // observation. Each row is announced at most once.
  std::vector<PendingAdmission> Observe(
      const std::vector<PendingAdmission>& next, qint64 now_ms) {
    std::map<QString, PendingAdmission> next_by_id;
    for (const PendingAdmission& admission : next) {
      next_by_id[admission.admission_id] = admission;
    }

    std::vector<PendingAdmission> entered_turn;
    for (const auto& [id, admission] : next_by_id) {
      const auto previous = current_.find(id);
      if (previous == current_.end()) {
        continue;
      }
      const bool was_queued =
          previous->second.status == QStringLiteral("queued");
      const bool now_in_turn = admission.status == QStringLiteral("started") ||
                               admission.steering ||
                               admission.status == QStringLiteral("steering");
      if (!was_queued || !now_in_turn ||
          announced_.contains(admission.admission_id)) {
        continue;
      }
      announced_.insert(admission.admission_id);
      awaiting_.erase(admission.admission_id);
      entered_turn.push_back(admission);
    }

    for (const auto& [id, previous] : current_) {
      if (next_by_id.contains(id) ||
          previous.status != QStringLiteral("queued") ||
          announced_.contains(id)) {
        continue;
      }
      awaiting_[id] = Awaiting{previous, now_ms};
    }

    current_ = std::move(next_by_id);
    return entered_turn;
  }

  // `message.start` is the last chance to surface a drained queue row when the
  // snapshot update is delayed or omitted. Expired rows are cancellations that
  // never entered a turn.
  std::optional<PendingAdmission> TakeNextAwaiting(qint64 now_ms,
                                                   qint64 max_age_ms = 5000) {
    for (auto it = awaiting_.begin(); it != awaiting_.end();) {
      if (now_ms - it->second.observed_ms > max_age_ms) {
        it = awaiting_.erase(it);
      } else {
        ++it;
      }
    }
    if (awaiting_.empty()) {
      return std::nullopt;
    }
    auto it = awaiting_.begin();
    PendingAdmission result = it->second.admission;
    awaiting_.erase(it);
    announced_.insert(result.admission_id);
    return result;
  }

  // Marks a row the UI already surfaced (e.g. prompt.steer_queued succeeded)
  // so a later pending snapshot does not duplicate the user bubble.
  bool ClaimAnnouncement(const QString& admission_id) {
    if (admission_id.isEmpty() || announced_.contains(admission_id)) {
      return false;
    }
    announced_.insert(admission_id);
    awaiting_.erase(admission_id);
    return true;
  }

  // A cancelled/replaced admission must never be announced if it briefly
  // reappears in a stale snapshot.
  void Forget(const QString& admission_id) {
    if (admission_id.isEmpty()) {
      return;
    }
    announced_.insert(admission_id);
    awaiting_.erase(admission_id);
    current_.erase(admission_id);
  }

  bool IsAnnounced(const QString& admission_id) const {
    return announced_.contains(admission_id);
  }

 private:
  struct Awaiting {
    PendingAdmission admission;
    qint64 observed_ms = 0;
  };

  std::map<QString, PendingAdmission> current_;
  std::map<QString, Awaiting> awaiting_;
  std::set<QString> announced_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PENDING_PROMPT_TRACKER_H_
