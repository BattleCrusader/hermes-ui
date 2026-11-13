// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/status_bar.h"

#include <QDateTime>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <algorithm>

#include "src/paint_utils.h"

namespace hermes::ui {

QString FormatWorkElapsed(qint64 elapsed_ms) {
  const qint64 total_seconds = std::max<qint64>(0, elapsed_ms / 1000);
  const qint64 hours = total_seconds / 3600;
  const qint64 minutes = (total_seconds % 3600) / 60;
  const qint64 seconds = total_seconds % 60;
  if (hours > 0) {
    return QStringLiteral("%1h %2m %3s")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
  }
  if (minutes > 0) {
    return QStringLiteral("%1m %2s").arg(minutes).arg(seconds, 2, 10,
                                                      QLatin1Char('0'));
  }
  return QStringLiteral("%1s").arg(seconds);
}

StatusBar::StatusBar(const Theme& theme, QWidget* parent)
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
  work_timer_ = new QTimer(this);
  work_timer_->setInterval(1000);
  connect(work_timer_, &QTimer::timeout, this, [this]() { update(); });
}

void StatusBar::SetBusy(bool busy) {
  SetWorkState(busy, busy ? QDateTime::currentMSecsSinceEpoch() : 0, 0);
}

void StatusBar::SetWorkState(bool working, qint64 started_ms, int queued) {
  const int bounded = std::max(0, queued);
  if (busy_ == working && work_started_ms_ == started_ms &&
      work_queued_ == bounded) {
    return;
  }
  busy_ = working;
  working_ = working;
  work_started_ms_ =
      working
          ? (started_ms > 0 ? started_ms : QDateTime::currentMSecsSinceEpoch())
          : 0;
  work_queued_ = bounded;
  if (working) {
    pulse_timer_->start();
    work_timer_->start();
  } else {
    pulse_timer_->stop();
    work_timer_->stop();
    pulse_ = 0;
  }
  update();
}

void StatusBar::SetActivity(const QString& text) {
  if (activity_ == text) {
    return;
  }
  activity_ = text;
  update();
}

QString StatusBar::WorkText() const {
  if (!working_) {
    return QString();
  }
  QString text =
      QStringLiteral("Working · %1")
          .arg(FormatWorkElapsed(QDateTime::currentMSecsSinceEpoch() -
                                 work_started_ms_));
  if (work_queued_ > 0) {
    text += QStringLiteral(" · %1 queued").arg(work_queued_);
  }
  return text;
}

void StatusBar::ShowTransient(const QString& text, int milliseconds) {
  transient_ = text;
  transient_timer_->start(milliseconds);
  update();
}

void StatusBar::SetTheme(const Theme& theme) {
  theme_ = theme;
  update();
}

void StatusBar::SetState(const QString& left, const QString& center,
                         const QString& right, bool healthy) {
  if (left_ == left && center_ == center && right_ == right &&
      healthy_ == healthy) {
    return;
  }
  left_ = left;
  center_ = center;
  right_ = right;
  healthy_ = healthy;
  update();
}

void StatusBar::SetUsage(const QString& usage) {
  if (usage_ == usage) {
    return;
  }
  usage_ = usage;
  update();
}

QString StatusBar::CenterTextForTest() const {
  if (!transient_.isEmpty()) {
    return transient_;
  }
  if (!activity_.isEmpty()) {
    return activity_;
  }
  if (working_) {
    return WorkText();
  }
  return center_;
}

void StatusBar::paintEvent(QPaintEvent* event) {
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
  const bool highlighted =
      !transient_.isEmpty() || !activity_.isEmpty() || working_;
  const QColor centre_colour =
      highlighted ? theme_.accent : theme_.text_tertiary;
  const QFont centre_font =
      theme_.UiFont(10, highlighted ? QFont::DemiBold : QFont::Normal);
  DrawElidedText(painter, QRectF(0, 0, width(), height()), Qt::AlignCenter,
                 CenterTextForTest(), centre_colour, centre_font);
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

void StatusBar::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && working_ && work_queued_ > 0) {
    emit QueuedReviewRequested();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

}  // namespace hermes::ui
