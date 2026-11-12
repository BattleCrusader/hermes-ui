// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/pet_overlay.h"

#include <QContextMenuEvent>
#include <QFutureWatcher>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

#include "src/native_prefs.h"

namespace hermes::ui {
namespace {

struct DecodeResult {
  PetSpriteInfo sprite;
  QString error;
};

QString CanonicalStateName(const QString &state) {
  const QString s = state.toLower();
  if (s == QStringLiteral("run")) {
    return QStringLiteral("running");
  }
  if (s == QStringLiteral("wave")) {
    return QStringLiteral("waving");
  }
  if (s == QStringLiteral("jump")) {
    return QStringLiteral("jumping");
  }
  return s.isEmpty() ? QStringLiteral("idle") : s;
}

} // namespace

PetOverlay::PetOverlay(const Theme &theme, QWidget *parent)
    : QWidget(parent,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
      theme_(theme) {
  setAttribute(Qt::WA_TranslucentBackground);
  setAttribute(Qt::WA_ShowWithoutActivating);
  setFocusPolicy(Qt::NoFocus);
  setWindowTitle(QStringLiteral("Hermes pet"));
  setObjectName(QStringLiteral("petOverlay"));
  timer_ = new QTimer(this);
  timer_->setTimerType(Qt::CoarseTimer);
  connect(timer_, &QTimer::timeout, this, [this]() { AdvanceFrame(); });
  resize(PetSpriteInfo().WindowSize());
}

PetOverlay::~PetOverlay() = default;

void PetOverlay::SetTheme(const Theme &theme) {
  theme_ = theme;
  update();
}

void PetOverlay::SetSpritePayload(const QJsonObject &payload) {
  const quint64 token = ++decode_token_;
  auto *watcher = new QFutureWatcher<DecodeResult>(this);
  connect(watcher, &QFutureWatcher<DecodeResult>::finished, this,
          [this, watcher, token]() {
            const DecodeResult result = watcher->result();
            watcher->deleteLater();
            if (token != decode_token_ || !isVisible()) {
              return;
            }
            if (!result.error.isEmpty()) {
              SetStatus(result.error);
              return;
            }
            SetSprite(result.sprite);
          });
  watcher->setFuture(QtConcurrent::run([payload]() {
    DecodeResult result;
    if (!DecodePetSprite(payload, &result.sprite, &result.error)) {
      return result;
    }
    return result;
  }));
}

void PetOverlay::SetSprite(const PetSpriteInfo &sprite) {
  sprite_ = sprite;
  timer_->stop();
  frame_ = 0;
  row_name_ = sprite_.RowNameForState(state_);
  row_ = sprite_.RowForState(state_);
  frame_count_ = sprite_.FrameCountForRow(row_);
  resize(sprite_.WindowSize());
  UpdateMask();
  RestartAnimation();
  update();
}

void PetOverlay::SetState(const QString &state) {
  const QString canonical = CanonicalStateName(state);
  if (canonical == state_) {
    return;
  }
  state_ = canonical;
  const QString row_name = sprite_.RowNameForState(state_);
  const int row = sprite_.RowForState(state_);
  if (row != row_ || row_name != row_name_) {
    row_ = row;
    row_name_ = row_name;
    frame_ = 0;
    frame_count_ = sprite_.FrameCountForRow(row_);
    RestartAnimation();
    update();
  }
}

void PetOverlay::SetStatus(const QString &status) {
  if (status == status_) {
    return;
  }
  status_ = status;
  UpdateMask();
  update();
}

bool PetOverlay::HasSprite() const { return sprite_.HasSheet(); }

QString PetOverlay::Revision() const { return sprite_.revision; }

QSize PetOverlay::PreferredSize() const { return sprite_.WindowSize(); }

void PetOverlay::SetSubmitHandler(
    std::function<void(const QString &)> handler) {
  submit_handler_ = std::move(handler);
}

void PetOverlay::SetQuickEntryHandler(std::function<void()> handler) {
  quick_entry_handler_ = std::move(handler);
}

void PetOverlay::SetHideHandler(std::function<void()> handler) {
  hide_handler_ = std::move(handler);
}

int PetOverlay::RowForTest() const { return row_; }

int PetOverlay::FrameForTest() const { return frame_; }

QString PetOverlay::StateForTest() const { return state_; }

QRect PetOverlay::SpriteRect() const {
  const QSize draw = sprite_.DrawSize();
  return QRect((width() - draw.width()) / 2, height() - draw.height() - 12,
               draw.width(), draw.height());
}

QRect PetOverlay::BubbleRect() const {
  return QRect(8, 8, std::max(1, width() - 16), 34);
}

void PetOverlay::UpdateMask() {
  QRegion region(SpriteRect().adjusted(-6, -6, 6, 6));
  if (!status_.isEmpty()) {
    region = region.united(BubbleRect());
  }
  setMask(region);
}

void PetOverlay::RestartAnimation() {
  timer_->stop();
  if (!sprite_.HasSheet()) {
    return;
  }
  frame_count_ = std::max(1, sprite_.FrameCountForRow(row_));
  frame_ %= frame_count_;
  const int step_ms = std::max(40, sprite_.loop_ms / frame_count_);
  timer_->start(step_ms);
}

void PetOverlay::AdvanceFrame() {
  if (!sprite_.HasSheet()) {
    return;
  }
  frame_ = (frame_ + 1) % std::max(1, frame_count_);
  update(SpriteRect());
}

void PetOverlay::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setCompositionMode(QPainter::CompositionMode_Source);
  painter.fillRect(rect(), Qt::transparent);
  painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  if (sprite_.HasSheet()) {
    const QRect target = SpriteRect();
    const int source_x = frame_ * sprite_.frame_w;
    const int source_y = row_ * sprite_.frame_h;
    QRect source(source_x, source_y, sprite_.frame_w, sprite_.frame_h);
    if (!sprite_.sheet.rect().contains(source)) {
      source = QRect(0, 0, std::min(sprite_.frame_w, sprite_.sheet.width()),
                     std::min(sprite_.frame_h, sprite_.sheet.height()));
    }
    painter.drawImage(target, sprite_.sheet, source);
  }

  if (!status_.isEmpty()) {
    const QRect bubble = BubbleRect();
    QPainterPath path;
    path.addRoundedRect(bubble, 10, 10);
    painter.fillPath(path, Mix(theme_.surface_raised, theme_.background, 0.08));
    painter.setPen(QPen(theme_.stroke, 1));
    painter.drawPath(path);
    painter.setPen(theme_.foreground);
    painter.setFont(theme_.UiFont(11, QFont::DemiBold));
    const QString elided = painter.fontMetrics().elidedText(
        status_, Qt::ElideRight, bubble.width() - 20);
    painter.drawText(bubble, Qt::AlignCenter, elided);
  }
}

void PetOverlay::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = true;
    drag_offset_ =
        event->globalPosition().toPoint() - frameGeometry().topLeft();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void PetOverlay::mouseMoveEvent(QMouseEvent *event) {
  if (dragging_ && (event->buttons() & Qt::LeftButton)) {
    move(event->globalPosition().toPoint() - drag_offset_);
    event->accept();
    return;
  }
  QWidget::mouseMoveEvent(event);
}

void PetOverlay::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void PetOverlay::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton && quick_entry_handler_) {
    quick_entry_handler_();
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void PetOverlay::contextMenuEvent(QContextMenuEvent *event) {
  QMenu menu(this);
  QAction *quick = menu.addAction(QStringLiteral("Quick entry…"));
  QAction *hide_action = menu.addAction(QStringLiteral("Hide pet"));
  QAction *chosen = menu.exec(event->globalPos());
  if (chosen == quick && quick_entry_handler_) {
    quick_entry_handler_();
  } else if (chosen == hide_action) {
    if (hide_handler_) {
      hide_handler_();
    } else {
      hide();
    }
  }
}

void PetOverlay::moveEvent(QMoveEvent *event) {
  QWidget::moveEvent(event);
  SetNativePref(QStringLiteral("pet.overlay_x"),
                QString::number(event->pos().x()));
  SetNativePref(QStringLiteral("pet.overlay_y"),
                QString::number(event->pos().y()));
}

} // namespace hermes::ui
