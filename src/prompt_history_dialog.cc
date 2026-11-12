// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/prompt_history_dialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace hermes::ui {
namespace {

QString Preview(const QString& text) {
  QString preview = text.simplified();
  if (preview.size() > 90) {
    preview = preview.left(90) + QStringLiteral("…");
  }
  return preview;
}

bool HasControl(const QStringList& controls, const QString& name) {
  return controls.contains(name);
}

QString ServerLabel(const PromptHistoryDialog::ServerPrompt& prompt) {
  QString prefix = QStringLiteral("Queued");
  if (prompt.steering) {
    prefix = QStringLiteral("Steering");
  } else if (prompt.status == QStringLiteral("unknown")) {
    prefix = QStringLiteral("Unknown");
  } else if (prompt.status == QStringLiteral("terminal")) {
    prefix = QStringLiteral("Settled");
  }
  return QStringLiteral("%1 · %2").arg(prefix, Preview(prompt.text));
}

}  // namespace

PromptHistoryDialog::PromptHistoryDialog(const Theme& theme,
                                         QStringList* queued,
                                         QStringList* steered, QWidget* parent)
    : QDialog(parent), queued_(queued), steered_(steered) {
  setWindowTitle(QStringLiteral("Queued & steered prompts"));
  setObjectName(QStringLiteral("queuedPromptsDialog"));
  resize(720, 560);

  const bool dark = theme.sidebar.lightness() < 128;
  setStyleSheet(
      QStringLiteral("QDialog { background: %1; color: %2; }")
          .arg(theme.sidebar.name(),
               dark ? QStringLiteral("#e8e8e8") : QStringLiteral("#202020")));

  auto* layout = new QVBoxLayout(this);
  auto* hint = new QLabel(
      QStringLiteral(
          "Queued prompts listed by Hermes can be edited, canceled, or "
          "steered into the running turn. A new chat in the desktop queue "
          "stays editable until it starts. Steered history was already "
          "delivered — editing it updates this history only, but you can "
          "load it back into the composer."),
      this);
  hint->setObjectName(QStringLiteral("queuedPromptsHint"));
  hint->setWordWrap(true);
  layout->addWidget(hint);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("queuedPromptList"));
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(list_, 1);

  auto* preview_label = new QLabel(QStringLiteral("Prompt"), this);
  preview_label->setObjectName(QStringLiteral("queuedPromptLabel"));
  layout->addWidget(preview_label);
  editor_ = new QPlainTextEdit(this);
  editor_->setObjectName(QStringLiteral("queuedPromptEditor"));
  editor_->setPlaceholderText(
      QStringLiteral("Select a prompt to view or edit it."));
  editor_->setMinimumHeight(120);
  layout->addWidget(editor_, 1);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("queuedPromptStatus"));
  status_->setWordWrap(true);
  layout->addWidget(status_);

  auto* buttons = new QHBoxLayout();
  save_ = new QPushButton(QStringLiteral("Save"), this);
  save_->setObjectName(QStringLiteral("queuedPromptEdit"));
  remove_ = new QPushButton(QStringLiteral("Delete"), this);
  remove_->setObjectName(QStringLiteral("queuedPromptRemove"));
  steer_ = new QPushButton(QStringLiteral("Inject now"), this);
  steer_->setObjectName(QStringLiteral("queuedPromptSteer"));
  steer_->setVisible(false);
  load_ = new QPushButton(QStringLiteral("Load into composer"), this);
  load_->setObjectName(QStringLiteral("queuedPromptLoad"));
  auto* close = new QPushButton(QStringLiteral("Close"), this);
  close->setObjectName(QStringLiteral("queuedPromptClose"));
  buttons->addWidget(save_);
  buttons->addWidget(remove_);
  buttons->addWidget(steer_);
  buttons->addWidget(load_);
  buttons->addStretch(1);
  buttons->addWidget(close);
  layout->addLayout(buttons);

  QObject::connect(list_, &QListWidget::currentRowChanged, this,
                   [this](int) { RefreshEditor(); });
  QObject::connect(save_, &QPushButton::clicked, this,
                   [this]() { SaveSelected(); });
  QObject::connect(remove_, &QPushButton::clicked, this,
                   [this]() { DeleteSelected(); });
  QObject::connect(steer_, &QPushButton::clicked, this,
                   [this]() { SteerSelected(); });
  QObject::connect(load_, &QPushButton::clicked, this,
                   [this]() { LoadSelected(); });
  QObject::connect(close, &QPushButton::clicked, this, &QDialog::accept);

  RebuildList();
}

void PromptHistoryDialog::SetServerPrompts(std::vector<ServerPrompt> prompts) {
  const QString preserve = selected_admission_;
  server_ = std::move(prompts);
  // Indexes move when the owner reorders its FIFO; keep the selected admission
  // by identity, and drop the selection if that admission settled or went
  // away. Reusing the old row index would silently select a different prompt.
  selected_admission_.clear();
  if (!preserve.isEmpty()) {
    for (const ServerPrompt& prompt : server_) {
      if (prompt.admission_id == preserve) {
        selected_admission_ = preserve;
        break;
      }
    }
  }
  RebuildList();
}

void PromptHistoryDialog::RemoveServerPrompt(const QString& admission_id) {
  for (auto it = server_.begin(); it != server_.end(); ++it) {
    if (it->admission_id == admission_id) {
      server_.erase(it);
      break;
    }
  }
  if (selected_admission_ == admission_id) {
    selected_admission_.clear();
  }
  RebuildList();
}

void PromptHistoryDialog::SetStatus(const QString& text) {
  status_->setText(text);
}

void PromptHistoryDialog::RebuildList() {
  list_->clear();
  for (std::size_t i = 0; i < server_.size(); ++i) {
    const ServerPrompt& prompt = server_[i];
    auto* item = new QListWidgetItem(ServerLabel(prompt), list_);
    item->setData(Qt::UserRole, static_cast<int>(Kind::kServer));
    item->setData(Qt::UserRole + 1, static_cast<int>(i));
    item->setData(Qt::UserRole + 2, prompt.admission_id);
    item->setToolTip(prompt.text);
  }
  const int queued_count = queued_ == nullptr ? 0 : queued_->size();
  for (int i = 0; i < queued_count; ++i) {
    const QString& prompt = queued_->at(i);
    auto* item = new QListWidgetItem(
        QStringLiteral("Queued · %1").arg(Preview(prompt)), list_);
    item->setData(Qt::UserRole, static_cast<int>(Kind::kQueued));
    item->setData(Qt::UserRole + 1, i);
    item->setToolTip(prompt);
  }
  const int steered_count = steered_ == nullptr ? 0 : steered_->size();
  for (int i = 0; i < steered_count; ++i) {
    const QString& prompt = steered_->at(i);
    auto* item = new QListWidgetItem(
        QStringLiteral("Steered · %1").arg(Preview(prompt)), list_);
    item->setData(Qt::UserRole, static_cast<int>(Kind::kSteered));
    item->setData(Qt::UserRole + 1, i);
    item->setToolTip(prompt);
  }
  if (list_->count() > 0) {
    int row = 0;
    if (!selected_admission_.isEmpty()) {
      for (int i = 0; i < list_->count(); ++i) {
        if (list_->item(i)->data(Qt::UserRole + 2).toString() ==
            selected_admission_) {
          row = i;
          break;
        }
      }
    }
    list_->setCurrentRow(row);
  } else {
    editor_->clear();
    status_->setText(QStringLiteral("No queued or steered prompts."));
  }
  RefreshEditor();
}

bool PromptHistoryDialog::Selected(Kind* kind, int* index) const {
  QListWidgetItem* item = list_->currentItem();
  if (item == nullptr) {
    return false;
  }
  if (kind != nullptr) {
    *kind = static_cast<Kind>(item->data(Qt::UserRole).toInt());
  }
  if (index != nullptr) {
    *index = item->data(Qt::UserRole + 1).toInt();
  }
  return true;
}

bool PromptHistoryDialog::SelectedServer(ServerPrompt* prompt) const {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index) || kind != Kind::kServer || index < 0 ||
      index >= static_cast<int>(server_.size())) {
    return false;
  }
  if (prompt != nullptr) {
    *prompt = server_[static_cast<std::size_t>(index)];
  }
  return true;
}

void PromptHistoryDialog::RefreshEditor() {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index)) {
    editor_->clear();
    editor_->setReadOnly(false);
    save_->setEnabled(false);
    remove_->setEnabled(false);
    steer_->setEnabled(false);
    steer_->setVisible(false);
    load_->setEnabled(false);
    return;
  }
  if (kind == Kind::kServer) {
    if (index < 0 || index >= static_cast<int>(server_.size())) {
      return;
    }
    const ServerPrompt& prompt = server_[static_cast<std::size_t>(index)];
    const bool can_replace =
        HasControl(prompt.controls, QStringLiteral("replace"));
    const bool can_steer =
        HasControl(prompt.controls, QStringLiteral("steer")) &&
        !prompt.steering && prompt.status == QStringLiteral("queued");
    selected_admission_ = prompt.admission_id;
    editor_->setPlainText(prompt.text);
    editor_->setReadOnly(!can_replace);
    save_->setEnabled(can_replace);
    remove_->setEnabled(true);
    steer_->setVisible(true);
    steer_->setText(QStringLiteral("Inject now"));
    steer_->setEnabled(can_steer);
    load_->setEnabled(true);
    if (prompt.status == QStringLiteral("unknown")) {
      status_->setText(
          QStringLiteral("Admission %1 was lost across an owner restart; "
                         "resolve it before the FIFO behind it can resume.")
              .arg(prompt.admission_id));
    } else if (prompt.steering) {
      status_->setText(
          QStringLiteral("Admission %1 is being delivered as a steer into the "
                         "running turn; it stays in FIFO until settled.")
              .arg(prompt.admission_id));
    } else {
      status_->setText(
          QStringLiteral("Queued admission %1 · controls: %2")
              .arg(prompt.admission_id,
                   prompt.controls.isEmpty()
                       ? QStringLiteral("none")
                       : prompt.controls.join(QStringLiteral(", "))));
    }
    return;
  }
  const QStringList* source = kind == Kind::kQueued ? queued_ : steered_;
  const bool valid = source != nullptr && index >= 0 && index < source->size();
  if (!valid) {
    editor_->clear();
    return;
  }
  selected_admission_.clear();
  editor_->setPlainText(source->at(index));
  editor_->setReadOnly(false);
  save_->setEnabled(true);
  remove_->setEnabled(true);
  steer_->setVisible(true);
  steer_->setText(kind == Kind::kSteered ? QStringLiteral("Inject again")
                                         : QStringLiteral("Inject now"));
  steer_->setEnabled(true);
  load_->setEnabled(true);
  if (kind == Kind::kSteered) {
    status_->setText(
        QStringLiteral("Steered prompts were already delivered. Changes update "
                       "this history entry only."));
  } else {
    status_->setText(
        QStringLiteral("Legacy local prompt — sent on the next turn."));
  }
}

void PromptHistoryDialog::SaveSelected() {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index)) {
    return;
  }
  if (kind == Kind::kServer) {
    ServerPrompt prompt;
    if (!SelectedServer(&prompt)) {
      return;
    }
    if (!HasControl(prompt.controls, QStringLiteral("replace"))) {
      status_->setText(
          QStringLiteral("Hermes does not expose replace for this admission."));
      return;
    }
    const QString text = editor_->toPlainText();
    if (text.trimmed().isEmpty()) {
      status_->setText(QStringLiteral("A prompt cannot be empty."));
      return;
    }
    if (!on_replace_server) {
      status_->setText(QStringLiteral("No backend replace handler is wired."));
      return;
    }
    save_->setEnabled(false);
    remove_->setEnabled(false);
    steer_->setEnabled(false);
    status_->setText(QStringLiteral("Replacing queued admission…"));
    const QString admission_id = prompt.admission_id;
    QPointer<PromptHistoryDialog> guard(this);
    on_replace_server(
        prompt, text, [guard, admission_id](bool ok, const QString& message) {
          if (guard == nullptr) {
            return;
          }
          if (ok) {
            guard->RemoveServerPrompt(admission_id);
            guard->SetStatus(
                QStringLiteral("Queued admission replaced; the edited text "
                               "sits at the FIFO tail."));
          } else {
            guard->RefreshEditor();
            guard->SetStatus(message.isEmpty()
                                 ? QStringLiteral("Replace failed.")
                                 : message);
          }
        });
    return;
  }

  QStringList* target = kind == Kind::kQueued ? queued_ : steered_;
  if (target == nullptr || index < 0 || index >= target->size()) {
    return;
  }
  const QString text = editor_->toPlainText();
  if (text.trimmed().isEmpty()) {
    status_->setText(QStringLiteral("A prompt cannot be empty."));
    return;
  }
  (*target)[index] = text;
  if (on_changed) {
    on_changed();
  }
  RebuildList();
  list_->setCurrentRow(index);
  status_->setText(kind == Kind::kQueued
                       ? QStringLiteral("Queued prompt updated.")
                       : QStringLiteral("Steered history updated."));
}

void PromptHistoryDialog::DeleteSelected() {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index)) {
    return;
  }
  if (kind == Kind::kServer) {
    ServerPrompt prompt;
    if (!SelectedServer(&prompt)) {
      return;
    }
    if (!on_cancel_server) {
      status_->setText(QStringLiteral("No backend cancel handler is wired."));
      return;
    }
    save_->setEnabled(false);
    remove_->setEnabled(false);
    steer_->setEnabled(false);
    status_->setText(QStringLiteral("Canceling queued admission…"));
    const QString admission_id = prompt.admission_id;
    QPointer<PromptHistoryDialog> guard(this);
    on_cancel_server(
        prompt, [guard, admission_id](bool ok, const QString& message) {
          if (guard == nullptr) {
            return;
          }
          if (ok) {
            guard->RemoveServerPrompt(admission_id);
            guard->SetStatus(QStringLiteral("Queued admission canceled."));
          } else {
            guard->RefreshEditor();
            guard->SetStatus(
                message.isEmpty() ? QStringLiteral("Cancel failed.") : message);
          }
        });
    return;
  }

  QStringList* target = kind == Kind::kQueued ? queued_ : steered_;
  if (target == nullptr || index < 0 || index >= target->size()) {
    return;
  }
  target->removeAt(index);
  if (on_changed) {
    on_changed();
  }
  RebuildList();
  if (list_->count() > 0) {
    list_->setCurrentRow(std::min(index, list_->count() - 1));
  }
  status_->setText(kind == Kind::kQueued
                       ? QStringLiteral("Queued prompt deleted.")
                       : QStringLiteral("Steered history entry deleted."));
}

void PromptHistoryDialog::SteerSelected() {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index)) {
    return;
  }
  if (kind == Kind::kServer) {
    ServerPrompt prompt;
    if (!SelectedServer(&prompt) || !on_steer_server) {
      return;
    }
    if (!HasControl(prompt.controls, QStringLiteral("steer")) ||
        prompt.steering || prompt.status != QStringLiteral("queued")) {
      status_->setText(
          QStringLiteral("This admission can no longer be injected."));
      return;
    }
    save_->setEnabled(false);
    remove_->setEnabled(false);
    steer_->setEnabled(false);
    status_->setText(QStringLiteral("Injecting queued admission…"));
    const QString admission_id = prompt.admission_id;
    QPointer<PromptHistoryDialog> guard(this);
    on_steer_server(prompt, [guard, admission_id](bool ok,
                                                  const QString& message) {
      if (guard == nullptr) {
        return;
      }
      if (ok) {
        guard->RemoveServerPrompt(admission_id);
        guard->SetStatus(
            QStringLiteral("Admission is steering into the running turn."));
      } else {
        guard->RefreshEditor();
        guard->SetStatus(message.isEmpty() ? QStringLiteral("Injection failed.")
                                           : message);
      }
    });
    return;
  }

  const QStringList* source = kind == Kind::kQueued ? queued_ : steered_;
  if (source == nullptr || index < 0 || index >= source->size()) {
    return;
  }
  if (!on_inject_local) {
    status_->setText(QStringLiteral("No local inject handler is wired."));
    return;
  }
  const QString text = source->at(index);
  save_->setEnabled(false);
  remove_->setEnabled(false);
  steer_->setEnabled(false);
  status_->setText(QStringLiteral("Injecting into the running turn…"));
  QPointer<PromptHistoryDialog> guard(this);
  QStringList* queued = queued_;
  std::function<void()> changed = on_changed;
  on_inject_local(text, [guard, queued, changed, kind, index, text](
                            bool ok, const QString& message) {
    if (!ok) {
      if (guard != nullptr) {
        guard->RefreshEditor();
        guard->SetStatus(message.isEmpty() ? QStringLiteral("Injection failed.")
                                           : message);
      }
      return;
    }
    // The local queue belongs to MainWindow, so retire the entry even if
    // the modal was dismissed while the steer ACK was in flight.
    if (kind == Kind::kQueued && queued != nullptr && index >= 0 &&
        index < queued->size() && queued->at(index) == text) {
      queued->removeAt(index);
      if (changed) {
        changed();
      }
    }
    if (guard == nullptr) {
      return;
    }
    guard->RebuildList();
    if (guard->list_->count() > 0) {
      guard->list_->setCurrentRow(std::min(index, guard->list_->count() - 1));
    }
    guard->SetStatus(
        kind == Kind::kQueued
            ? QStringLiteral("Queued prompt injected into the running turn.")
            : QStringLiteral("Prompt injected into the running turn again."));
  });
}

void PromptHistoryDialog::LoadSelected() {
  Kind kind = Kind::kQueued;
  int index = -1;
  if (!Selected(&kind, &index)) {
    return;
  }
  if (kind == Kind::kServer) {
    if (!SelectedServer(nullptr)) {
      return;
    }
    if (on_load) {
      on_load(server_[static_cast<std::size_t>(index)].text);
    }
    accept();
    return;
  }
  const QStringList* source = kind == Kind::kQueued ? queued_ : steered_;
  if (source == nullptr || index < 0 || index >= source->size()) {
    return;
  }
  if (on_load) {
    on_load(source->at(index));
  }
  accept();
}

}  // namespace hermes::ui
