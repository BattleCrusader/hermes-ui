// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/project_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
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
  setProperty("draftScope",
              create_ ? QStringLiteral("new")
                      : QStringLiteral("project:") + existing->id);
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
  auto* folder_row = new QHBoxLayout();
  folder_row->addWidget(folders_, 1);
  auto* browse = new QPushButton(QStringLiteral("Add folder…"), this);
  browse->setObjectName(QStringLiteral("projectBrowseFolder"));
  browse->setToolTip(QStringLiteral("Choose a folder in the filesystem"));
  QObject::connect(browse, &QPushButton::clicked, this, [this]() {
    const QStringList current = folders();
    const QString start = current.isEmpty() ? QDir::homePath() : current.last();
    const QString path = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Add project folder"), start);
    AddFolderPath(path);
  });
  folder_row->addWidget(browse, 0, Qt::AlignTop);
  layout->addLayout(folder_row, 1);

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
    // Keep the text editor read-only so membership changes are deliberate;
    // the Add folder button stages new paths and the shell applies them after
    // the display update is accepted.
    folders_->setReadOnly(true);
    primary_->setEnabled(false);
    status_->setText(
        QStringLiteral("Use Add folder… to attach a directory, or the project "
                       "menu to remove or set the primary folder."));
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

void ProjectDialog::AddFolderPath(const QString& path) {
  const QString raw = path.trimmed();
  if (raw.isEmpty()) {
    return;
  }
  const QString cleaned = QDir::cleanPath(raw);
  const QFileInfo info(cleaned);
  if (!info.exists() || !info.isDir()) {
    status_->setText(
        QStringLiteral("Not a folder: %1").arg(cleaned));
    return;
  }
  QStringList current = folders();
  if (current.contains(cleaned)) {
    status_->setText(QStringLiteral("That folder is already attached."));
    return;
  }
  current.push_back(cleaned);
  folders_->setPlainText(current.join(QLatin1Char('\n')));
  RefreshPrimaryChoices();
  status_->setText(QStringLiteral("Folder added: %1").arg(cleaned));
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
