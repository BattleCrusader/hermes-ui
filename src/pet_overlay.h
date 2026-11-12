// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SRC_PET_OVERLAY_H_
#define HERMES_UI_SRC_PET_OVERLAY_H_

#include <QJsonObject>
#include <QPoint>
#include <QString>
#include <QWidget>
#include <functional>

#include "src/pet_sprite.h"
#include "src/theme.h"

class QTimer;

namespace hermes::ui {

// Transparent, always-on-top mascot window (the desktop's "pop out" pet).
// The gateway owns the sprite; this window only slices and animates it, and
// hands composer submissions back to the shell's normal new-chat path.
class PetOverlay : public QWidget {
  Q_OBJECT

 public:
  explicit PetOverlay(const Theme& theme, QWidget* parent = nullptr);
  ~PetOverlay() override;

  void SetTheme(const Theme& theme);
  // Async decode: the multi-megabyte sheet never blocks the UI thread.
  void SetSpritePayload(const QJsonObject& payload);
  void SetSprite(const PetSpriteInfo& sprite);
  void SetState(const QString& state);
  void SetStatus(const QString& status);
  bool HasSprite() const;
  QString Revision() const;
  QSize PreferredSize() const;

  void SetSubmitHandler(std::function<void(const QString&)> handler);
  void SetQuickEntryHandler(std::function<void()> handler);
  void SetHideHandler(std::function<void()> handler);

  // Test seams.
  int RowForTest() const;
  int FrameForTest() const;
  QString StateForTest() const;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void moveEvent(QMoveEvent* event) override;

 private:
  void RestartAnimation();
  void AdvanceFrame();
  void UpdateMask();
  QRect SpriteRect() const;
  QRect BubbleRect() const;

  Theme theme_;
  PetSpriteInfo sprite_;
  QString state_ = QStringLiteral("idle");
  QString status_;
  QString row_name_ = QStringLiteral("idle");
  int row_ = 0;
  int frame_ = 0;
  int frame_count_ = 1;
  quint64 decode_token_ = 0;
  bool dragging_ = false;
  QPoint drag_offset_;
  QTimer* timer_ = nullptr;
  std::function<void(const QString&)> submit_handler_;
  std::function<void()> quick_entry_handler_;
  std::function<void()> hide_handler_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SRC_PET_OVERLAY_H_
