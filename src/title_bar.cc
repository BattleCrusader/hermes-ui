// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/title_bar.h"

#include <QDateTime>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWindow>
#include <vector>

#include "src/paint_utils.h"

namespace hermes::ui {
namespace {

constexpr int kTrafficLightRadius = 6;
constexpr int kTrafficLightSpacing = 20;
constexpr int kTrafficLightFirstX = 20;
constexpr int kToolSize = 24;

}  // namespace

TitleBar::TitleBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme), sidebar_width_(theme.sidebar_width) {
  setFixedHeight(theme_.title_bar_height);
  setMouseTracking(true);
  flash_timer_ = new QTimer(this);
  flash_timer_->setInterval(60);
  connect(flash_timer_, &QTimer::timeout, this, [this]() {
    if (QDateTime::currentMSecsSinceEpoch() >= flash_until_ms_) {
      flash_timer_->stop();
    }
    update();
  });
}

void TitleBar::Flash() {
  flash_until_ms_ = QDateTime::currentMSecsSinceEpoch() + 1400;
  flash_timer_->start();
  update();
}

void TitleBar::SetTheme(const Theme& theme) {
  theme_ = theme;
  update();
}

void TitleBar::SetTitle(const QString& title) {
  if (title.isEmpty() || title_ == title) {
    return;
  }
  title_ = title;
  update();
}

void TitleBar::SetSidebarWidth(int width) {
  if (sidebar_width_ == width) {
    return;
  }
  sidebar_width_ = width;
  update();
}

std::vector<ClickTarget> TitleBar::ClickTargets() const {
  std::vector<ClickTarget> targets;
  targets.push_back({QStringLiteral("titlebar:search"), SearchButtonRect()});
  targets.push_back(
      {QStringLiteral("titlebar:settings"), SettingsButtonRect()});
  targets.push_back(
      {QStringLiteral("titlebar:close"), CloseButtonRect(), true});
  targets.push_back(
      {QStringLiteral("titlebar:minimize"), MinimizeButtonRect(), true});
  targets.push_back(
      {QStringLiteral("titlebar:maximize"), MaximizeButtonRect(), true});
  return targets;
}

QRectF TitleBar::CloseButtonRect() const {
  return QRectF(kTrafficLightFirstX - kTrafficLightRadius - 4,
                height() / 2.0 - kTrafficLightRadius - 4,
                (kTrafficLightRadius + 4) * 2, (kTrafficLightRadius + 4) * 2);
}

QRectF TitleBar::MinimizeButtonRect() const {
  return CloseButtonRect().translated(kTrafficLightSpacing, 0);
}

QRectF TitleBar::MaximizeButtonRect() const {
  return CloseButtonRect().translated(kTrafficLightSpacing * 2, 0);
}

QRectF TitleBar::SearchButtonRect() const {
  return QRectF(width() - 14 - kToolSize * 2 - 4, (height() - kToolSize) / 2.0,
                kToolSize, kToolSize);
}

QRectF TitleBar::SettingsButtonRect() const {
  return QRectF(width() - 14 - kToolSize, (height() - kToolSize) / 2.0,
                kToolSize, kToolSize);
}

void TitleBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  painter.fillRect(QRectF(0, 0, sidebar_width_, height()), theme_.sidebar);
  painter.fillRect(
      QRectF(sidebar_width_, 0, width() - sidebar_width_, height()),
      theme_.background);
  painter.fillRect(QRectF(0, height() - 1, width(), 1), theme_.stroke);
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now < flash_until_ms_) {
    const qreal remaining = (flash_until_ms_ - now) / 1400.0;
    QColor line = theme_.accent;
    line.setAlphaF(0.25 + 0.75 * remaining);
    painter.fillRect(QRectF(0, height() - 3, width(), 2), line);
  }

  const QPointF close_center(kTrafficLightFirstX, height() / 2.0);
  const QPointF minimize_center(kTrafficLightFirstX + kTrafficLightSpacing,
                                height() / 2.0);
  const QPointF maximize_center(kTrafficLightFirstX + kTrafficLightSpacing * 2,
                                height() / 2.0);

  const QColor close_color =
      hovered_control_ == 0 ? QColor("#FF5F57") : QColor("#FF6159");
  const QColor minimize_color =
      hovered_control_ == 1 ? QColor("#FEBC2E") : QColor("#FFBD2E");
  const QColor maximize_color =
      hovered_control_ == 2 ? QColor("#28C840") : QColor("#28C841");

  painter.setPen(Qt::NoPen);
  painter.setBrush(close_color);
  painter.drawEllipse(close_center, kTrafficLightRadius, kTrafficLightRadius);
  painter.setBrush(minimize_color);
  painter.drawEllipse(minimize_center, kTrafficLightRadius,
                      kTrafficLightRadius);
  painter.setBrush(maximize_color);
  painter.drawEllipse(maximize_center, kTrafficLightRadius,
                      kTrafficLightRadius);

  if (hovered_control_ == 0) {
    painter.setPen(QPen(QColor(90, 20, 20, 190), 1.2));
    painter.drawLine(close_center + QPointF(-2.5, -2.5),
                     close_center + QPointF(2.5, 2.5));
    painter.drawLine(close_center + QPointF(2.5, -2.5),
                     close_center + QPointF(-2.5, 2.5));
  } else if (hovered_control_ == 1) {
    painter.setPen(QPen(QColor(90, 60, 10, 190), 1.2));
    painter.drawLine(minimize_center + QPointF(-3, 0),
                     minimize_center + QPointF(3, 0));
  } else if (hovered_control_ == 2) {
    painter.setPen(QPen(QColor(20, 70, 30, 190), 1.2));
    painter.drawRect(
        QRectF(maximize_center.x() - 2.5, maximize_center.y() - 2.5, 5, 5));
  }

  QFont title_font = theme_.UiFont(12, QFont::DemiBold);
  DrawElidedText(painter, QRectF(0, 0, width(), height()), Qt::AlignCenter,
                 title_, theme_.text_secondary, title_font);

  const QRectF search_rect = SearchButtonRect();
  const QRectF settings_rect = SettingsButtonRect();
  if (hovered_control_ == 3) {
    FillRounded(painter, search_rect, 5, theme_.hover);
  }
  if (hovered_control_ == 4) {
    FillRounded(painter, settings_rect, 5, theme_.hover);
  }
  DrawCodicon(painter, search_rect, 0xEA6D, theme_.text_secondary, 14);
  DrawCodicon(painter, settings_rect, 0xEB51, theme_.text_secondary, 14);
}

void TitleBar::UpdateHover(const QPointF& position) {
  int next = -1;
  if (CloseButtonRect().contains(position)) {
    next = 0;
  } else if (MinimizeButtonRect().contains(position)) {
    next = 1;
  } else if (MaximizeButtonRect().contains(position)) {
    next = 2;
  } else if (SearchButtonRect().contains(position)) {
    next = 3;
  } else if (SettingsButtonRect().contains(position)) {
    next = 4;
  }
  if (next != hovered_control_) {
    hovered_control_ = next;
    setCursor(next == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
    update();
  }
}

void TitleBar::BeginSystemMove() {
  QWidget* top_level = window();
  if (top_level == nullptr) {
    return;
  }
  QWindow* handle = top_level->windowHandle();
  if (handle != nullptr) {
    handle->startSystemMove();
  }
}

void TitleBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QPointF position = event->position();
  if (CloseButtonRect().contains(position)) {
    if (on_close) {
      on_close();
    }
    return;
  }
  if (MinimizeButtonRect().contains(position)) {
    if (on_minimize) {
      on_minimize();
    }
    return;
  }
  if (MaximizeButtonRect().contains(position)) {
    if (on_maximize) {
      on_maximize();
    }
    return;
  }
  if (SearchButtonRect().contains(position)) {
    if (on_search) {
      on_search();
    }
    return;
  }
  if (SettingsButtonRect().contains(position)) {
    if (on_settings) {
      on_settings();
    }
    return;
  }
  BeginSystemMove();
}

void TitleBar::mouseMoveEvent(QMouseEvent* event) {
  UpdateHover(event->position());
  QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && on_maximize) {
    on_maximize();
  }
  QWidget::mouseDoubleClickEvent(event);
}

void TitleBar::leaveEvent(QEvent* event) {
  hovered_control_ = -1;
  setCursor(Qt::ArrowCursor);
  update();
  QWidget::leaveEvent(event);
}

}  // namespace hermes::ui
