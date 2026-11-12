// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/detached_chat.h"

#include <QLabel>
#include <QVBoxLayout>
#include <memory>

#include "src/chat_view.h"
#include "src/composer.h"

namespace hermes::ui {

DetachedChatWindow::DetachedChatWindow(const Theme& theme, QWidget* parent)
    : QWidget(parent, Qt::Window), theme_(theme) {
  setObjectName(QStringLiteral("detachedChat"));
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowTitle(QStringLiteral("Hermes · chat"));
  resize(940, 780);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  chat_view_ = new ChatView(theme_, this);
  layout->addWidget(chat_view_, 1);

  auto* composer_host = new QWidget(this);
  auto* composer_layout = new QVBoxLayout(composer_host);
  composer_layout->setContentsMargins(24, 0, 24, 8);
  composer_layout->setSpacing(4);
  status_ = new QLabel(composer_host);
  status_->setObjectName(QStringLiteral("detachedChatStatus"));
  status_->setVisible(false);
  composer_layout->addWidget(status_);
  composer_ = new Composer(theme_, composer_host);
  composer_->setMaximumWidth(780);
  composer_layout->addWidget(composer_, 0, Qt::AlignHCenter);
  composer_host->setFixedHeight(144);
  QObject::connect(composer_, &Composer::HeightChanged, composer_host,
                   [composer_host](int height) {
                     composer_host->setFixedHeight(height + 28);
                   });
  layout->addWidget(composer_host);
  SetTheme(theme_);
}

void DetachedChatWindow::SetTheme(const Theme& theme) {
  theme_ = theme;
  setStyleSheet(QStringLiteral("QWidget#detachedChat { background: %1; }")
                    .arg(theme.background.name()));
  chat_view_->SetTheme(theme);
  composer_->SetTheme(theme);
  if (status_ != nullptr) {
    status_->setStyleSheet(
        QStringLiteral(
            "QLabel#detachedChatStatus { color: %1; padding: 0 2px; }")
            .arg(theme.text_secondary.name()));
  }
}

void DetachedChatWindow::SetSnapshot(
    std::shared_ptr<const ProfileSnapshot> snapshot) {
  chat_view_->SetSnapshot(snapshot);
  if (!snapshot) {
    composer_->SetRunning(false);
    return;
  }
  chat_view_->SetProfileName(snapshot->label);
  if (!snapshot->requests.empty()) {
    chat_view_->SetRequest(snapshot->requests.front());
  } else {
    chat_view_->ClearRequest();
  }
  composer_->SetModelCatalog(snapshot->model_providers, snapshot->provider,
                             snapshot->model);
  composer_->SetReasoning(snapshot->reasoning_effort);
  composer_->SetUsage(snapshot->usage.context_used,
                      snapshot->usage.context_max);
  composer_->SetVoiceState(snapshot->voice_recording, snapshot->voice_partial,
                           snapshot->voice_status);
  composer_->SetRunning(SelectedTurnRunning(*snapshot));
  setWindowTitle(snapshot->label.isEmpty()
                     ? QStringLiteral("Hermes · chat")
                     : QStringLiteral("Hermes · %1").arg(snapshot->label));
  SetStatus(snapshot->connected ? QString() : snapshot->status_text);
}

void DetachedChatWindow::SetProfileName(const QString& name) {
  chat_view_->SetProfileName(name);
  setWindowTitle(name.isEmpty() ? QStringLiteral("Hermes · chat")
                                : QStringLiteral("Hermes · %1").arg(name));
}

void DetachedChatWindow::SetRunning(bool running) {
  composer_->SetRunning(running);
}

void DetachedChatWindow::SetStatus(const QString& status) {
  if (status_ == nullptr) {
    return;
  }
  status_->setText(status);
  status_->setVisible(!status.isEmpty());
}

void DetachedChatWindow::SetDraft(const QString& text) {
  composer_->SetText(text);
}

QString DetachedChatWindow::Draft() const { return composer_->Text(); }

}  // namespace hermes::ui
