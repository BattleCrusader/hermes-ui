// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/quick_entry.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <utility>

namespace hermes::ui {

QuickEntryDialog::QuickEntryDialog(QWidget* parent) : QDialog(parent) {
  setObjectName(QStringLiteral("quickEntryDialog"));
  setWindowTitle(QStringLiteral("Quick entry"));
  setWindowFlag(Qt::Tool, true);
  setWindowFlag(Qt::WindowStaysOnTopHint, true);
  resize(560, 190);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(16, 14, 16, 14);
  layout->setSpacing(8);

  session_label_ = new QLabel(this);
  session_label_->setObjectName(QStringLiteral("quickEntrySession"));
  session_label_->setStyleSheet(
      QStringLiteral("color: palette(mid); font-size: 11px;"));
  layout->addWidget(session_label_);

  text_ = new QPlainTextEdit(this);
  text_->setObjectName(QStringLiteral("quickEntryText"));
  text_->setPlaceholderText(
      QStringLiteral("Ask Hermes anything — Enter sends, Shift+Enter adds a "
                     "line"));
  text_->setMinimumHeight(84);
  text_->installEventFilter(this);
  layout->addWidget(text_, 1);

  auto* footer = new QHBoxLayout();
  footer->setContentsMargins(0, 0, 0, 0);
  footer->setSpacing(8);
  hint_ = new QLabel(QStringLiteral("Starts a new chat · Esc closes"), this);
  hint_->setObjectName(QStringLiteral("quickEntryHint"));
  hint_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  footer->addWidget(hint_, 1);
  submit_ = new QPushButton(QStringLiteral("Send"), this);
  submit_->setObjectName(QStringLiteral("quickEntrySubmit"));
  submit_->setDefault(true);
  QObject::connect(submit_, &QPushButton::clicked, this,
                   [this]() { Submit(); });
  footer->addWidget(submit_);
  layout->addLayout(footer);
}

void QuickEntryDialog::SetSubmitHandler(
    std::function<void(const QString&)> handler) {
  submit_handler_ = std::move(handler);
}

void QuickEntryDialog::SetSessionLabel(const QString& label) {
  session_label_->setText(label);
}

void QuickEntryDialog::FocusEditor() {
  text_->setFocus(Qt::ShortcutFocusReason);
  text_->moveCursor(QTextCursor::End);
}

QString QuickEntryDialog::Text() const { return text_->toPlainText(); }

void QuickEntryDialog::SetText(const QString& text) {
  text_->setPlainText(text);
  text_->moveCursor(QTextCursor::End);
}

bool QuickEntryDialog::eventFilter(QObject* watched, QEvent* event) {
  if (watched == text_ && event->type() == QEvent::KeyPress) {
    auto* key = static_cast<QKeyEvent*>(event);
    if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
        (key->modifiers() & Qt::ShiftModifier) == 0) {
      Submit();
      return true;
    }
  }
  return QDialog::eventFilter(watched, event);
}

void QuickEntryDialog::Submit() {
  const QString text = Text().trimmed();
  if (text.isEmpty()) {
    hint_->setText(QStringLiteral("Type a prompt first."));
    return;
  }
  if (submit_handler_) {
    submit_handler_(text);
  }
  text_->clear();
  hide();
}

}  // namespace hermes::ui
