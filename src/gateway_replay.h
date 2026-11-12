// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_REPLAY_H_
#define HERMES_UI_GATEWAY_REPLAY_H_

#include <QHash>
#include <QString>
#include <QStringList>

namespace hermes::ui {

// Per-session sequencing watermark for the gateway's replay ring.
//
// `gateway.ready` carries a process-scoped `replay_epoch`. Sequence numbers are
// only meaningful inside one epoch, so a backend restart clears every
// watermark. Events carry a per-session monotonic `seq`; accepting an event
// only moves a watermark forward, which lets replayed frames and live frames
// race without duplicating transcript rows.
class ReplayCursor {
 public:
  // Stores the new epoch. Returns true when an existing epoch was replaced
  // (the caller must refetch state for every session it had seen).
  bool ObserveEpoch(const QString& epoch);
  QString Epoch() const;

  // Returns false for a duplicate or out-of-order frame.
  bool AcceptEvent(const QString& session_id, int seq);

  int LastSeq(const QString& session_id) const;
  void SetLastSeq(const QString& session_id, int seq);
  void ForgetSession(const QString& session_id);
  // Sessions that still have a positive watermark, in stable order.
  QStringList Sessions() const;
  void Reset();

 private:
  QString epoch_;
  QHash<QString, int> last_seq_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_REPLAY_H_
