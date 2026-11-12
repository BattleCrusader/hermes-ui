// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/preferences_dialog.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace hermes::ui {

PreferencesDialog::PreferencesDialog(const Theme &theme, QWidget *parent)
    : QDialog(parent), theme_(theme) {
  setObjectName(QStringLiteral("preferences"));
  setWindowTitle(QStringLiteral("Preferences"));
  setModal(false);
  setMinimumWidth(500);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(20, 18, 20, 16);
  root->setSpacing(12);

  auto *title = new QLabel(QStringLiteral("Preferences"), this);
  title->setObjectName(QStringLiteral("preferencesTitle"));
  root->addWidget(title);

  auto *appearance = new QLabel(QStringLiteral("Appearance"), this);
  appearance->setObjectName(QStringLiteral("preferencesSection"));
  root->addWidget(appearance);
  theme_combo_ = new QComboBox(this);
  theme_combo_->setObjectName(QStringLiteral("preferencesTheme"));
  theme_combo_->addItem(QStringLiteral("Light"));
  theme_combo_->addItem(QStringLiteral("Dark"));
  connect(theme_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int index) {
            if (on_theme) {
              on_theme(index == 1 ? ThemeMode::kDark : ThemeMode::kLight);
            }
          });
  root->addWidget(theme_combo_);

  auto *model_heading = new QLabel(QStringLiteral("Model"), this);
  model_heading->setObjectName(QStringLiteral("preferencesSection"));
  root->addWidget(model_heading);

  auto *form = new QFormLayout();
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(8);
  provider_ = new QComboBox(this);
  provider_->setObjectName(QStringLiteral("preferencesProvider"));
  connect(provider_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) {
            RebuildModels(model_ != nullptr ? model_->currentData().toString()
                                            : QString());
          });
  form->addRow(QStringLiteral("Provider"), provider_);

  model_ = new QComboBox(this);
  model_->setObjectName(QStringLiteral("preferencesModel"));
  form->addRow(QStringLiteral("Model"), model_);

  reasoning_ = new QComboBox(this);
  reasoning_->setObjectName(QStringLiteral("preferencesReasoning"));
  for (const QString &level :
       {QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("medium"),
        QStringLiteral("high")}) {
    reasoning_->addItem(level, level);
  }
  reasoning_->setCurrentText(QStringLiteral("medium"));
  form->addRow(QStringLiteral("Reasoning"), reasoning_);
  root->addLayout(form);

  gateway_ = new QLabel(this);
  gateway_->setObjectName(QStringLiteral("preferencesGateway"));
  root->addWidget(gateway_);

  auto *actions = new QHBoxLayout();
  actions->addStretch(1);
  auto *close = new QPushButton(QStringLiteral("Close"), this);
  close->setObjectName(QStringLiteral("preferencesClose"));
  connect(close, &QPushButton::clicked, this, &QDialog::reject);
  actions->addWidget(close);
  auto *apply = new QPushButton(QStringLiteral("Apply"), this);
  apply->setObjectName(QStringLiteral("preferencesApply"));
  apply->setDefault(true);
  connect(apply, &QPushButton::clicked, this, [this]() { Apply(); });
  actions->addWidget(apply);
  root->addLayout(actions);

  SetTheme(theme);
}

void PreferencesDialog::SetTheme(const Theme &theme) {
  theme_ = theme;
  Style();
}

void PreferencesDialog::SetSnapshot(const ProfileSnapshot &snapshot) {
  snapshot_ = snapshot;
  {
    const QSignalBlocker blocker(theme_combo_);
    theme_combo_->setCurrentIndex(theme_.background.lightness() > 128 ? 0 : 1);
  }
  gateway_->setText(QStringLiteral("Gateway %1 · %2")
                        .arg(snapshot.gateway_version.isEmpty()
                                 ? QStringLiteral("unknown")
                                 : snapshot.gateway_version,
                             snapshot.label));
  RebuildProviders(snapshot.provider, snapshot.model);
  {
    const QSignalBlocker blocker(reasoning_);
    const int index = reasoning_->findData(snapshot.reasoning_effort);
    if (index >= 0) {
      reasoning_->setCurrentIndex(index);
    }
  }
}

void PreferencesDialog::RebuildProviders(const QString &provider,
                                         const QString &model) {
  const QSignalBlocker blocker(provider_);
  provider_->clear();
  for (const ModelProvider &row : snapshot_.model_providers) {
    provider_->addItem(row.name.isEmpty() ? row.slug : row.name, row.slug);
  }
  if (provider_->count() == 0) {
    provider_->addItem(provider.isEmpty() ? QStringLiteral("No provider")
                                          : provider,
                       provider);
  }
  const int index = provider_->findData(provider);
  provider_->setCurrentIndex(index >= 0 ? index : 0);
  RebuildModels(model);
}

void PreferencesDialog::RebuildModels(const QString &model) {
  const QSignalBlocker blocker(model_);
  model_->clear();
  const QString slug = provider_->currentData().toString();
  for (const ModelProvider &row : snapshot_.model_providers) {
    if (row.slug != slug) {
      continue;
    }
    for (const QString &id : row.models) {
      model_->addItem(id, id);
    }
    break;
  }
  if (model_->count() == 0 && !model.isEmpty()) {
    model_->addItem(model, model);
  }
  const int index = model_->findData(model);
  model_->setCurrentIndex(index >= 0 ? index : 0);
}

void PreferencesDialog::Apply() {
  if (!on_apply) {
    return;
  }
  const QString provider = provider_->currentData().toString();
  const QString model = model_->currentData().toString();
  const QString reasoning = reasoning_->currentData().toString();
  on_apply(provider, model, reasoning);
}

void PreferencesDialog::Style() {
  setStyleSheet(
      QStringLiteral(
          "QDialog#preferences { background: %1; color: %2; }"
          "QLabel { color: %2; }"
          "QLabel#preferencesTitle { font-size: 16px; font-weight: 600; }"
          "QLabel#preferencesSection { color: %3; font-size: 11px; "
          "font-weight: 600; text-transform: uppercase; }"
          "QComboBox { background: %4; color: %2; border: 1px solid %5; "
          "border-radius: 6px; padding: 4px 8px; min-height: 22px; }"
          "QPushButton { background: %4; color: %2; border: 1px solid %5; "
          "border-radius: 6px; padding: 5px 14px; }"
          "QPushButton:hover { background: %6; }"
          "QPushButton#preferencesApply { background: %7; color: white; "
          "border-color: %7; }")
          .arg(theme_.background.name(), theme_.foreground.name(),
               theme_.text_tertiary.name(), theme_.surface.name(),
               theme_.stroke.name(), theme_.hover.name(),
               theme_.accent.name()));
}

} // namespace hermes::ui
