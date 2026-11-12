// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SESSION_TRANSFER_H_
#define HERMES_UI_SESSION_TRANSFER_H_

#include <QByteArray>
#include <QJsonArray>
#include <QString>

namespace hermes::ui {

// Accepts the three shapes a session export can take: the gateway's
// `{"sessions": [...]}` payload, one exported session object with `id`, or a
// bare array of session objects. Returns false with a human-readable error for
// malformed or empty input.
bool ParseSessionImport(const QByteArray &bytes, QJsonArray *sessions,
                        QString *error);

} // namespace hermes::ui

#endif // HERMES_UI_SESSION_TRANSFER_H_
