// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_PROTOCOL_H_
#define HERMES_UI_GATEWAY_PROTOCOL_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

struct SessionMutationSpec {
  QString operation;
  QJsonObject payload;
};

// Canonical `session.create` is idempotent per `request_id`, so a retried
// create returns the first session instead of a duplicate. The canonical
// owner rejects the legacy `idempotency_key` field as unknown.
QJsonObject BuildSessionCreateParams(const QString& profile,
                                     const QString& request_id,
                                     const QString& cwd = QString());

// Revision/generation-fenced canonical mutation.
QJsonObject BuildSessionMutateParams(const QString& session_id,
                                     const QString& request_id,
                                     int expected_revision,
                                     const QString& operation,
                                     const QJsonObject& payload,
                                     int expected_generation);

// Maps a sidebar PATCH body onto the closed `session.mutate` operation set.
SessionMutationSpec SessionMutationForPatch(const QJsonObject& patch);

QJsonObject BuildSessionEventsSinceParams(const QString& session_id,
                                          const QString& profile,
                                          int last_seen);

QJsonObject BuildPromptReceiptParams(const QString& session_id,
                                     const QString& admission_id);

// Gateway-owned queue snapshot. `session.resume` and `session.info` publish
// the same row shape; parsing it here keeps the worker and the queue dialog on
// one definition. Rows remain in FIFO order.
std::vector<PendingAdmission> ParsePendingAdmissions(const QJsonArray& rows);

// A same-epoch older snapshot must not prove an admission left the queue. A
// different epoch means the sequences are incomparable and the current owner
// is authoritative. `known_revision < 0` accepts the first snapshot.
bool PendingSnapshotWins(int authority_epoch, int revision, int known_epoch,
                         int known_revision);

QJsonObject BuildPromptCancelParams(const QString& session_id,
                                    const QString& admission_id,
                                    const QString& profile = QString());
QJsonObject BuildPromptSteerQueuedParams(const QString& session_id,
                                         const QString& admission_id,
                                         const QString& profile = QString());
QJsonObject BuildPromptReplaceQueuedParams(const QString& session_id,
                                           const QString& admission_id,
                                           const QString& submission_id,
                                           const QString& text,
                                           const QString& profile = QString());
QJsonObject BuildPromptResolveUnknownParams(const QString& session_id,
                                            const QString& admission_id,
                                            int execution_generation,
                                            const QString& profile = QString());

// `connection.request` / `connection.update` / resume `pending_connection`.
// The operation is backend-owned: this parser only projects the full snapshot,
// and `BuildConnectionRespondParams` wraps the card's per-target answer.
ConnectionOperation ParseConnectionOperation(const QJsonObject& payload);
QJsonObject BuildConnectionRespondParams(const QString& op_id,
                                         const QString& session_id,
                                         const QJsonObject& result,
                                         const QString& profile = QString());

// Desktop /handoff: the platform token is the first word and case-insensitive.
QString NormalizeHandoffPlatform(const QString& args);
QString HandoffStatusText(const QString& state, const QString& platform,
                          const QString& error);

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_PROTOCOL_H_
