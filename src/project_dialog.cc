// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/project_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>
#include <algorithm>

namespace hermes::ui {
namespace {

QStringList ParsedFolders(const QString& text) {
  QStringList folders;
  QSet<QString> seen;
  for (const QString& raw : text.split(QLatin1Char('\n'))) {
    const QString path = raw.trimmed();
    if (!path.isEmpty() && !seen.contains(path)) {
      seen.insert(path);
      folders.push_back(path);
    }
  }
  return folders;
}

}  // namespace

ProjectDialog::ProjectDialog(const Theme& theme, const ProjectInfo* existing,
                             QWidget* parent)
    : QDialog(parent), create_(existing == nullptr) {
  setWindowTitle(create_ ? QStringLiteral("New project")
                         : QStringLiteral("Edit project"));
  setObjectName(QStringLiteral("projectDialog"));
  resize(560, 520);

  const bool dark = theme.sidebar.lightness() < 128;
  setStyleSheet(
      QStringLiteral("QDialog { background: %1; color: %2; }")
          .arg(theme.sidebar.name(),
               dark ? QStringLiteral("#e8e8e8") : QStringLiteral("#202020")));

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Name"), this));
  name_ = new QLineEdit(this);
  name_->setObjectName(QStringLiteral("projectName"));
  name_->setPlaceholderText(QStringLiteral("Project name"));
  layout->addWidget(name_);

  layout->addWidget(new QLabel(QStringLiteral("Description"), this));
  description_ = new QLineEdit(this);
  description_->setObjectName(QStringLiteral("projectDescription"));
  description_->setPlaceholderText(QStringLiteral("Optional description"));
  layout->addWidget(description_);

  layout->addWidget(
      new QLabel(QStringLiteral("Folders (one absolute path per line)"), this));
  folders_ = new QPlainTextEdit(this);
  folders_->setObjectName(QStringLiteral("projectFolders"));
  folders_->setPlaceholderText(QStringLiteral("/Users/you/code/project"));
  folders_->setMinimumHeight(110);
  layout->addWidget(folders_, 1);

  layout->addWidget(new QLabel(QStringLiteral("Primary folder"), this));
  primary_ = new QComboBox(this);
  primary_->setObjectName(QStringLiteral("projectPrimary"));
  layout->addWidget(primary_);

  use_ = new QCheckBox(QStringLiteral("Use this project after creating"), this);
  use_->setObjectName(QStringLiteral("projectUse"));
  use_->setChecked(true);
  use_->setVisible(create_);
  layout->addWidget(use_);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("projectDialogStatus"));
  status_->setWordWrap(true);
  layout->addWidget(status_);

  auto* buttons = new QDialogButtonBox(
      QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
  buttons->button(QDialogButtonBox::Save)
      ->setObjectName(QStringLiteral("projectSave"));
  buttons->button(QDialogButtonBox::Cancel)
      ->setObjectName(QStringLiteral("projectCancel"));
  QObject::connect(buttons, &QDialogButtonBox::accepted, this,
                   &ProjectDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, this,
                   &QDialog::reject);
  layout->addWidget(buttons);

  if (existing != nullptr) {
    name_->setText(existing->name);
    description_->setText(existing->description);
    QStringList paths;
    for (const ProjectFolderInfo& folder : existing->folders) {
      paths.push_back(folder.path);
      original_folders_.push_back(folder.path);
    }
    if (!existing->primary_path.isEmpty() &&
        !paths.contains(existing->primary_path)) {
      paths.push_front(existing->primary_path);
      original_folders_.push_front(existing->primary_path);
    }
    folders_->setPlainText(paths.join(QLatin1Char('\n')));
    original_primary_path_ = existing->primary_path;
    RefreshPrimaryChoices();
    primary_->setCurrentIndex(
        std::max(0, primary_->findData(existing->primary_path)));
    // Folder membership and the primary path are separate mutator verbs.
    // Keep this dialog focused on display fields; the sidebar menu applies
    // add/remove/set-primary so every step has its own gateway receipt.
    folders_->setReadOnly(true);
    primary_->setEnabled(false);
    status_->setText(
        QStringLiteral("Use the project menu to add, remove, or set the "
                       "primary folder."));
  } else {
    RefreshPrimaryChoices();
  }

  QObject::connect(folders_, &QPlainTextEdit::textChanged, this,
                   [this]() { RefreshPrimaryChoices(); });
}

void ProjectDialog::RefreshPrimaryChoices() {
  const QString selected =
      primary_ == nullptr ? QString() : primary_->currentData().toString();
  if (primary_ == nullptr) {
    return;
  }
  const QSignalBlocker blocker(primary_);
  primary_->clear();
  primary_->addItem(QStringLiteral("Automatic (first folder)"), QString());
  for (const QString& path : ParsedFolders(folders_->toPlainText())) {
    primary_->addItem(path, path);
  }
  const int index = primary_->findData(selected);
  primary_->setCurrentIndex(index >= 0 ? index : 0);
}

QString ProjectDialog::name() const { return name_->text().trimmed(); }

QString ProjectDialog::description() const {
  return description_->text().trimmed();
}

QStringList ProjectDialog::folders() const {
  return ParsedFolders(folders_->toPlainText());
}

QString ProjectDialog::primary_path() const {
  const QString path = primary_->currentData().toString().trimmed();
  const QStringList parsed = folders();
  if (!path.isEmpty() && parsed.contains(path)) {
    return path;
  }
  return parsed.isEmpty() ? QString() : parsed.first();
}

bool ProjectDialog::use_after_create() const {
  return create_ && use_->isChecked();
}

void ProjectDialog::SetCreatePrefill(const QString& name,
                                     const QStringList& folders) {
  if (!create_) {
    return;
  }
  name_->setText(name);
  folders_->setPlainText(folders.join(QLatin1Char('\n')));
  RefreshPrimaryChoices();
}

void ProjectDialog::accept() {
  if (name().isEmpty()) {
    status_->setText(QStringLiteral("A project needs a name."));
    name_->setFocus();
    return;
  }
  QDialog::accept();
}

}  // namespace hermes::ui
