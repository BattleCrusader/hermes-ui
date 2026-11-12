// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/session_transfer.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace hermes::ui {

bool ParseSessionImport(const QByteArray &bytes, QJsonArray *sessions,
                        QString *error) {
  if (sessions != nullptr) {
    *sessions = QJsonArray();
  }
  QJsonParseError parse_error;
  const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse_error);
  if (parse_error.error != QJsonParseError::NoError) {
    if (error != nullptr) {
      *error = parse_error.errorString();
    }
    return false;
  }
  QJsonArray parsed;
  if (document.isArray()) {
    parsed = document.array();
  } else if (document.isObject()) {
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("sessions")).isArray()) {
      parsed = object.value(QStringLiteral("sessions")).toArray();
    } else if (object.contains(QStringLiteral("id"))) {
      parsed.append(object);
    }
  }
  if (parsed.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No sessions found in that file.");
    }
    return false;
  }
  if (sessions != nullptr) {
    *sessions = parsed;
  }
  return true;
}

} // namespace hermes::ui
