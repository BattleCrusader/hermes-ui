// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SRC_DETACHED_CHAT_H_
#define HERMES_UI_SRC_DETACHED_CHAT_H_

#include <QWidget>
#include <memory>

#include "src/model.h"
#include "src/theme.h"

class QLabel;

namespace hermes::ui {

class ChatView;
class Composer;

// A second chat surface in its own top-level window. It shares the shell's
// Supervisor and callbacks (the detached window has no gateway connection of
// its own), so the selected session and composer state stay in sync.
class DetachedChatWindow : public QWidget {
  Q_OBJECT

public:
  explicit DetachedChatWindow(const Theme &theme, QWidget *parent = nullptr);

  void SetTheme(const Theme &theme);
  void SetSnapshot(std::shared_ptr<const ProfileSnapshot> snapshot);
  void SetProfileName(const QString &name);
  void SetRunning(bool running);
  void SetStatus(const QString &status);
  void SetDraft(const QString &text);
  QString Draft() const;

  ChatView *chat_view() const { return chat_view_; }
  Composer *composer() const { return composer_; }

private:
  Theme theme_;
  ChatView *chat_view_ = nullptr;
  Composer *composer_ = nullptr;
  QLabel *status_ = nullptr;
};

} // namespace hermes::ui

#endif // HERMES_UI_SRC_DETACHED_CHAT_H_
