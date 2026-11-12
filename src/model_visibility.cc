// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/model_visibility.h"

#include <QRegularExpression>
#include <QStringList>
#include <utility>
#include <vector>

namespace hermes::ui {

QString ModelVisibilityKey(const QString &provider, const QString &model) {
  return provider.trimmed() + QLatin1Char('/') + model.trimmed();
}

QSet<QString> ParseHiddenModels(const QString &stored) {
  QSet<QString> hidden;
  const QStringList parts = stored.split(
      QRegularExpression(QStringLiteral("[\\n,]")), Qt::SkipEmptyParts);
  for (const QString &part : parts) {
    const QString key = part.trimmed();
    if (!key.isEmpty()) {
      hidden.insert(key);
    }
  }
  return hidden;
}

QString FormatHiddenModels(const QSet<QString> &hidden) {
  QStringList keys = hidden.values();
  keys.sort(Qt::CaseInsensitive);
  return keys.join(QLatin1Char('\n'));
}

std::vector<ModelProvider>
VisibleModelCatalog(const std::vector<ModelProvider> &providers,
                    const QSet<QString> &hidden) {
  std::vector<ModelProvider> visible;
  for (const ModelProvider &provider : providers) {
    ModelProvider filtered = provider;
    filtered.models.clear();
    for (const QString &model : provider.models) {
      if (!hidden.contains(ModelVisibilityKey(provider.slug, model))) {
        filtered.models.push_back(model);
      }
    }
    if (!filtered.models.empty()) {
      visible.push_back(std::move(filtered));
    }
  }
  return visible;
}

} // namespace hermes::ui
