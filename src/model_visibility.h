// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MODEL_VISIBILITY_H_
#define HERMES_UI_MODEL_VISIBILITY_H_

#include <QSet>
#include <QString>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

QString ModelVisibilityKey(const QString& provider, const QString& model);
QSet<QString> ParseHiddenModels(const QString& stored);
QString FormatHiddenModels(const QSet<QString>& hidden);

// Provider/model rows with hidden entries removed. A provider whose entire
// catalog is hidden is omitted so the picker never opens an empty submenu.
std::vector<ModelProvider> VisibleModelCatalog(
    const std::vector<ModelProvider>& providers, const QSet<QString>& hidden);

}  // namespace hermes::ui

#endif  // HERMES_UI_MODEL_VISIBILITY_H_
