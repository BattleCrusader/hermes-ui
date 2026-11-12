// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/fallback_models.h"

#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>

namespace hermes::ui {

QString FormatFallbackChain(const QJsonArray& entries) {
  QStringList lines;
  for (const QJsonValue& value : entries) {
    if (value.isObject()) {
      const QJsonObject entry = value.toObject();
      const QString provider =
          entry.value(QStringLiteral("provider")).toString();
      const QString model = entry.value(QStringLiteral("model")).toString();
      if (!provider.isEmpty() || !model.isEmpty()) {
        lines.append(provider + QLatin1Char('/') + model);
      }
    } else if (value.isString()) {
      lines.append(value.toString());
    }
  }
  return lines.join(QStringLiteral(", "));
}

QJsonArray ParseFallbackChain(const QString& text, const QJsonArray& previous) {
  QJsonArray entries;
  const QStringList parts = text.split(
      QRegularExpression(QStringLiteral("[\\n,]")), Qt::SkipEmptyParts);
  for (const QString& raw : parts) {
    const QString line = raw.trimmed();
    if (line.isEmpty()) {
      continue;
    }
    const int slash = line.indexOf(QLatin1Char('/'));
    const QString provider = slash > 0 ? line.left(slash).trimmed() : QString();
    const QString model = slash > 0 ? line.mid(slash + 1).trimmed() : line;
    if (model.isEmpty()) {
      continue;
    }
    QJsonObject entry;
    for (const QJsonValue& value : previous) {
      const QJsonObject old = value.toObject();
      if (old.value(QStringLiteral("provider")).toString() == provider &&
          old.value(QStringLiteral("model")).toString() == model) {
        entry = old;
        break;
      }
    }
    entry.insert(QStringLiteral("provider"), provider);
    entry.insert(QStringLiteral("model"), model);
    entries.append(entry);
  }
  return entries;
}

}  // namespace hermes::ui
