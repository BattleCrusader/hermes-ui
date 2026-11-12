// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_FALLBACK_MODELS_H_
#define HERMES_UI_FALLBACK_MODELS_H_

#include <QJsonArray>
#include <QString>

namespace hermes::ui {

// `fallback_providers` is stored as an ordered list of {provider, model}
// objects. Legacy string entries ("provider/model") are accepted on read.
// Parsing keeps every extra key of a pair that already exists so an editor
// never rewrites a local-gateway chain as bare provider/model pairs.
QString FormatFallbackChain(const QJsonArray& entries);
QJsonArray ParseFallbackChain(const QString& text, const QJsonArray& previous);

}  // namespace hermes::ui

#endif  // HERMES_UI_FALLBACK_MODELS_H_
