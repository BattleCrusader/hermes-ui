// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_replay.h"

#include <algorithm>

namespace hermes::ui {

bool ReplayCursor::ObserveEpoch(const QString &epoch) {
  if (epoch.isEmpty() || epoch == epoch_) {
    return false;
  }
  const bool changed = !epoch_.isEmpty();
  epoch_ = epoch;
  last_seq_.clear();
  return changed;
}

QString ReplayCursor::Epoch() const { return epoch_; }

bool ReplayCursor::AcceptEvent(const QString &session_id, int seq) {
  if (session_id.isEmpty() || seq <= 0) {
    return true;
  }
  const int last = last_seq_.value(session_id, 0);
  if (seq <= last) {
    return false;
  }
  last_seq_.insert(session_id, seq);
  return true;
}

int ReplayCursor::LastSeq(const QString &session_id) const {
  return last_seq_.value(session_id, 0);
}

void ReplayCursor::SetLastSeq(const QString &session_id, int seq) {
  if (session_id.isEmpty() || seq < 0) {
    return;
  }
  last_seq_.insert(session_id, std::max(last_seq_.value(session_id, 0), seq));
}

void ReplayCursor::ForgetSession(const QString &session_id) {
  last_seq_.remove(session_id);
}

QStringList ReplayCursor::Sessions() const {
  QStringList sessions = last_seq_.keys();
  std::sort(sessions.begin(), sessions.end());
  QStringList with_watermark;
  for (const QString &session : sessions) {
    if (last_seq_.value(session, 0) > 0) {
      with_watermark.push_back(session);
    }
  }
  return with_watermark;
}

void ReplayCursor::Reset() {
  epoch_.clear();
  last_seq_.clear();
}

} // namespace hermes::ui
