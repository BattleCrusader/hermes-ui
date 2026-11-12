// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROJECT_DIALOG_H_
#define HERMES_UI_PROJECT_DIALOG_H_

#include <QDialog>
#include <QStringList>

#include "src/projects.h"
#include "src/theme.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

namespace hermes::ui {

// Create/edit surface for the `projects.*` RPC family. Edit mode keeps folder
// membership read-only because add/remove/set-primary are separate mutator
// RPCs applied from the sidebar menu.
class ProjectDialog : public QDialog {
 public:
  ProjectDialog(const Theme& theme, const ProjectInfo* existing,
                QWidget* parent = nullptr);

  bool is_create() const { return create_; }
  QString name() const;
  QString description() const;
  QStringList folders() const;
  QString primary_path() const;
  QStringList original_folders() const { return original_folders_; }
  QString original_primary_path() const { return original_primary_path_; }
  bool use_after_create() const;

  // Create mode only. Auto-discovered rows have no durable project record, so
  // adopting one starts from its label/path in the normal create dialog.
  void SetCreatePrefill(const QString& name, const QStringList& folders);

  void accept() override;

 private:
  void RefreshPrimaryChoices();

  const bool create_ = true;
  QStringList original_folders_;
  QString original_primary_path_;
  QLineEdit* name_ = nullptr;
  QLineEdit* description_ = nullptr;
  QPlainTextEdit* folders_ = nullptr;
  QComboBox* primary_ = nullptr;
  QCheckBox* use_ = nullptr;
  QLabel* status_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PROJECT_DIALOG_H_
