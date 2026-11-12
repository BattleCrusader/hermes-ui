// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_TITLE_BAR_H_
#define HERMES_UI_TITLE_BAR_H_

#include <QWidget>

class QTimer;
#include <functional>
#include <vector>

#include "src/click_target.h"
#include "src/theme.h"

namespace hermes::ui {

// Custom 34px title bar: drawn traffic lights on macOS, window drag, and the
// two titlebar tools the spike actually needs.
class TitleBar : public QWidget {
 public:
  explicit TitleBar(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetTitle(const QString& title);
  // Match the draggable area to the resizable sidebar instead of the theme's
  // static default.
  void SetSidebarWidth(int width);
  // Brief completion cue: an accent underline that fades out.
  void Flash();
  std::vector<ClickTarget> ClickTargets() const;

  std::function<void()> on_close;
  std::function<void()> on_minimize;
  std::function<void()> on_maximize;
  std::function<void()> on_search;
  std::function<void()> on_settings;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  QRectF CloseButtonRect() const;
  QRectF MinimizeButtonRect() const;
  QRectF MaximizeButtonRect() const;
  QRectF SearchButtonRect() const;
  QRectF SettingsButtonRect() const;
  void UpdateHover(const QPointF& position);
  void BeginSystemMove();

  Theme theme_;
  QString title_ = QStringLiteral("Hermes");
  int sidebar_width_ = 0;
  int hovered_control_ = -1;
  qint64 flash_until_ms_ = 0;
  QTimer* flash_timer_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_TITLE_BAR_H_
