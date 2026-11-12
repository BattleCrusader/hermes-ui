// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PREFERENCES_DIALOG_H_
#define HERMES_UI_PREFERENCES_DIALOG_H_

#include <QDialog>
#include <functional>
#include <vector>

#include "src/model.h"
#include "src/theme.h"

class QComboBox;
class QLabel;

namespace hermes::ui {

// Native Settings → Model surface. The desktop app's Preferences route edits
// the profile default; this dialog edits the selected session through the same
// config.set keys the desktop composer uses.
class PreferencesDialog : public QDialog {
  Q_OBJECT

 public:
  explicit PreferencesDialog(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetSnapshot(const ProfileSnapshot& snapshot);

  std::function<void(const QString& provider, const QString& model,
                     const QString& reasoning)>
      on_apply;
  std::function<void(ThemeMode mode)> on_theme;

 private:
  void RebuildProviders(const QString& provider, const QString& model);
  void RebuildModels(const QString& model);
  void Apply();
  void Style();

  Theme theme_;
  ProfileSnapshot snapshot_;
  QComboBox* theme_combo_ = nullptr;
  QComboBox* provider_ = nullptr;
  QComboBox* model_ = nullptr;
  QComboBox* reasoning_ = nullptr;
  QLabel* gateway_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PREFERENCES_DIALOG_H_
