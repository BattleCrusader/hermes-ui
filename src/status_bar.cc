// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/status_bar.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QPainter>
#include <QTimer>

#include "src/paint_utils.h"

namespace hermes::ui {

StatusBar::StatusBar(const Theme &theme, QWidget *parent)
    : QWidget(parent), theme_(theme) {
  setFixedHeight(26);
  pulse_timer_ = new QTimer(this);
  pulse_timer_->setInterval(160);
  connect(pulse_timer_, &QTimer::timeout, this, [this]() {
    ++pulse_;
    update();
  });
  transient_timer_ = new QTimer(this);
  transient_timer_->setSingleShot(true);
  connect(transient_timer_, &QTimer::timeout, this, [this]() {
    transient_.clear();
    update();
  });
}

void StatusBar::SetBusy(bool busy) {
  if (busy_ == busy) {
    return;
  }
  busy_ = busy;
  if (busy_) {
    pulse_timer_->start();
  } else {
    pulse_timer_->stop();
    pulse_ = 0;
  }
  update();
}

void StatusBar::ShowTransient(const QString &text, int milliseconds) {
  transient_ = text;
  transient_timer_->start(milliseconds);
  update();
}

void StatusBar::SetTheme(const Theme &theme) {
  theme_ = theme;
  update();
}

void StatusBar::SetState(const QString &left, const QString &center,
                         const QString &right, bool healthy) {
  left_ = left;
  center_ = center;
  right_ = right;
  healthy_ = healthy;
  update();
}

void StatusBar::SetUsage(const QString &usage) {
  if (usage_ == usage) {
    return;
  }
  usage_ = usage;
  update();
}

void StatusBar::paintEvent(QPaintEvent *event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.fillRect(rect(), theme_.sidebar);
  painter.fillRect(QRectF(0, 0, width(), 1), theme_.stroke);

  painter.setPen(Qt::NoPen);
  if (busy_) {
    const int phase = pulse_ % 6;
    const qreal alpha =
        0.35 + 0.65 * (phase < 3 ? phase / 3.0 : (6 - phase) / 3.0);
    QColor dot = theme_.accent;
    dot.setAlphaF(alpha);
    painter.setBrush(dot);
    painter.drawEllipse(QPointF(14, height() / 2.0), 3.4, 3.4);
  } else {
    painter.setBrush(healthy_ ? theme_.success : theme_.danger);
    painter.drawEllipse(QPointF(14, height() / 2.0), 3, 3);
  }

  painter.setFont(theme_.UiFont(10, QFont::Normal));
  painter.setPen(theme_.text_tertiary);
  DrawElidedText(painter, QRectF(24, 0, width() / 3.0 - 28, height()),
                 Qt::AlignVCenter | Qt::AlignLeft, left_, theme_.text_tertiary,
                 theme_.UiFont(10, QFont::Normal));
  DrawElidedText(painter, QRectF(0, 0, width(), height()), Qt::AlignCenter,
                 transient_.isEmpty() ? center_ : transient_,
                 transient_.isEmpty() ? theme_.text_tertiary : theme_.accent,
                 transient_.isEmpty() ? theme_.UiFont(10, QFont::Normal)
                                      : theme_.UiFont(10, QFont::DemiBold));
  const QFont small = theme_.UiFont(10, QFont::Normal);
  if (!usage_.isEmpty()) {
    // Right-aligned immediately left of the profile/gateway label, so the
    // footer reads: usage · profile - gateway.
    const qreal right_width =
        QFontMetricsF(small).horizontalAdvance(right_) + 22;
    DrawElidedText(painter,
                   QRectF(width() * 2.0 / 3.0, 0,
                          width() / 3.0 - right_width - 8, height()),
                   Qt::AlignVCenter | Qt::AlignRight, usage_,
                   theme_.text_tertiary, small);
  }
  DrawElidedText(painter,
                 QRectF(width() / 3.0, 0, width() * 2.0 / 3.0 - 14, height()),
                 Qt::AlignVCenter | Qt::AlignRight, right_,
                 theme_.text_tertiary, theme_.UiFont(10, QFont::Normal));
}

} // namespace hermes::ui
