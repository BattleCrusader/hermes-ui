#include "src/kanban_view.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSpinBox>
#include <QTextEdit>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

#include "src/paint_utils.h"

namespace hermes::ui {
namespace {

using kanban::KanbanTaskDetail;

constexpr int kColumnWidth = 252;
constexpr int kColumnGap = 10;
constexpr int kBoardPad = 14;
constexpr int kHeaderHeight = 32;
constexpr int kCardGap = 7;
constexpr int kCardPad = 10;
constexpr int kBulkHeight = 40;
constexpr int kCollapsedWidth = 34;

QColor StatusColor(const Theme& theme, const QString& status) {
  if (status == QStringLiteral("running") ||
      status == QStringLiteral("done")) {
    return theme.success;
  }
  if (status == QStringLiteral("ready")) {
    return theme.accent;
  }
  if (status == QStringLiteral("review") ||
      status == QStringLiteral("scheduled")) {
    return theme.warning;
  }
  if (status == QStringLiteral("blocked")) {
    return theme.danger;
  }
  return theme.text_tertiary;
}

void ClearLayout(QLayout* layout) {
  while (QLayoutItem* item = layout->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      widget->deleteLater();
    }
    if (QLayout* child = item->layout()) {
      ClearLayout(child);
    }
    delete item;
  }
}

QString Elide(const QFontMetricsF& metrics, const QString& text, double width) {
  return metrics.elidedText(text, Qt::ElideRight, static_cast<int>(width));
}

int WrappedHeight(const QString& text, double width, const QFont& font,
                  int max_lines) {
  if (text.trimmed().isEmpty()) {
    return 0;
  }
  QTextLayout layout(text, font);
  layout.beginLayout();
  const double line_height = QFontMetricsF(font).height();
  int lines = 0;
  while (lines < max_lines) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(width);
    ++lines;
  }
  layout.endLayout();
  return static_cast<int>(std::ceil(line_height * lines));
}

int DrawWrappedClamped(QPainter& painter, const QRectF& rect,
                       const QString& text, const QFont& font,
                       const QColor& color, int max_lines) {
  if (text.trimmed().isEmpty()) {
    return 0;
  }
  painter.save();
  painter.setPen(color);
  painter.setFont(font);
  QTextLayout layout(text, font);
  layout.beginLayout();
  const double line_height = QFontMetricsF(font).height();
  double y = 0;
  int lines = 0;
  while (lines < max_lines) {
    QTextLine line = layout.createLine();
    if (!line.isValid()) {
      break;
    }
    line.setLineWidth(rect.width());
    line.setPosition(QPointF(0, y));
    y += line_height;
    ++lines;
  }
  layout.endLayout();
  layout.draw(&painter, rect.topLeft());
  painter.restore();
  return static_cast<int>(std::ceil(y));
}

QString EventLine(const kanban::KanbanEvent& event) {
  QString text = event.kind;
  text.replace('_', ' ');
  if (!event.payload.trimmed().isEmpty()) {
    const QJsonDocument doc =
        QJsonDocument::fromJson(event.payload.toUtf8());
    if (doc.isObject()) {
      const QJsonObject object = doc.object();
      QString detail;
      for (auto it = object.begin(); it != object.end(); ++it) {
        if (it.value().isObject() || it.value().isArray()) {
          continue;
        }
        if (!detail.isEmpty()) {
          detail += QStringLiteral(" · ");
        }
        detail += QStringLiteral("%1=%2").arg(it.key(), it.value().toVariant().toString());
      }
      if (!detail.isEmpty()) {
        text += QStringLiteral("  %1").arg(detail);
      }
    } else {
      text += QStringLiteral("  %1").arg(event.payload);
    }
  }
  return text;
}

QString SizeLabel(qint64 bytes) {
  if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
  if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024);
  return QStringLiteral("%1 MB").arg(bytes / (1024 * 1024));
}

}  // namespace

// ---------------------------------------------------------------------------
// Board canvas
// ---------------------------------------------------------------------------

class KanbanBoardCanvas : public QWidget {
 public:
  explicit KanbanBoardCanvas(Theme theme, QWidget* parent = nullptr)
      : QWidget(parent), theme_(std::move(theme)) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumHeight(240);
  }

  void SetTheme(const Theme& theme) {
    theme_ = theme;
    update();
  }

  // `reset_view` is for a board switch; a polled refresh keeps the user's
  // selection, collapsed lanes, and per-column scroll positions.
  void SetBoard(const kanban::KanbanBoard& board,
                const kanban::KanbanBoardMeta& meta,
                bool reset_view = false) {
    board_ = board;
    meta_ = meta;
    if (reset_view) {
      selection_.clear();
      column_scroll_.clear();
      collapsed_.clear();
    } else {
      // Keep only selected cards that still exist after the refresh.
      QSet<QString> live;
      for (const kanban::KanbanTask& task : board_.tasks) {
        live.insert(task.id);
      }
      for (auto it = selection_.begin(); it != selection_.end();) {
        if (live.contains(*it)) {
          ++it;
        } else {
          it = selection_.erase(it);
        }
      }
    }
    dragging_ = false;
    drag_started_ = false;
    drag_id_.clear();
    pressed_id_.clear();
    update();
  }

  void SetProfiles(const QStringList& assignees) {
    if (assignees_ == assignees) {
      return;
    }
    assignees_ = assignees;
    update();
  }
  void SetDefaultAssignee(const QString& assignee) {
    if (default_assignee_ == assignee) {
      return;
    }
    default_assignee_ = assignee;
    update();
  }
  void SetHorizontalOffset(int value) {
    value = std::max(0, value);
    if (h_offset_ == value) {
      return;
    }
    h_offset_ = value;
    update();
  }
  int ContentWidth() const { return content_width_; }
  void SetSearch(const QString& search) {
    const QString next = search.trimmed().toLower();
    if (search_ == next) {
      return;
    }
    search_ = next;
    update();
  }
  void SetFilter(const QString& assignee, const QString& tenant,
                 bool archived) {
    if (assignee_filter_ == assignee && tenant_filter_ == tenant &&
        show_archived_ == archived) {
      return;
    }
    assignee_filter_ = assignee;
    tenant_filter_ = tenant;
    show_archived_ = archived;
    update();
  }
  QString AssigneeFilter() const { return assignee_filter_; }
  QString TenantFilter() const { return tenant_filter_; }
  bool ShowsArchived() const { return show_archived_; }

  const QSet<QString>& Selection() const { return selection_; }
  void ClearSelection() {
    selection_.clear();
    update();
  }
  void SelectOnly(const QString& id) {
    selection_.clear();
    if (!id.isEmpty()) {
      selection_.insert(id);
    }
    update();
  }
  QVector<QRectF> TaskRects() const { return task_rects_; }
  QColor BackgroundForTest() const { return theme_.background; }
  int VisibleTaskCount() const {
    int count = 0;
    for (const auto& column : columns_) {
      count += static_cast<int>(column.cards.size());
    }
    return count;
  }
  void LayoutForTest() { Layout(); }

  std::function<void(const QString&)> on_open;
  std::function<void(const QString&)> on_add;
  std::function<void(const QStringList&, const QString&)> on_move;
  std::function<void(const QString&)> on_delete;
  std::function<void(const QStringList&, const QString&)> on_bulk_assign;
  std::function<void(const QStringList&, int)> on_bulk_priority;
  std::function<void(const QStringList&)> on_bulk_archive;
  std::function<void(const QStringList&)> on_bulk_delete;
  std::function<void(int, int)> on_content_width;
  std::function<void(int)> on_horizontal_delta;

 protected:
  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    Layout();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), theme_.background);

    if (board_.columns.isEmpty()) {
      painter.setPen(theme_.text_tertiary);
      painter.setFont(theme_.UiFont(12, QFont::Normal));
      painter.drawText(rect(), Qt::AlignCenter,
                       QStringLiteral("Loading board…"));
      return;
    }

    for (const ColumnVisual& column : columns_) {
      PaintColumn(painter, column);
    }
    PaintDrag(painter);
    PaintBulkBar(painter);
  }

  void mousePressEvent(QMouseEvent* event) override {
    Layout();
    const QPointF pos = event->position();
    if (!selection_.isEmpty() && bulk_bar_.contains(pos)) {
      HandleBulkPress(pos);
      return;
    }
    for (const ColumnVisual& column : columns_) {
      if (column.add_rect.contains(pos)) {
        if (on_add) {
          on_add(column.slug);
        }
        return;
      }
      if (column.header.contains(pos)) {
        if (collapsed_.contains(column.slug)) {
          collapsed_.remove(column.slug);
        } else {
          collapsed_.insert(column.slug);
        }
        update();
        return;
      }
      for (const CardVisual& card : column.cards) {
        if (card.rect.contains(pos)) {
          if (!event->modifiers().testFlag(Qt::ControlModifier) &&
              !event->modifiers().testFlag(Qt::MetaModifier) &&
              !selection_.contains(card.id)) {
            selection_.clear();
          }
          pressed_id_ = card.id;
          pressed_pos_ = pos;
          drag_started_ = false;
          drag_id_.clear();
          update();
          return;
        }
      }
    }
    if (!selection_.isEmpty()) {
      selection_.clear();
      update();
    }
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    const QPointF pos = event->position();
    if (!pressed_id_.isEmpty() &&
        (pos - pressed_pos_).manhattanLength() > 6) {
      drag_started_ = true;
      drag_id_ = pressed_id_;
      drag_pos_ = pos;
      hover_drop_ = DropTarget(pos);
      setCursor(Qt::ClosedHandCursor);
      update();
      return;
    }
    if (drag_started_) {
      drag_pos_ = pos;
      hover_drop_ = DropTarget(pos);
      setCursor(Qt::ClosedHandCursor);
      update();
      return;
    }
    for (const ColumnVisual& column : columns_) {
      if (column.add_rect.contains(pos)) {
        setCursor(Qt::PointingHandCursor);
        return;
      }
    }
    for (const ColumnVisual& column : columns_) {
      for (const CardVisual& card : column.cards) {
        if (card.rect.contains(pos)) {
          setCursor(Qt::PointingHandCursor);
          return;
        }
      }
    }
    setCursor(Qt::ArrowCursor);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    Q_UNUSED(event);
    if (drag_started_ && !drag_id_.isEmpty()) {
      const QString target = DropTarget(event->position());
      const kanban::KanbanTask* dragged = TaskById(drag_id_);
      if (!target.isEmpty() && dragged != nullptr &&
          kanban::CanMove(dragged->status, target)) {
        QStringList ids;
        if (selection_.contains(drag_id_) && selection_.size() > 1) {
          ids = selection_.values();
        } else {
          ids.push_back(drag_id_);
        }
        if (on_move) {
          on_move(ids, target);
        }
      }
      dragging_ = false;
      drag_started_ = false;
      drag_id_.clear();
      pressed_id_.clear();
      hover_drop_.clear();
      setCursor(Qt::ArrowCursor);
      update();
      return;
    }
    if (!pressed_id_.isEmpty()) {
      const QString id = pressed_id_;
      pressed_id_.clear();
      if (!id.isEmpty()) {
        if (event->modifiers().testFlag(Qt::ControlModifier) ||
            event->modifiers().testFlag(Qt::MetaModifier)) {
          if (selection_.contains(id)) {
            selection_.remove(id);
          } else {
            selection_.insert(id);
          }
          update();
        } else if (on_open) {
          on_open(id);
        }
      }
    }
  }

  void wheelEvent(QWheelEvent* event) override {
    Layout();
    const QPointF pos = event->position();
    const QPoint delta = event->angleDelta();
    if (event->modifiers().testFlag(Qt::ShiftModifier) ||
        std::abs(delta.x()) > std::abs(delta.y())) {
      const int step = delta.x() != 0 ? delta.x() : delta.y();
      if (on_horizontal_delta) {
        on_horizontal_delta(-step);
      }
      return;
    }
    for (const ColumnVisual& column : columns_) {
      if (!column.rect.contains(pos) || collapsed_.contains(column.slug)) {
        continue;
      }
      int& scroll = column_scroll_[column.slug];
      scroll -= delta.y() / 2;
      scroll = std::max(0, scroll);
      update();
      return;
    }
  }

  void contextMenuEvent(QContextMenuEvent* event) override {
    Layout();
    for (const ColumnVisual& column : columns_) {
      for (const CardVisual& card : column.cards) {
        if (!card.rect.contains(event->pos())) {
          continue;
        }
        const kanban::KanbanTask* task = TaskById(card.id);
        if (task == nullptr) {
          return;
        }
        QMenu menu(this);
        menu.addAction(QStringLiteral("Open"), this, [this, id = card.id]() {
          if (on_open) on_open(id);
        });
        if (!selection_.contains(card.id)) {
          menu.addAction(QStringLiteral("Select"), this, [this, id = card.id]() {
            selection_.insert(id);
            update();
          });
        } else {
          menu.addAction(QStringLiteral("Deselect"), this, [this, id = card.id]() {
            selection_.remove(id);
            update();
          });
        }
        QMenu* move = menu.addMenu(QStringLiteral("Move to"));
        for (const QString& target : board_.columns) {
          if (!kanban::CanMove(task->status, target)) {
            continue;
          }
          move->addAction(kanban::ColumnLabel(target), this,
                          [this, ids = QStringList{card.id}, target]() {
                            if (on_move) on_move(ids, target);
                          });
        }
        if (move->isEmpty()) {
          move->setEnabled(false);
        }
        QMenu* priority = menu.addMenu(QStringLiteral("Priority"));
        for (int value = 0; value <= 4; ++value) {
          priority->addAction(kanban::PriorityLabel(value), this,
                              [this, ids = QStringList{card.id}, value]() {
                                if (on_bulk_priority) on_bulk_priority(ids, value);
                              });
        }
        QMenu* assignee = menu.addMenu(QStringLiteral("Assignee"));
        assignee->addAction(QStringLiteral("Unassigned"), this,
                            [this, ids = QStringList{card.id}]() {
                              if (on_bulk_assign) on_bulk_assign(ids, QString());
                            });
        for (const QString& name : assignees_) {
          assignee->addAction(name, this,
                              [this, ids = QStringList{card.id}, name]() {
                                if (on_bulk_assign) on_bulk_assign(ids, name);
                              });
        }
        menu.addSeparator();
        menu.addAction(QStringLiteral("Copy ID"), this,
                       [id = card.id]() {
                         QApplication::clipboard()->setText(id);
                       });
        menu.addAction(QStringLiteral("Archive"), this,
                       [this, ids = QStringList{card.id}]() {
                         if (on_bulk_archive) on_bulk_archive(ids);
                       });
        menu.addAction(QStringLiteral("Delete…"), this,
                       [this, id = card.id]() {
                         if (on_delete) on_delete(id);
                       });
        menu.exec(event->globalPos());
        return;
      }
    }
  }

  void keyPressEvent(QKeyEvent* event) override {
    if (event->key() == Qt::Key_Escape && !selection_.isEmpty()) {
      selection_.clear();
      update();
      return;
    }
    QWidget::keyPressEvent(event);
  }

 private:
  struct CardVisual {
    QString id;
    QRectF rect;
  };
  struct ColumnVisual {
    QString slug;
    QRectF rect;
    QRectF header;
    QRectF body;
    QRectF add_rect;
    QVector<CardVisual> cards;
  };

  const kanban::KanbanTask* TaskById(const QString& id) const {
    for (const kanban::KanbanTask& task : board_.tasks) {
      if (task.id == id) {
        return &task;
      }
    }
    return nullptr;
  }

  bool Matches(const kanban::KanbanTask& task) const {
    if (!show_archived_ && task.archived) {
      return false;
    }
    if (!assignee_filter_.isEmpty() &&
        task.assignee != assignee_filter_) {
      return false;
    }
    if (!tenant_filter_.isEmpty() && task.tenant != tenant_filter_) {
      return false;
    }
    if (search_.isEmpty()) {
      return true;
    }
    return task.title.toLower().contains(search_) ||
           task.body.toLower().contains(search_) ||
           task.id.toLower().contains(search_) ||
           task.assignee.toLower().contains(search_);
  }

  int CardHeight(const kanban::KanbanTask& task, double width) const {
    const QFont title_font = theme_.UiFont(12, QFont::DemiBold);
    const QFont body_font = theme_.UiFont(10, QFont::Normal);
    const double inner = width - kCardPad * 2;
    int height = kCardPad + 6;
    height += WrappedHeight(task.title, inner, title_font, 2);
    height += 3;
    if (!task.latest_summary.isEmpty()) {
      height += WrappedHeight(task.latest_summary, inner, body_font, 2);
    }
    if (task.block_kind == QStringLiteral("error") ||
        task.warning_count > 0) {
      height += 4;
    }
    height += 22;  // footer
    height += kCardPad;
    return std::max(74, height);
  }

  void Layout() {
    columns_.clear();
    task_rects_.clear();
    int x = kBoardPad - h_offset_;
    const int available = std::max(320, height() - 4);
    for (const QString& slug : board_.columns) {
      ColumnVisual column;
      column.slug = slug;
      const bool collapsed = collapsed_.contains(slug);
      const int width = collapsed ? kCollapsedWidth : kColumnWidth;
      column.rect = QRectF(x, 0, width, available);
      column.header = QRectF(x, 6, width, collapsed ? available - 12 : kHeaderHeight);
      if (collapsed) {
        column.body = column.header;
      } else {
        column.body = QRectF(x, kHeaderHeight + 10, width,
                             std::max(0, available - kHeaderHeight - 16));
        column.add_rect = QRectF(x + width - 26, 8, 20, 20);
      }
      int y = static_cast<int>(column.body.top()) -
              column_scroll_.value(slug, 0);
      const int inner_width = width - 8;
      for (const kanban::KanbanTask& task : board_.tasks) {
        if (task.status != slug || !Matches(task)) {
          continue;
        }
        if (!collapsed) {
          const int card_height = CardHeight(task, inner_width);
          CardVisual card;
          card.id = task.id;
          card.rect = QRectF(x + 4, y, inner_width, card_height);
          column.cards.push_back(card);
          task_rects_.push_back(card.rect);
        }
        y += collapsed ? 0 : CardHeight(task, inner_width) + kCardGap;
      }
      const int content_height =
          y + column_scroll_.value(slug, 0) -
          static_cast<int>(column.body.top());
      const int max_scroll =
          std::max(0, static_cast<int>(content_height - column.body.height()));
      int& stored = column_scroll_[slug];
      stored = std::clamp(stored, 0, max_scroll);
      columns_.push_back(column);
      x += width + kColumnGap;
    }
    content_width_ = std::max(0, x - kColumnGap + kBoardPad - 1);
    if (on_content_width) {
      on_content_width(content_width_, width());
    }
  }

  void PaintColumn(QPainter& painter, const ColumnVisual& column) {
    const bool collapsed = collapsed_.contains(column.slug);
    const QColor status_color = StatusColor(theme_, column.slug);
    if (!collapsed) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme_.surface);
      painter.drawRoundedRect(column.rect.adjusted(0, 4, 0, -2), 10, 10);
    }

    if (collapsed) {
      painter.save();
      painter.setBrush(theme_.surface);
      painter.setPen(QPen(theme_.stroke, 1));
      painter.drawRoundedRect(column.rect.adjusted(0, 4, 0, -2), 8, 8);
      painter.translate(column.rect.center().x(), column.rect.center().y());
      painter.rotate(-90);
      painter.setPen(theme_.text_secondary);
      painter.setFont(theme_.UiFont(10, QFont::DemiBold));
      const QString label = QStringLiteral("%1  %2")
                                .arg(kanban::ColumnLabel(column.slug))
                                .arg(column.cards.size());
      painter.drawText(QRectF(-column.rect.height() / 2, -8,
                              column.rect.height(), 16),
                       Qt::AlignCenter, label);
      painter.restore();
      return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(status_color);
    painter.drawEllipse(QPointF(column.header.left() + 12,
                               column.header.top() + 16),
                        4, 4);
    painter.setPen(theme_.text_secondary);
    painter.setFont(theme_.UiFont(10, QFont::DemiBold));
    painter.drawText(QRectF(column.header.left() + 22, column.header.top(),
                            column.header.width() - 54, kHeaderHeight),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     kanban::ColumnLabel(column.slug).toUpper());
    painter.setPen(theme_.text_tertiary);
    painter.drawText(QRectF(column.header.right() - 62, column.header.top(),
                            36, kHeaderHeight),
                     Qt::AlignVCenter | Qt::AlignRight,
                     QString::number(column.cards.size()));
    painter.setPen(theme_.text_tertiary);
    painter.setFont(theme_.UiFont(14, QFont::Normal));
    painter.drawText(column.add_rect, Qt::AlignCenter, QStringLiteral("+"));

    if (column.cards.isEmpty()) {
      painter.setPen(theme_.text_tertiary);
      painter.setFont(theme_.UiFont(10, QFont::Normal));
      painter.drawText(column.body.adjusted(4, 12, -4, 0),
                       Qt::AlignTop | Qt::AlignHCenter,
                       hover_drop_ == column.slug
                           ? QStringLiteral("Drop here")
                           : QStringLiteral("No cards"));
      return;
    }

    painter.save();
    painter.setClipRect(column.body.adjusted(-1, -4, 1, 2));
    for (const CardVisual& card : column.cards) {
      const kanban::KanbanTask* task = TaskById(card.id);
      if (task != nullptr) {
        PaintCard(painter, card, *task, column.slug);
      }
    }
    painter.restore();
  }

  void PaintCard(QPainter& painter, const CardVisual& card,
                 const kanban::KanbanTask& task, const QString& column) {
  Q_UNUSED(column);
    const bool selected = selection_.contains(task.id);
    const bool dragging = drag_started_ && drag_id_ == task.id;
    if (dragging) {
      painter.setOpacity(0.45);
    }
    painter.setPen(QPen(selected ? theme_.accent : theme_.stroke,
                        selected ? 1.6 : 1.0));
    painter.setBrush(selected ? theme_.selected : theme_.surface_raised);
    painter.drawRoundedRect(card.rect, 8, 8);

    const QColor accent = StatusColor(theme_, task.status);
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawRoundedRect(
        QRectF(card.rect.left(), card.rect.top() + 7, 3,
               card.rect.height() - 14),
        1.5, 1.5);

    const double inner = card.rect.width() - kCardPad * 2;
    double y = card.rect.top() + kCardPad;

    // Priority + warning badges sit above the title, like the desktop card.
    double badge_x = card.rect.left() + kCardPad;
    const double badge_y = y;
    if (task.priority > 0) {
      const QString label = kanban::PriorityLabel(task.priority);
      painter.setFont(theme_.UiFont(9, QFont::DemiBold));
      const double width = QFontMetricsF(theme_.UiFont(9, QFont::DemiBold))
                               .horizontalAdvance(label) +
                           12;
      const QRectF badge(badge_x, badge_y, width, 16);
      painter.setPen(Qt::NoPen);
      painter.setBrush(Mix(theme_.surface_raised, accent, 0.18));
      painter.drawRoundedRect(badge, 4, 4);
      painter.setPen(accent);
      painter.drawText(badge, Qt::AlignCenter, label);
      badge_x += width + 5;
    }
    if (task.warning_count > 0) {
      const QString label = QStringLiteral("!%1").arg(task.warning_count);
      painter.setFont(theme_.UiFont(9, QFont::DemiBold));
      const QRectF badge(badge_x, badge_y, 24, 16);
      painter.setPen(Qt::NoPen);
      painter.setBrush(Mix(theme_.surface_raised, theme_.danger, 0.18));
      painter.drawRoundedRect(badge, 4, 4);
      painter.setPen(theme_.danger);
      painter.drawText(badge, Qt::AlignCenter, label);
      badge_x += 29;
    }
    if (task.tenant == QLatin1String("personal")) {
      // personal is the default tenant: showing it on every card is noise.
    } else if (!task.tenant.isEmpty()) {
      painter.setFont(theme_.UiFont(9, QFont::Normal));
      const QString label = Elide(QFontMetricsF(painter.font()), task.tenant,
                                  inner - (badge_x - card.rect.left() - kCardPad));
      painter.setPen(theme_.text_tertiary);
      painter.drawText(QRectF(badge_x, badge_y, 80, 16),
                       Qt::AlignVCenter | Qt::AlignLeft, label);
    }
    if (task.priority > 0 || task.warning_count > 0 || !task.tenant.isEmpty()) {
      y += 19;
      painter.setPen(theme_.stroke);
      painter.drawLine(QPointF(card.rect.left() + kCardPad, y - 4),
                       QPointF(card.rect.right() - kCardPad, y - 4));
      y += 2;
    }

    y += DrawWrappedClamped(
             painter,
             QRectF(card.rect.left() + kCardPad, y, inner, 40), task.title,
             theme_.UiFont(12, QFont::DemiBold), theme_.foreground, 2) +
         3;
    if (!task.latest_summary.isEmpty()) {
      y += DrawWrappedClamped(
               painter,
               QRectF(card.rect.left() + kCardPad, y, inner, 34),
               task.latest_summary,
               theme_.UiFont(10, QFont::Normal), theme_.text_secondary, 2) +
           2;
    }

    // Footer: avatar + assignee on the left, run/id/metrics on the right.
    const double footer_top = card.rect.bottom() - kCardPad - 15;
    const double avatar = 16;
    painter.setPen(Qt::NoPen);
    painter.setBrush(task.assignee.isEmpty()
                         ? theme_.surface
                         : Mix(theme_.accent, theme_.surface_raised, 0.55));
    painter.drawEllipse(QRectF(card.rect.left() + kCardPad, footer_top + 1,
                               avatar, avatar));
    painter.setPen(task.assignee.isEmpty() ? theme_.text_tertiary
                                          : theme_.accent_text);
    painter.setFont(theme_.UiFont(8, QFont::DemiBold));
    painter.drawText(QRectF(card.rect.left() + kCardPad, footer_top + 1,
                            avatar, avatar),
                     Qt::AlignCenter,
                     task.assignee.isEmpty()
                         ? QStringLiteral("?")
                         : kanban::Initials(task.assignee));

    const QFont footer_font = theme_.UiFont(9, QFont::Normal);
    painter.setFont(footer_font);
    const QFontMetricsF footer_metrics(footer_font);
    const double footer_left = card.rect.left() + kCardPad + avatar + 5;
    painter.setPen(theme_.text_secondary);
    painter.drawText(QRectF(footer_left, footer_top,
                            card.rect.right() - kCardPad - footer_left - 52,
                            16),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     Elide(footer_metrics,
                           task.assignee.isEmpty() ? QStringLiteral("unassigned")
                                                   : task.assignee,
                           card.rect.right() - kCardPad - footer_left - 52));

    QString right;
    if (task.status == QStringLiteral("running") &&
        task.started_age_seconds >= 0) {
      right = kanban::RelativeLabel(task.started_age_seconds);
    } else if (task.progress_done >= 0) {
      right = QStringLiteral("%1/%2").arg(task.progress_done).arg(task.progress_total);
    }
    if (task.comment_count > 0) {
      right += QStringLiteral("  C%1").arg(task.comment_count);
    }
    if (task.link_count > 0) {
      right += QStringLiteral("  L%1").arg(task.link_count);
    }
    if (right.isEmpty()) {
      right = kanban::ShortId(task.id);
    }
    painter.setPen(task.status == QStringLiteral("running") ? theme_.success
                                                            : theme_.text_tertiary);
    painter.drawText(QRectF(card.rect.left(), footer_top,
                            card.rect.width() - kCardPad, 16),
                     Qt::AlignVCenter | Qt::AlignRight, right);
    painter.setOpacity(1.0);
  }

  void PaintDrag(QPainter& painter) {
    if (!drag_started_ || drag_id_.isEmpty()) {
      return;
    }
    const kanban::KanbanTask* task = TaskById(drag_id_);
    if (task == nullptr) {
      return;
    }
    const QRectF ghost(drag_pos_.x() + 12, drag_pos_.y() - 18, 210, 36);
    painter.setPen(QPen(theme_.accent, 1.2));
    painter.setBrush(theme_.surface_raised);
    painter.drawRoundedRect(ghost, 7, 7);
    painter.setPen(theme_.foreground);
    painter.setFont(theme_.UiFont(10, QFont::DemiBold));
    painter.drawText(ghost.adjusted(9, 0, -9, 0),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     Elide(QFontMetricsF(painter.font()), task->title,
                           ghost.width() - 18));
    if (!hover_drop_.isEmpty()) {
      const bool valid =
          kanban::CanMove(task->status, hover_drop_);
      const QRectF target = ColumnRect(hover_drop_);
      if (!target.isEmpty()) {
        painter.setPen(QPen(valid ? theme_.accent : theme_.danger, 1.5,
                            Qt::DashLine));
        painter.setBrush(QColor(valid ? theme_.accent_soft : theme_.danger));
        painter.setOpacity(0.12);
        painter.drawRoundedRect(target, 9, 9);
        painter.setOpacity(1.0);
      }
    }
  }

  void PaintBulkBar(QPainter& painter) {
    bulk_bar_ = QRectF();
    bulk_rects_.clear();
    if (selection_.isEmpty() || drag_started_) {
      return;
    }
    const QFont font = theme_.UiFont(10, QFont::DemiBold);
    const QFontMetricsF metrics(font);
    struct Item {
      QString key;
      QString label;
      int width;
    };
    QVector<Item> items{{QStringLiteral("count"),
                         QStringLiteral("%1 selected").arg(selection_.size()),
                         0},
                        {QStringLiteral("move"), QStringLiteral("Move"), 54},
                        {QStringLiteral("assign"), QStringLiteral("Assign"), 58},
                        {QStringLiteral("priority"), QStringLiteral("Priority"), 60},
                        {QStringLiteral("archive"), QStringLiteral("Archive"), 60},
                        {QStringLiteral("delete"), QStringLiteral("Delete"), 54},
                        {QStringLiteral("clear"), QStringLiteral("×"), 28}};
    int total = 24;
    for (Item& item : items) {
      if (item.width == 0) {
        item.width = static_cast<int>(metrics.horizontalAdvance(item.label)) + 24;
      }
      total += item.width + 6;
    }
    total = std::min(total, width() - 24);
    const double left = (width() - total) / 2.0;
    bulk_bar_ = QRectF(left, height() - kBulkHeight - 12, total, kBulkHeight);
    painter.setPen(QPen(theme_.stroke_strong, 1));
    painter.setBrush(theme_.surface_raised);
    painter.drawRoundedRect(bulk_bar_, 12, 12);
    double x = bulk_bar_.left() + 12;
    painter.setFont(font);
    for (const Item& item : items) {
      const QRectF rect(x, bulk_bar_.top() + 6, item.width, kBulkHeight - 12);
      if (item.key == QStringLiteral("count")) {
        painter.setPen(theme_.text_secondary);
        painter.drawText(rect, Qt::AlignVCenter | Qt::AlignLeft, item.label);
      } else {
        painter.setPen(item.key == QStringLiteral("delete") ? theme_.danger
                                                            : theme_.foreground);
        painter.drawText(rect, Qt::AlignCenter, item.label);
      }
      bulk_rects_.insert(item.key, rect);
      x += item.width + 6;
    }
  }

  void HandleBulkPress(const QPointF& pos) {
    if (bulk_rects_.value(QStringLiteral("clear")).contains(pos)) {
      selection_.clear();
      update();
      return;
    }
    QStringList ids = selection_.values();
    std::sort(ids.begin(), ids.end());
    if (bulk_rects_.value(QStringLiteral("move")).contains(pos)) {
      QMenu menu(this);
      for (const QString& column : board_.columns) {
        if (kanban::LockedTarget(column)) {
          continue;
        }
        menu.addAction(kanban::ColumnLabel(column), this,
                       [this, ids, column]() {
                         if (on_move) on_move(ids, column);
                       });
      }
      menu.exec(QCursor::pos());
    } else if (bulk_rects_.value(QStringLiteral("assign")).contains(pos)) {
      QMenu menu(this);
      menu.addAction(QStringLiteral("Unassigned"), this, [this, ids]() {
        if (on_bulk_assign) on_bulk_assign(ids, QString());
      });
      for (const QString& name : assignees_) {
        menu.addAction(name, this, [this, ids, name]() {
          if (on_bulk_assign) on_bulk_assign(ids, name);
        });
      }
      menu.exec(QCursor::pos());
    } else if (bulk_rects_.value(QStringLiteral("priority")).contains(pos)) {
      QMenu menu(this);
      for (int value = 0; value <= 4; ++value) {
        menu.addAction(kanban::PriorityLabel(value), this,
                       [this, ids, value]() {
                         if (on_bulk_priority) on_bulk_priority(ids, value);
                       });
      }
      menu.exec(QCursor::pos());
    } else if (bulk_rects_.value(QStringLiteral("archive")).contains(pos)) {
      if (on_bulk_archive) on_bulk_archive(ids);
    } else if (bulk_rects_.value(QStringLiteral("delete")).contains(pos)) {
      if (on_bulk_delete) on_bulk_delete(ids);
    }
  }

  QString DropTarget(const QPointF& pos) const {
    for (const ColumnVisual& column : columns_) {
      if (column.rect.contains(pos)) {
        return column.slug;
      }
    }
    return QString();
  }

  QRectF ColumnRect(const QString& slug) const {
    for (const ColumnVisual& column : columns_) {
      if (column.slug == slug) {
        return column.rect;
      }
    }
    return QRectF();
  }

  Theme theme_;
  kanban::KanbanBoard board_;
  kanban::KanbanBoardMeta meta_;
  QStringList assignees_;
  QString default_assignee_;
  QString search_;
  QString assignee_filter_;
  QString tenant_filter_;
  bool show_archived_ = false;
  QSet<QString> selection_;
  QHash<QString, int> column_scroll_;
  QSet<QString> collapsed_;
  mutable QVector<ColumnVisual> columns_;
  mutable QVector<QRectF> task_rects_;
  int content_width_ = 0;
  int h_offset_ = 0;
  QRectF bulk_bar_;
  QHash<QString, QRectF> bulk_rects_;
  QString pressed_id_;
  QPointF pressed_pos_;
  QString drag_id_;
  QPointF drag_pos_;
  bool drag_started_ = false;
  bool dragging_ = false;
  QString hover_drop_;
};


// ---------------------------------------------------------------------------
// Task dialog (the desktop drawer)
// ---------------------------------------------------------------------------

class KanbanTaskDialog : public QDialog {
 public:
  KanbanTaskDialog(KanbanView* view, Theme theme)
      : QDialog(view), view_(view), theme_(std::move(theme)) {
    setObjectName(QStringLiteral("kanbanTaskDialog"));
    setWindowTitle(QStringLiteral("Task"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(980, 740);
    BuildUi();
    ApplyStyle();
  }

  void SetTheme(const Theme& theme) {
    theme_ = theme;
    ApplyStyle();
  }

  void SetStatus(const QString& text) { status_->setText(text); }

  void SetDetail(const kanban::KanbanTaskDetail& detail,
                 const QVector<kanban::KanbanTask>& all_tasks,
                 const QStringList& columns, const QStringList& assignees,
                 const QString& default_assignee) {
    const bool new_task =
        detail.task.id != current_id_ || current_id_.isEmpty();
    current_id_ = detail.task.id;
    detail_ = detail;
    all_tasks_ = all_tasks;
    column_list_ = columns;
    assignees_ = assignees;
    default_assignee_ = default_assignee;
    setWindowTitle(QStringLiteral("%1 · %2")
                       .arg(kanban::ShortId(detail.task.id),
                            detail.task.title.left(48)));
    title_->setText(detail.task.title);
    status_chip_->setText(kanban::ColumnLabel(detail.task.status));
    status_chip_->setStyleSheet(
        QStringLiteral("QLabel { color: %1; border: 1px solid %2; "
                       "border-radius: 8px; padding: 2px 8px; font-size: 10px; "
                       "font-weight: 600; }")
            .arg(StatusColor(theme_, detail.task.status).name(),
                 theme_.stroke.name()));

    applying_ = true;
    if (new_task || !body_dirty_) {
      body_->setPlainText(detail.task.body);
      body_dirty_ = false;
    }
    status_->clear();
    status_combo_->clear();
    for (const QString& column : column_list_) {
      if (kanban::LockedTarget(column) &&
          column != detail.task.status) {
        continue;
      }
      status_combo_->addItem(kanban::ColumnLabel(column), column);
    }
    const int status_index = status_combo_->findData(detail.task.status);
    if (status_index >= 0) {
      status_combo_->setCurrentIndex(status_index);
    }
    assignee_combo_->clear();
    assignee_combo_->addItem(QStringLiteral("Unassigned"), QString());
    for (const QString& name : assignees_) {
      assignee_combo_->addItem(name, name);
    }
    const int assignee_index =
        assignee_combo_->findData(detail.task.assignee);
    if (assignee_index >= 0) {
      assignee_combo_->setCurrentIndex(assignee_index);
    } else if (!detail.task.assignee.isEmpty()) {
      assignee_combo_->addItem(detail.task.assignee, detail.task.assignee);
      assignee_combo_->setCurrentIndex(assignee_combo_->count() - 1);
    }
    priority_->setValue(detail.task.priority);
    applying_ = false;

    created_value_->setText(detail.task.created_at.isEmpty()
                                ? QStringLiteral("—")
                                : detail.task.created_at);
    model_value_->setText(
        detail.task.model_override.isEmpty()
            ? QStringLiteral("inherits profile")
            : QStringLiteral("%1%2").arg(
                  detail.task.provider_override.isEmpty()
                      ? QString()
                      : detail.task.provider_override + QStringLiteral("/"),
                  detail.task.model_override));
    workspace_value_->setText(
        detail.task.workspace_path.isEmpty()
            ? (detail.task.workspace_kind.isEmpty()
                   ? QStringLiteral("scratch")
                   : detail.task.workspace_kind)
            : QStringLiteral("%1\n%2")
                  .arg(detail.task.workspace_kind.isEmpty()
                           ? QStringLiteral("dir")
                           : detail.task.workspace_kind,
                       detail.task.workspace_path));
    workspace_value_->setToolTip(detail.task.workspace_path);

    result_value_->setText(detail.task.result);
    result_section_->setVisible(!detail.task.result.trimmed().isEmpty());
    summary_value_->setText(detail.task.latest_summary);
    summary_section_->setVisible(
        !detail.task.latest_summary.trimmed().isEmpty());

    unblock_->setVisible(detail.task.status == QStringLiteral("blocked"));
    reclaim_->setVisible(detail.task.status == QStringLiteral("running") ||
                         detail.task.status == QStringLiteral("review"));

    RebuildDiagnostics();
    RebuildLinks();
    RebuildComments();
    RebuildEvents();
    RebuildRuns();
    RebuildAttachments();
    if (log_task_ != detail.task.id) {
      log_task_ = detail.task.id;
      log_view_->clear();
      log_view_->hide();
      log_button_->setText(QStringLiteral("Load worker log"));
    }
    body_->setEnabled(true);
    comment_->setEnabled(true);
  }

  void SetDetailError(const QString& error) {
    status_->setText(error);
  }

 private:
  void BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 14);
    outer->setSpacing(10);

    auto* header = new QWidget(this);
    auto* header_row = new QHBoxLayout(header);
    header_row->setContentsMargins(0, 0, 0, 0);
    header_row->setSpacing(10);
    auto* titles = new QVBoxLayout();
    titles->setSpacing(3);
    title_ = new QLabel(header);
    title_->setObjectName(QStringLiteral("kanbanTaskTitle"));
    title_->setWordWrap(true);
    title_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    titles->addWidget(title_);
    status_chip_ = new QLabel(header);
    status_chip_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    titles->addWidget(status_chip_, 0, Qt::AlignLeft);
    header_row->addLayout(titles, 1);
    status_ = new QLabel(header);
    status_->setObjectName(QStringLiteral("kanbanTaskStatus"));
    header_row->addWidget(status_);
    auto* close = new QPushButton(QStringLiteral("Close"), header);
    close->setObjectName(QStringLiteral("kanbanSecondary"));
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    header_row->addWidget(close);
    outer->addWidget(header);

    auto* body_row = new QHBoxLayout();
    body_row->setSpacing(16);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* left = new QWidget(scroll);
    auto* left_layout = new QVBoxLayout(left);
    left_layout->setContentsMargins(0, 0, 6, 0);
    left_layout->setSpacing(8);

    auto* description_label = SectionLabel(QStringLiteral("DESCRIPTION"), left);
    left_layout->addWidget(description_label);
    body_ = new QTextEdit(left);
    body_->setObjectName(QStringLiteral("kanbanBody"));
    body_->setPlaceholderText(QStringLiteral("Describe the work…"));
    body_->setMinimumHeight(120);
    body_->setMaximumHeight(190);
    connect(body_, &QTextEdit::textChanged, this, [this]() {
      if (!applying_) {
        body_dirty_ = true;
      }
    });
    left_layout->addWidget(body_);
    auto* save_row = new QHBoxLayout();
    save_row->addStretch(1);
    save_body_ = new QPushButton(QStringLiteral("Save description"), left);
    save_body_->setObjectName(QStringLiteral("kanbanPrimary"));
    connect(save_body_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      view_->PatchTask(current_id_,
                       QJsonObject{{QStringLiteral("body"), body_->toPlainText()}},
                       QStringLiteral("Description saved"));
      body_dirty_ = false;
      status_->setText(QStringLiteral("Saving description…"));
    });
    save_row->addWidget(save_body_);
    left_layout->addLayout(save_row);

    result_section_ = new QWidget(left);
    auto* result_layout = new QVBoxLayout(result_section_);
    result_layout->setContentsMargins(0, 0, 0, 0);
    result_layout->setSpacing(5);
    result_layout->addWidget(SectionLabel(QStringLiteral("RESULT"), result_section_));
    result_value_ = new QLabel(result_section_);
    result_value_->setWordWrap(true);
    result_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    result_layout->addWidget(result_value_);
    left_layout->addWidget(result_section_);

    summary_section_ = new QWidget(left);
    auto* summary_layout = new QVBoxLayout(summary_section_);
    summary_layout->setContentsMargins(0, 0, 0, 0);
    summary_layout->setSpacing(5);
    summary_layout->addWidget(SectionLabel(QStringLiteral("LATEST SUMMARY"), summary_section_));
    summary_value_ = new QLabel(summary_section_);
    summary_value_->setWordWrap(true);
    summary_value_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_layout->addWidget(summary_value_);
    left_layout->addWidget(summary_section_);

    auto* diag_section = new QWidget(left);
    diag_layout_ = new QVBoxLayout(diag_section);
    diag_layout_->setContentsMargins(0, 0, 0, 0);
    diag_layout_->setSpacing(6);
    diag_layout_->addWidget(SectionLabel(QStringLiteral("DIAGNOSTICS"), diag_section));
    diagnostics_host_ = new QWidget(diag_section);
    diagnostics_layout_ = new QVBoxLayout(diagnostics_host_);
    diagnostics_layout_->setContentsMargins(0, 0, 0, 0);
    diagnostics_layout_->setSpacing(6);
    diag_layout_->addWidget(diagnostics_host_);
    left_layout->addWidget(diag_section);

    auto* links_section = new QWidget(left);
    auto* links_layout = new QVBoxLayout(links_section);
    links_layout->setContentsMargins(0, 0, 0, 0);
    links_layout->setSpacing(5);
    links_layout->addWidget(SectionLabel(QStringLiteral("DEPENDENCIES"), links_section));
    links_host_ = new QWidget(links_section);
    links_host_layout_ = new QVBoxLayout(links_host_);
    links_host_layout_->setContentsMargins(0, 0, 0, 0);
    links_host_layout_->setSpacing(4);
    links_layout->addWidget(links_host_);
    auto* add_link_row = new QHBoxLayout();
    link_combo_ = new QComboBox(links_section);
    link_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    add_parent_ = new QPushButton(QStringLiteral("Add parent"), links_section);
    add_parent_->setObjectName(QStringLiteral("kanbanSecondary"));
    add_child_ = new QPushButton(QStringLiteral("Add child"), links_section);
    add_child_->setObjectName(QStringLiteral("kanbanSecondary"));
    connect(add_parent_, &QPushButton::clicked, this, [this]() {
      const QString parent = link_combo_->currentData().toString();
      if (parent.isEmpty() || current_id_.isEmpty()) return;
      view_->Request(QStringLiteral("POST"), QStringLiteral("/links"),
                     QJsonObject{{QStringLiteral("parent_id"), parent},
                                 {QStringLiteral("child_id"), current_id_}},
                     [this](const QJsonObject&, const QString& error) {
                       if (!error.isEmpty()) {
                         status_->setText(error);
                         return;
                       }
                       view_->ShowTask(current_id_);
                       view_->FetchBoard();
                     });
    });
    connect(add_child_, &QPushButton::clicked, this, [this]() {
      const QString child = link_combo_->currentData().toString();
      if (child.isEmpty() || current_id_.isEmpty()) return;
      view_->Request(QStringLiteral("POST"), QStringLiteral("/links"),
                     QJsonObject{{QStringLiteral("parent_id"), current_id_},
                                 {QStringLiteral("child_id"), child}},
                     [this](const QJsonObject&, const QString& error) {
                       if (!error.isEmpty()) {
                         status_->setText(error);
                         return;
                       }
                       view_->ShowTask(current_id_);
                       view_->FetchBoard();
                     });
    });
    add_link_row->addWidget(link_combo_, 1);
    add_link_row->addWidget(add_parent_);
    add_link_row->addWidget(add_child_);
    links_layout->addLayout(add_link_row);
    left_layout->addWidget(links_section);

    auto* comments_section = new QWidget(left);
    auto* comments_layout = new QVBoxLayout(comments_section);
    comments_layout->setContentsMargins(0, 0, 0, 0);
    comments_layout->setSpacing(5);
    comments_layout->addWidget(SectionLabel(QStringLiteral("COMMENTS"), comments_section));
    comments_host_ = new QWidget(comments_section);
    comments_host_layout_ = new QVBoxLayout(comments_host_);
    comments_host_layout_->setContentsMargins(0, 0, 0, 0);
    comments_host_layout_->setSpacing(5);
    comments_layout->addWidget(comments_host_);
    comment_ = new QTextEdit(comments_section);
    comment_->setObjectName(QStringLiteral("kanbanBody"));
    comment_->setPlaceholderText(QStringLiteral("Add a note for the worker…"));
    comment_->setFixedHeight(58);
    comments_layout->addWidget(comment_);
    auto* add_comment = new QPushButton(QStringLiteral("Add comment"), comments_section);
    add_comment->setObjectName(QStringLiteral("kanbanPrimary"));
    connect(add_comment, &QPushButton::clicked, this, [this]() {
      const QString text = comment_->toPlainText().trimmed();
      if (text.isEmpty() || current_id_.isEmpty()) return;
      view_->Request(
          QStringLiteral("POST"),
          QStringLiteral("/tasks/%1/comments").arg(current_id_),
          QJsonObject{{QStringLiteral("body"), text},
                      {QStringLiteral("author"), QStringLiteral("desktop")}},
          [this](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            comment_->clear();
            view_->ShowTask(current_id_);
            view_->FetchBoard();
          });
    });
    comments_layout->addWidget(add_comment, 0, Qt::AlignRight);
    left_layout->addWidget(comments_section);

    auto* events_section = new QWidget(left);
    auto* events_layout = new QVBoxLayout(events_section);
    events_layout->setContentsMargins(0, 0, 0, 0);
    events_layout->setSpacing(4);
    events_layout->addWidget(SectionLabel(QStringLiteral("ACTIVITY"), events_section));
    events_host_ = new QWidget(events_section);
    events_host_layout_ = new QVBoxLayout(events_host_);
    events_host_layout_->setContentsMargins(0, 0, 0, 0);
    events_host_layout_->setSpacing(2);
    events_layout->addWidget(events_host_);
    left_layout->addWidget(events_section);

    auto* runs_section = new QWidget(left);
    auto* runs_layout = new QVBoxLayout(runs_section);
    runs_layout->setContentsMargins(0, 0, 0, 0);
    runs_layout->setSpacing(4);
    runs_layout->addWidget(SectionLabel(QStringLiteral("RUNS"), runs_section));
    runs_host_ = new QWidget(runs_section);
    runs_host_layout_ = new QVBoxLayout(runs_host_);
    runs_host_layout_->setContentsMargins(0, 0, 0, 0);
    runs_host_layout_->setSpacing(3);
    runs_layout->addWidget(runs_host_);
    left_layout->addWidget(runs_section);

    auto* log_section = new QWidget(left);
    auto* log_layout = new QVBoxLayout(log_section);
    log_layout->setContentsMargins(0, 0, 0, 0);
    log_layout->setSpacing(5);
    auto* log_header = new QHBoxLayout();
    log_header->addWidget(SectionLabel(QStringLiteral("WORKER LOG"), log_section), 1);
    log_button_ = new QPushButton(QStringLiteral("Load worker log"), log_section);
    log_button_->setObjectName(QStringLiteral("kanbanSecondary"));
    log_header->addWidget(log_button_);
    log_layout->addLayout(log_header);
    log_view_ = new QPlainTextEdit(log_section);
    log_view_->setReadOnly(true);
    log_view_->setMinimumHeight(180);
    log_view_->setFont(theme_.MonoFont(10, QFont::Normal));
    log_view_->hide();
    log_layout->addWidget(log_view_);
    connect(log_button_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      view_->Request(
          QStringLiteral("GET"),
          QStringLiteral("/tasks/%1/log?tail=16384").arg(current_id_),
          {}, [this](const QJsonObject& data, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            const kanban::KanbanLog log = kanban::ParseLog(data);
            log_view_->setPlainText(
                log.content.isEmpty() ? QStringLiteral("(no log yet)") : log.content);
            log_view_->show();
            log_button_->setText(QStringLiteral("Reload log"));
            if (log.truncated) {
              status_->setText(QStringLiteral("Showing the last 16 KB of the worker log."));
            }
          });
    });
    left_layout->addWidget(log_section);

    auto* att_section = new QWidget(left);
    auto* att_layout = new QVBoxLayout(att_section);
    att_layout->setContentsMargins(0, 0, 0, 0);
    att_layout->setSpacing(5);
    auto* att_header = new QHBoxLayout();
    att_header->addWidget(SectionLabel(QStringLiteral("ATTACHMENTS"), att_section), 1);
    upload_ = new QPushButton(QStringLiteral("Upload"), att_section);
    upload_->setObjectName(QStringLiteral("kanbanSecondary"));
    att_header->addWidget(upload_);
    att_layout->addLayout(att_header);
    attachments_host_ = new QWidget(att_section);
    attachments_layout_ = new QVBoxLayout(attachments_host_);
    attachments_layout_->setContentsMargins(0, 0, 0, 0);
    attachments_layout_->setSpacing(3);
    att_layout->addWidget(attachments_host_);
    connect(upload_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty() || !view_->on_upload_attachment) return;
      const QString path = QFileDialog::getOpenFileName(
          this, QStringLiteral("Attach a file"));
      if (path.isEmpty()) return;
      status_->setText(QStringLiteral("Uploading…"));
      view_->on_upload_attachment(
          view_->BoardPath(QStringLiteral("/tasks/%1/attachments")
                               .arg(current_id_)),
          path, [this](const QJsonObject&, const QString& error) {
                                    if (!error.isEmpty()) {
                                      status_->setText(error);
                                      return;
                                    }
              view_->ShowTask(current_id_);
            });
    });
    left_layout->addWidget(att_section);
    left_layout->addStretch(1);
    scroll->setWidget(left);
    body_row->addWidget(scroll, 1);

    auto* side = new QWidget(this);
    side->setFixedWidth(246);
    auto* side_layout = new QVBoxLayout(side);
    side_layout->setContentsMargins(0, 0, 0, 0);
    side_layout->setSpacing(8);
    side_layout->addWidget(SectionLabel(QStringLiteral("PROPERTIES"), side));

    status_combo_ = new QComboBox(side);
    connect(status_combo_, &QComboBox::activated, this, [this]() {
      if (applying_ || current_id_.isEmpty()) return;
      const QString status = status_combo_->currentData().toString();
      if (!kanban::CanMove(detail_.task.status, status)) {
        status_->setText(kanban::LockedReason(status).isEmpty()
                             ? QStringLiteral("That move is not allowed here.")
                             : kanban::LockedReason(status));
        const int index = status_combo_->findData(detail_.task.status);
        applying_ = true;
        status_combo_->setCurrentIndex(std::max(0, index));
        applying_ = false;
        return;
      }
      view_->MoveTask(current_id_, status);
    });
    side_layout->addWidget(MetaRow(QStringLiteral("Status"), status_combo_, side));

    assignee_combo_ = new QComboBox(side);
    connect(assignee_combo_, &QComboBox::activated, this, [this]() {
      if (applying_ || current_id_.isEmpty()) return;
      const QString profile = assignee_combo_->currentData().toString();
      view_->Request(
          QStringLiteral("POST"),
          QStringLiteral("/tasks/%1/reassign").arg(current_id_),
          QJsonObject{{QStringLiteral("profile"), profile},
                      {QStringLiteral("reclaim_first"), true}},
          [this](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            view_->ShowTask(current_id_);
            view_->FetchBoard();
          });
    });
    side_layout->addWidget(MetaRow(QStringLiteral("Assignee"), assignee_combo_, side));

    priority_ = new QSpinBox(side);
    priority_->setRange(0, 4);
    connect(priority_, &QSpinBox::valueChanged, this, [this](int value) {
      if (applying_ || current_id_.isEmpty()) return;
      view_->BulkPriority(QStringList{current_id_}, value);
    });
    side_layout->addWidget(MetaRow(QStringLiteral("Priority"), priority_, side));

    created_value_ = ValueLabel(side);
    side_layout->addWidget(MetaRow(QStringLiteral("Created"), created_value_, side));
    model_value_ = ValueLabel(side);
    side_layout->addWidget(MetaRow(QStringLiteral("Model"), model_value_, side));
    workspace_value_ = ValueLabel(side);
    side_layout->addWidget(MetaRow(QStringLiteral("Workspace"), workspace_value_, side));

    auto* actions = new QHBoxLayout();
    reclaim_ = new QPushButton(QStringLiteral("Reclaim"), side);
    reclaim_->setObjectName(QStringLiteral("kanbanSecondary"));
    unblock_ = new QPushButton(QStringLiteral("Unblock"), side);
    unblock_->setObjectName(QStringLiteral("kanbanSecondary"));
    connect(reclaim_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      view_->Request(
          QStringLiteral("POST"),
          QStringLiteral("/tasks/%1/reclaim").arg(current_id_),
          QJsonObject{{QStringLiteral("reason"), QStringLiteral("dashboard")}},
          [this](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            status_->setText(QStringLiteral("Claim released."));
            view_->ShowTask(current_id_);
            view_->FetchBoard();
          });
    });
    connect(unblock_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      view_->PatchTask(current_id_,
                       kanban::BuildMovePayload(QStringLiteral("ready")),
                       QStringLiteral("Task unblocked"));
    });
    actions->addWidget(reclaim_);
    actions->addWidget(unblock_);
    side_layout->addLayout(actions);

    auto* decompose = new QPushButton(QStringLiteral("Decompose"), side);
    decompose->setObjectName(QStringLiteral("kanbanSecondary"));
    connect(decompose, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      status_->setText(QStringLiteral("Decomposing…"));
      view_->Request(
          QStringLiteral("POST"),
          QStringLiteral("/tasks/%1/decompose").arg(current_id_),
          QJsonObject{{QStringLiteral("author"), QStringLiteral("desktop")}},
          [this](const QJsonObject& data, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            const bool ok = data.value(QStringLiteral("ok")).toBool();
            const QString reason = data.value(QStringLiteral("reason")).toString();
            status_->setText(ok ? QStringLiteral("Decomposed into %1 child task(s).")
                                      .arg(data.value(QStringLiteral("child_ids")).toArray().size())
                                : (reason.isEmpty() ? QStringLiteral("Decompose did not run.")
                                                    : reason));
            view_->ShowTask(current_id_);
            view_->FetchBoard();
          });
    });
    side_layout->addWidget(decompose);

    auto* specify = new QPushButton(QStringLiteral("Specify"), side);
    specify->setObjectName(QStringLiteral("kanbanSecondary"));
    connect(specify, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      view_->Request(
          QStringLiteral("POST"),
          QStringLiteral("/tasks/%1/specify").arg(current_id_),
          QJsonObject{{QStringLiteral("author"), QStringLiteral("desktop")}},
          [this](const QJsonObject& data, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            const bool ok = data.value(QStringLiteral("ok")).toBool();
            const QString reason = data.value(QStringLiteral("reason")).toString();
            status_->setText(ok ? QStringLiteral("Specification written.")
                                : (reason.isEmpty() ? QStringLiteral("Specify did not run.")
                                                    : reason));
            view_->ShowTask(current_id_);
            view_->FetchBoard();
          });
    });
    side_layout->addWidget(specify);

    side_layout->addStretch(1);
    delete_ = new QPushButton(QStringLiteral("Delete task…"), side);
    delete_->setObjectName(QStringLiteral("kanbanDanger"));
    connect(delete_, &QPushButton::clicked, this, [this]() {
      if (current_id_.isEmpty()) return;
      const auto answer = QMessageBox::question(
          this, QStringLiteral("Delete task"),
          QStringLiteral("Delete %1 permanently? This cannot be undone.")
              .arg(kanban::ShortId(current_id_)));
      if (answer != QMessageBox::Yes) return;
      const QString id = current_id_;
      QDialog::close();
      view_->DeleteTask(id);
    });
    side_layout->addWidget(delete_);
    body_row->addWidget(side);
    outer->addLayout(body_row, 1);
  }

  static QLabel* SectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("kanbanSection"));
    return label;
  }

  static QLabel* ValueLabel(QWidget* parent) {
    auto* label = new QLabel(parent);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("font-size: 10px;"));
    return label;
  }

  static QWidget* MetaRow(const QString& label, QWidget* value,
                          QWidget* parent) {
    auto* row = new QWidget(parent);
    auto* layout = new QVBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    layout->addWidget(SectionLabel(label, row));
    layout->addWidget(value);
    return row;
  }

  void RebuildDiagnostics() {
    ClearLayout(diagnostics_layout_);
    if (detail_.task.diagnostics.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("No diagnostics."), diagnostics_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      diagnostics_layout_->addWidget(empty);
      return;
    }
    for (const kanban::KanbanDiagnostic& diag : detail_.task.diagnostics) {
      auto* card = new QWidget(diagnostics_host_);
      card->setObjectName(QStringLiteral("kanbanCallout"));
      auto* layout = new QVBoxLayout(card);
      layout->setContentsMargins(9, 7, 9, 7);
      layout->setSpacing(3);
      auto* title = new QLabel(
          QStringLiteral("%1%2").arg(diag.title,
                                     diag.count > 1
                                         ? QStringLiteral(" ×%1").arg(diag.count)
                                         : QString()),
          card);
      title->setWordWrap(true);
      title->setStyleSheet(
          QStringLiteral("font-weight: 600; color: %1;")
              .arg(diag.severity == QLatin1String("error")
                       ? theme_.danger.name()
                       : (diag.severity == QLatin1String("warning")
                              ? theme_.warning.name()
                              : theme_.text_secondary.name())));
      layout->addWidget(title);
      auto* detail = new QLabel(diag.detail, card);
      detail->setWordWrap(true);
      detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
      detail->setObjectName(QStringLiteral("kanbanMuted"));
      layout->addWidget(detail);
      auto* buttons = new QHBoxLayout();
      for (const kanban::KanbanDiagnosticAction& action : diag.actions) {
        if (action.kind != QLatin1String("reclaim") &&
            action.kind != QLatin1String("unblock") &&
            action.kind != QLatin1String("cli_hint")) {
          continue;
        }
        auto* button = new QPushButton(action.label, card);
        button->setObjectName(QStringLiteral("kanbanSecondary"));
        connect(button, &QPushButton::clicked, this, [this, action]() {
          if (action.kind == QLatin1String("reclaim")) {
            reclaim_->click();
          } else if (action.kind == QLatin1String("unblock")) {
            unblock_->click();
          } else {
            QApplication::clipboard()->setText(
                action.payload.value(QStringLiteral("command")).toString(
                    action.label));
            status_->setText(QStringLiteral("Command copied."));
          }
        });
        buttons->addWidget(button);
      }
      buttons->addStretch(1);
      layout->addLayout(buttons);
      diagnostics_layout_->addWidget(card);
    }
  }

  void RebuildLinks() {
    ClearLayout(links_host_layout_);
    auto add_link = [this](const QString& label, const QString& id,
                           const QString& parent, const QString& child) {
      auto* row = new QWidget(links_host_);
      auto* layout = new QHBoxLayout(row);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(5);
      const QString title = TaskTitle(id);
      auto* text = new QLabel(QStringLiteral("%1 · %2").arg(label, title), row);
      text->setToolTip(id + QStringLiteral(" ") + title);
      text->setObjectName(QStringLiteral("kanbanMuted"));
      text->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(text, 1);
      auto* remove = new QPushButton(QStringLiteral("×"), row);
      remove->setObjectName(QStringLiteral("kanbanSecondary"));
      remove->setFixedWidth(26);
      connect(remove, &QPushButton::clicked, this, [this, parent, child]() {
        view_->Request(
            QStringLiteral("DELETE"),
            QStringLiteral("/links?parent_id=%1&child_id=%2")
                                 .arg(parent, child),
            {}, [this](const QJsonObject&, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              view_->ShowTask(current_id_);
              view_->FetchBoard();
            });
      });
      layout->addWidget(remove);
      links_host_layout_->addWidget(row);
    };
    if (detail_.task.parents.isEmpty() && detail_.task.children.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("No linked tasks."), links_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      links_host_layout_->addWidget(empty);
    }
    for (const QString& parent : detail_.task.parents) {
      add_link(QStringLiteral("Parent"), parent, parent, detail_.task.id);
    }
    for (const QString& child : detail_.task.children) {
      add_link(QStringLiteral("Child"), child, detail_.task.id, child);
    }
    link_combo_->clear();
    for (const kanban::KanbanTask& task : all_tasks_) {
      if (task.id == detail_.task.id) {
        continue;
      }
      link_combo_->addItem(QStringLiteral("%1 · %2")
                               .arg(kanban::ShortId(task.id), task.title),
                           task.id);
    }
  }

  QString TaskTitle(const QString& id) const {
    for (const kanban::KanbanTask& task : all_tasks_) {
      if (task.id == id) {
        return task.title;
      }
    }
    return kanban::ShortId(id);
  }

  void RebuildComments() {
    ClearLayout(comments_host_layout_);
    if (detail_.comments.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("No comments yet."), comments_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      comments_host_layout_->addWidget(empty);
      return;
    }
    for (const kanban::KanbanComment& comment : detail_.comments) {
      auto* card = new QWidget(comments_host_);
      auto* layout = new QVBoxLayout(card);
      layout->setContentsMargins(9, 6, 9, 6);
      layout->setSpacing(2);
      auto* header = new QLabel(
          QStringLiteral("%1 · %2").arg(comment.author, comment.created_at),
          card);
      header->setObjectName(QStringLiteral("kanbanMuted"));
      header->setStyleSheet(QStringLiteral("font-size: 9px;"));
      layout->addWidget(header);
      auto* body = new QLabel(comment.body, card);
      body->setWordWrap(true);
      body->setTextInteractionFlags(Qt::TextSelectableByMouse);
      layout->addWidget(body);
      comments_host_layout_->addWidget(card);
    }
  }

  void RebuildEvents() {
    ClearLayout(events_host_layout_);
    if (detail_.events.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("No activity recorded."), events_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      events_host_layout_->addWidget(empty);
      return;
    }
    for (const kanban::KanbanEvent& event : detail_.events) {
      auto* label = new QLabel(
          QStringLiteral("%1  %2").arg(event.created_at, EventLine(event)),
          events_host_);
      label->setWordWrap(true);
      label->setTextInteractionFlags(Qt::TextSelectableByMouse);
      label->setObjectName(QStringLiteral("kanbanMuted"));
      events_host_layout_->addWidget(label);
    }
  }

  void RebuildRuns() {
    ClearLayout(runs_host_layout_);
    if (detail_.runs.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("This task has not run yet."), runs_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      runs_host_layout_->addWidget(empty);
      return;
    }
    for (const kanban::KanbanRun& run : detail_.runs) {
      QString text = QStringLiteral("%1 · %2")
                         .arg(run.profile.isEmpty() ? QStringLiteral("worker")
                                                    : run.profile,
                              run.status);
      if (!run.started_at.isEmpty()) {
        text += QStringLiteral(" · started %1").arg(run.started_at);
      }
      if (!run.finished_at.isEmpty()) {
        text += QStringLiteral(" · ended %1").arg(run.finished_at);
      }
      auto* label = new QLabel(text, runs_host_);
      label->setObjectName(QStringLiteral("kanbanMuted"));
      label->setWordWrap(true);
      label->setTextInteractionFlags(Qt::TextSelectableByMouse);
      runs_host_layout_->addWidget(label);
      if (!run.summary.isEmpty()) {
        auto* summary = new QLabel(run.summary, runs_host_);
        summary->setWordWrap(true);
        summary->setObjectName(QStringLiteral("kanbanMuted"));
        summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
        runs_host_layout_->addWidget(summary);
      }
      if (!run.error.isEmpty()) {
        auto* error = new QLabel(run.error, runs_host_);
        error->setWordWrap(true);
        error->setStyleSheet(QStringLiteral("color: %1;").arg(theme_.danger.name()));
        runs_host_layout_->addWidget(error);
      }
    }
  }

  void RebuildAttachments() {
    ClearLayout(attachments_layout_);
    if (detail_.attachments.isEmpty()) {
      auto* empty = new QLabel(QStringLiteral("No attachments."), attachments_host_);
      empty->setObjectName(QStringLiteral("kanbanMuted"));
      attachments_layout_->addWidget(empty);
      return;
    }
    for (const kanban::KanbanAttachment& attachment : detail_.attachments) {
      auto* row = new QWidget(attachments_host_);
      auto* layout = new QHBoxLayout(row);
      layout->setContentsMargins(0, 0, 0, 0);
      layout->setSpacing(5);
      auto* name = new QLabel(
          QStringLiteral("%1  (%2)").arg(attachment.filename,
                                         SizeLabel(attachment.size)),
          row);
      name->setObjectName(QStringLiteral("kanbanMuted"));
      name->setTextInteractionFlags(Qt::TextSelectableByMouse);
      name->setToolTip(attachment.path);
      layout->addWidget(name, 1);
      auto* download = new QPushButton(QStringLiteral("Save"), row);
      download->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(download, &QPushButton::clicked, this, [this, attachment]() {
        if (!view_->on_download_attachment) return;
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Save attachment"),
            QDir::homePath() + QStringLiteral("/") + attachment.filename);
        if (path.isEmpty()) return;
        status_->setText(QStringLiteral("Downloading…"));
        view_->on_download_attachment(
            view_->BoardPath(QStringLiteral("/attachments/%1")
                                 .arg(attachment.id)),
            path, [this](const QJsonObject&, const QString& error) {
              status_->setText(error.isEmpty() ? QStringLiteral("Saved.")
                                               : error);
            });
      });
      layout->addWidget(download);
      auto* remove = new QPushButton(QStringLiteral("Delete"), row);
      remove->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(remove, &QPushButton::clicked, this, [this, attachment]() {
        view_->Request(
            QStringLiteral("DELETE"),
            QStringLiteral("/attachments/%1").arg(attachment.id),
            {}, [this](const QJsonObject&, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              view_->ShowTask(current_id_);
            });
      });
      layout->addWidget(remove);
      attachments_layout_->addWidget(row);
    }
  }

  void ApplyStyle() {
    setStyleSheet(
        QStringLiteral(
            "QDialog#kanbanTaskDialog { background: %1; color: %2; }"
            "QLabel#kanbanTaskTitle { color: %2; font-size: 18px; font-weight: 600; }"
            "QLabel#kanbanTaskStatus { color: %3; font-size: 10px; }"
            "QLabel#kanbanSection { color: %3; font-size: 9px; font-weight: 700; "
            "letter-spacing: 0.8px; }"
            "QLabel#kanbanMuted { color: %3; font-size: 10px; }"
            "QTextEdit#kanbanBody, QPlainTextEdit { background: %4; color: %2; "
            "border: 1px solid %5; border-radius: 8px; padding: 6px; }"
            "QWidget#kanbanCallout { background: %4; border: 1px solid %5; "
            "border-radius: 8px; }"
            "QPushButton#kanbanPrimary { background: %6; color: %7; "
            "border: none; border-radius: 7px; padding: 6px 12px; font-weight: 600; }"
            "QPushButton#kanbanSecondary { background: transparent; color: %3; "
            "border: 1px solid %5; border-radius: 7px; padding: 5px 9px; }"
            "QPushButton#kanbanSecondary:hover { color: %2; background: %8; }"
            "QPushButton#kanbanDanger { background: transparent; color: %9; "
            "border: 1px solid %9; border-radius: 7px; padding: 5px 9px; }")
            .arg(theme_.background.name(), theme_.foreground.name(),
                 theme_.text_secondary.name(), theme_.surface.name(),
                 theme_.stroke.name(), theme_.accent.name(),
                 theme_.accent_text.name(), theme_.hover.name(),
                 theme_.danger.name()));
    log_view_->setFont(theme_.MonoFont(10, QFont::Normal));
    update();
  }

  KanbanView* view_ = nullptr;
  Theme theme_;
  KanbanTaskDetail detail_;
  QVector<kanban::KanbanTask> all_tasks_;
  QStringList column_list_;
  QStringList assignees_;
  QString default_assignee_;
  QString current_id_;
  QString log_task_;
  bool applying_ = false;
  bool body_dirty_ = false;

  QLabel* title_ = nullptr;
  QLabel* status_chip_ = nullptr;
  QLabel* status_ = nullptr;
  QTextEdit* body_ = nullptr;
  QPushButton* save_body_ = nullptr;
  QWidget* result_section_ = nullptr;
  QLabel* result_value_ = nullptr;
  QVBoxLayout* diag_layout_ = nullptr;
  QWidget* summary_section_ = nullptr;
  QLabel* summary_value_ = nullptr;
  QWidget* diagnostics_host_ = nullptr;
  QVBoxLayout* diagnostics_layout_ = nullptr;
  QWidget* links_host_ = nullptr;
  QVBoxLayout* links_host_layout_ = nullptr;
  QComboBox* link_combo_ = nullptr;
  QPushButton* add_parent_ = nullptr;
  QPushButton* add_child_ = nullptr;
  QWidget* comments_host_ = nullptr;
  QVBoxLayout* comments_host_layout_ = nullptr;
  QTextEdit* comment_ = nullptr;
  QWidget* events_host_ = nullptr;
  QVBoxLayout* events_host_layout_ = nullptr;
  QWidget* runs_host_ = nullptr;
  QVBoxLayout* runs_host_layout_ = nullptr;
  QWidget* attachments_host_ = nullptr;
  QVBoxLayout* attachments_layout_ = nullptr;
  QPushButton* upload_ = nullptr;
  QPushButton* log_button_ = nullptr;
  QPlainTextEdit* log_view_ = nullptr;
  QComboBox* status_combo_ = nullptr;
  QComboBox* assignee_combo_ = nullptr;
  QSpinBox* priority_ = nullptr;
  QLabel* created_value_ = nullptr;
  QLabel* model_value_ = nullptr;
  QLabel* workspace_value_ = nullptr;
  QPushButton* reclaim_ = nullptr;
  QPushButton* unblock_ = nullptr;
  QPushButton* delete_ = nullptr;
};


// ---------------------------------------------------------------------------
// New task dialog
// ---------------------------------------------------------------------------

class KanbanNewTaskDialog : public QDialog {
 public:
  KanbanNewTaskDialog(KanbanView* view, Theme theme, const QString& column,
                      QWidget* parent = nullptr)
      : QDialog(parent ? parent : view), view_(view), theme_(std::move(theme)),
        column_(column) {
    setObjectName(QStringLiteral("kanbanNewTaskDialog"));
    setWindowTitle(QStringLiteral("New task"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(660, 700);
    BuildUi();
    ApplyStyle();
  }

  void SetTheme(const Theme& theme) {
    theme_ = theme;
    ApplyStyle();
  }

 private:
  void BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 14);
    outer->setSpacing(9);
    auto* title = new QLabel(QStringLiteral("New task"), this);
    title->setObjectName(QStringLiteral("kanbanDialogTitle"));
    outer->addWidget(title);
    auto* hint = new QLabel(
        QStringLiteral("Cards are dispatcher work items. A worker claims a ready "
                       "card; nothing here runs just because it exists."),
        this);
    hint->setObjectName(QStringLiteral("kanbanMuted"));
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* form = new QFormLayout();
    form->setLabelAlignment(Qt::AlignLeft);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    title_ = new QLineEdit(this);
    title_->setPlaceholderText(QStringLiteral("What needs doing?"));
    form->addRow(QStringLiteral("Title"), title_);
    body_ = new QTextEdit(this);
    body_->setObjectName(QStringLiteral("kanbanBody"));
    body_->setPlaceholderText(QStringLiteral("Context, acceptance criteria, links…"));
    body_->setFixedHeight(112);
    form->addRow(QStringLiteral("Body"), body_);
    column_combo_ = new QComboBox(this);
    for (const QString& slug : view_->board_.columns) {
      column_combo_->addItem(kanban::ColumnLabel(slug), slug);
    }
    int index = column_combo_->findData(column_);
    if (index < 0) {
      index = column_combo_->findData(QStringLiteral("triage"));
    }
    if (index >= 0) {
      column_combo_->setCurrentIndex(index);
    }
    form->addRow(QStringLiteral("Column"), column_combo_);
    assignee_combo_ = new QComboBox(this);
    assignee_combo_->addItem(QStringLiteral("Unassigned"), QString());
    for (const kanban::KanbanProfile& profile : view_->profiles_) {
      assignee_combo_->addItem(profile.name, profile.name);
    }
    if (!view_->orchestration_.resolved_default_assignee.isEmpty()) {
      const int assignee_index = assignee_combo_->findData(
          view_->orchestration_.resolved_default_assignee);
      if (assignee_index >= 0) {
        assignee_combo_->setCurrentIndex(assignee_index);
      }
    }
    form->addRow(QStringLiteral("Assignee"), assignee_combo_);
    priority_ = new QSpinBox(this);
    priority_->setRange(0, 4);
    form->addRow(QStringLiteral("Priority"), priority_);
    workspace_kind_ = new QComboBox(this);
    workspace_kind_->addItem(QStringLiteral("Board default"), QString());
    workspace_kind_->addItem(QStringLiteral("Scratch sandbox"), QStringLiteral("scratch"));
    workspace_kind_->addItem(QStringLiteral("Worktree"), QStringLiteral("worktree"));
    workspace_kind_->addItem(QStringLiteral("Directory"), QStringLiteral("dir"));
    form->addRow(QStringLiteral("Workspace"), workspace_kind_);
    auto* path_row = new QWidget(this);
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(6);
    workspace_path_ = new QLineEdit(path_row);
    workspace_path_->setPlaceholderText(QStringLiteral("absolute path (optional)"));
    path_layout->addWidget(workspace_path_, 1);
    auto* browse = new QPushButton(QStringLiteral("…"), path_row);
    browse->setObjectName(QStringLiteral("kanbanSecondary"));
    browse->setFixedWidth(30);
    connect(browse, &QPushButton::clicked, this, [this]() {
      const QString path = QFileDialog::getExistingDirectory(
          this, QStringLiteral("Working directory"));
      if (!path.isEmpty()) {
        workspace_path_->setText(path);
        if (workspace_kind_->currentData().toString().isEmpty()) {
          workspace_kind_->setCurrentIndex(
              workspace_kind_->findData(QStringLiteral("dir")));
        }
      }
    });
    path_layout->addWidget(browse);
    form->addRow(QStringLiteral("Path"), path_row);
    skills_ = new QLineEdit(this);
    skills_->setPlaceholderText(QStringLiteral("skill-a, skill-b"));
    form->addRow(QStringLiteral("Skills"), skills_);
    tenant_ = new QLineEdit(this);
    tenant_->setPlaceholderText(QStringLiteral("personal"));
    form->addRow(QStringLiteral("Tenant"), tenant_);
    parent_combo_ = new QComboBox(this);
    parent_combo_->addItem(QStringLiteral("None"), QString());
    for (const kanban::KanbanTask& task : view_->board_.tasks) {
      parent_combo_->addItem(
          QStringLiteral("%1 · %2").arg(kanban::ShortId(task.id), task.title),
          task.id);
    }
    form->addRow(QStringLiteral("Parent"), parent_combo_);
    auto* model_row = new QWidget(this);
    auto* model_layout = new QHBoxLayout(model_row);
    model_layout->setContentsMargins(0, 0, 0, 0);
    model_layout->setSpacing(6);
    provider_ = new QLineEdit(model_row);
    provider_->setPlaceholderText(QStringLiteral("provider"));
    model_ = new QLineEdit(model_row);
    model_->setPlaceholderText(QStringLiteral("model (empty = inherit)"));
    model_layout->addWidget(provider_);
    model_layout->addWidget(model_, 1);
    form->addRow(QStringLiteral("Model"), model_row);
    goal_mode_ = new QCheckBox(QStringLiteral("Goal mode (keep iterating until done)"));
    form->addRow(QString(), goal_mode_);
    outer->addLayout(form);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("kanbanMuted"));
    status_->setWordWrap(true);
    outer->addWidget(status_);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"),
                                      QDialogButtonBox::RejectRole);
    create_ = buttons->addButton(QStringLiteral("Create task"),
                                 QDialogButtonBox::AcceptRole);
    cancel->setObjectName(QStringLiteral("kanbanSecondary"));
    create_->setObjectName(QStringLiteral("kanbanPrimary"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(create_, &QPushButton::clicked, this, [this]() {
      const QString title = title_->text().trimmed();
      if (title.isEmpty()) {
        status_->setText(QStringLiteral("A title is required."));
        return;
      }
      kanban::KanbanNewTask task;
      task.title = title;
      task.body = body_->toPlainText();
      task.status = column_combo_->currentData().toString();
      task.assignee = assignee_combo_->currentData().toString();
      task.priority = priority_->value();
      task.workspace_kind = workspace_kind_->currentData().toString();
      task.workspace_path = workspace_path_->text().trimmed();
      task.skills = skills_->text();
      task.tenant = tenant_->text().trimmed();
      task.parent_id = parent_combo_->currentData().toString();
      task.model_override = model_->text().trimmed();
      task.provider_override = provider_->text().trimmed();
      task.goal_mode = goal_mode_->isChecked();
      create_->setEnabled(false);
      status_->setText(QStringLiteral("Creating…"));
      view_->CreateTask(task);
      accept();
    });
    outer->addWidget(buttons);
  }

  void ApplyStyle() {
    setStyleSheet(
        QStringLiteral(
            "QDialog#kanbanNewTaskDialog { background: %1; color: %2; }"
            "QLabel#kanbanDialogTitle { color: %2; font-size: 18px; "
            "font-weight: 600; }"
            "QLabel#kanbanMuted { color: %3; font-size: 10px; }"
            "QLineEdit, QComboBox, QSpinBox, QTextEdit#kanbanBody { "
            "background: %4; color: %2; border: 1px solid %5; "
            "border-radius: 7px; padding: 5px 7px; }"
            "QPushButton#kanbanPrimary { background: %6; color: %7; "
            "border: none; border-radius: 7px; padding: 7px 14px; "
            "font-weight: 600; }"
            "QPushButton#kanbanSecondary { background: transparent; color: %3; "
            "border: 1px solid %5; border-radius: 7px; padding: 6px 12px; }")
            .arg(theme_.background.name(), theme_.foreground.name(),
                 theme_.text_secondary.name(), theme_.surface.name(),
                 theme_.stroke.name(), theme_.accent.name(),
                 theme_.accent_text.name()));
    update();
  }

  KanbanView* view_ = nullptr;
  Theme theme_;
  QString column_;
  QLineEdit* title_ = nullptr;
  QTextEdit* body_ = nullptr;
  QComboBox* column_combo_ = nullptr;
  QComboBox* assignee_combo_ = nullptr;
  QSpinBox* priority_ = nullptr;
  QComboBox* workspace_kind_ = nullptr;
  QLineEdit* workspace_path_ = nullptr;
  QLineEdit* skills_ = nullptr;
  QLineEdit* tenant_ = nullptr;
  QComboBox* parent_combo_ = nullptr;
  QLineEdit* provider_ = nullptr;
  QLineEdit* model_ = nullptr;
  QCheckBox* goal_mode_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* create_ = nullptr;
};

// ---------------------------------------------------------------------------
// Board settings dialog
// ---------------------------------------------------------------------------

class KanbanBoardSettingsDialog : public QDialog {
 public:
  KanbanBoardSettingsDialog(KanbanView* view, Theme theme, bool create,
                            QWidget* parent = nullptr)
      : QDialog(parent ? parent : view), view_(view), theme_(std::move(theme)),
        create_(create) {
    setObjectName(QStringLiteral("kanbanBoardSettingsDialog"));
    setWindowTitle(create ? QStringLiteral("New board")
                          : QStringLiteral("Board settings"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(520, create ? 430 : 520);
    BuildUi();
    ApplyStyle();
  }

 private:
  void BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 14);
    outer->setSpacing(9);
    auto* heading = new QLabel(create_ ? QStringLiteral("New board")
                                      : QStringLiteral("Board settings"),
                               this);
    heading->setObjectName(QStringLiteral("kanbanDialogTitle"));
    outer->addWidget(heading);
    auto* hint = new QLabel(
        QStringLiteral("A board is scoped to a Hermes project. Its primary repo "
                       "becomes the default workspace root; new tasks inherit a "
                       "worktree and a deterministic branch. A board without a "
                       "project uses scratch sandboxes."),
        this);
    hint->setObjectName(QStringLiteral("kanbanMuted"));
    hint->setWordWrap(true);
    outer->addWidget(hint);
    auto* form = new QFormLayout();
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(8);
    name_ = new QLineEdit(this);
    name_->setPlaceholderText(QStringLiteral("Board name"));
    if (!create_) {
      name_->setText(view_->meta_.name);
    }
    form->addRow(QStringLiteral("Name"), name_);
    slug_ = new QLineEdit(this);
    slug_->setPlaceholderText(QStringLiteral("slug (immutable once created)"));
    if (!create_) {
      slug_->setText(view_->meta_.slug);
      slug_->setEnabled(false);
    }
    form->addRow(QStringLiteral("Slug"), slug_);
    description_ = new QLineEdit(this);
    description_->setPlaceholderText(QStringLiteral("Optional description"));
    if (!create_) {
      description_->setText(view_->meta_.description);
    }
    form->addRow(QStringLiteral("Description"), description_);
    project_ = new QComboBox(this);
    project_->addItem(QStringLiteral("No project (scratch sandboxes)"), QString());
    for (const kanban::KanbanProject& project : view_->projects_) {
      project_->addItem(project.name, project.id);
    }
    if (!create_) {
      const int index = project_->findData(view_->meta_.project_id);
      if (index >= 0) {
        project_->setCurrentIndex(index);
      }
    }
    form->addRow(QStringLiteral("Project"), project_);
    auto* path_row = new QWidget(this);
    auto* path_layout = new QHBoxLayout(path_row);
    path_layout->setContentsMargins(0, 0, 0, 0);
    path_layout->setSpacing(6);
    workdir_ = new QLineEdit(path_row);
    workdir_->setPlaceholderText(QStringLiteral("absolute path (optional)"));
    if (!create_) {
      workdir_->setText(view_->meta_.default_workdir);
    }
    path_layout->addWidget(workdir_, 1);
    auto* browse = new QPushButton(QStringLiteral("…"), path_row);
    browse->setObjectName(QStringLiteral("kanbanSecondary"));
    browse->setFixedWidth(30);
    connect(browse, &QPushButton::clicked, this, [this]() {
      const QString path = QFileDialog::getExistingDirectory(
          this, QStringLiteral("Default working directory"));
      if (!path.isEmpty()) {
        workdir_->setText(path);
      }
    });
    path_layout->addWidget(browse);
    form->addRow(QStringLiteral("Workdir"), path_row);
    connect(project_, &QComboBox::currentIndexChanged, this, [this]() {
      for (const kanban::KanbanProject& project : view_->projects_) {
        if (project.id == project_->currentData().toString() &&
            workdir_->text().isEmpty()) {
          workdir_->setText(project.primary_path);
          return;
        }
      }
    });
    outer->addLayout(form);
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("kanbanMuted"));
    status_->setWordWrap(true);
    outer->addWidget(status_);

    if (!create_) {
      auto* transfer = new QHBoxLayout();
      auto* export_button = new QPushButton(QStringLiteral("Export…"), this);
      export_button->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(export_button, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Export board"),
            QDir::homePath() + QStringLiteral("/%1.tar.gz").arg(view_->meta_.slug),
            QStringLiteral("Board archive (*.tar.gz)"));
        if (path.isEmpty()) return;
        status_->setText(QStringLiteral("Exporting…"));
        view_->Request(
            QStringLiteral("POST"),
            QStringLiteral("/boards/%1/export").arg(view_->meta_.slug),
            QJsonObject{{QStringLiteral("output"), path}},
            [this, path](const QJsonObject& data, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              const QString written =
                  data.value(QStringLiteral("path")).toString(
                      data.value(QStringLiteral("output")).toString(path));
              status_->setText(QStringLiteral("Exported to %1").arg(written));
            });
      });
      auto* import_button = new QPushButton(QStringLiteral("Import…"), this);
      import_button->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(import_button, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Import board"), QDir::homePath(),
            QStringLiteral("Board archive (*.tar.gz)"));
        if (path.isEmpty()) return;
        status_->setText(QStringLiteral("Importing…"));
        view_->Request(QStringLiteral("POST"), QStringLiteral("/boards/import"),
                       QJsonObject{{QStringLiteral("archive"), path}},
                       [this](const QJsonObject&, const QString& error) {
                         if (!error.isEmpty()) {
                           status_->setText(error);
                           return;
                         }
                         view_->FetchBoards();
                         view_->FetchBoard();
                         view_->ShowBoardMenu();
                       });
      });
      transfer->addWidget(export_button);
      transfer->addWidget(import_button);
      transfer->addStretch(1);
      outer->addLayout(transfer);
    }
    outer->addStretch(1);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancel = buttons->addButton(QStringLiteral("Cancel"),
                                      QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("kanbanSecondary"));
    auto* save = buttons->addButton(
        create_ ? QStringLiteral("Create board") : QStringLiteral("Save"),
        QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("kanbanPrimary"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this]() {
      const QString name = name_->text().trimmed();
      if (name.isEmpty()) {
        status_->setText(QStringLiteral("A board name is required."));
        return;
      }
      QJsonObject body;
      body.insert(QStringLiteral("name"), name);
      const QString description = description_->text().trimmed();
      body.insert(QStringLiteral("description"), description);
      body.insert(QStringLiteral("project_id"),
                  project_->currentData().toString());
      const QString workdir = workdir_->text().trimmed();
      if (create_) {
        const QString slug = slug_->text().trimmed().isEmpty()
                                 ? kanban::Slugify(name)
                                 : kanban::Slugify(slug_->text());
        body.insert(QStringLiteral("slug"), slug);
        if (!workdir.isEmpty()) {
          body.insert(QStringLiteral("default_workdir"), workdir);
        }
        status_->setText(QStringLiteral("Creating…"));
        view_->Request(
            QStringLiteral("POST"), QStringLiteral("/boards"), body,
            [this, slug](const QJsonObject&, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              view_->FetchBoards();
              view_->SelectBoard(slug, true);
              accept();
            });
      } else {
        if (workdir != view_->meta_.default_workdir) {
          body.insert(QStringLiteral("default_workdir"), workdir);
        }
        status_->setText(QStringLiteral("Saving…"));
        view_->Request(
            QStringLiteral("PATCH"),
            QStringLiteral("/boards/%1").arg(view_->meta_.slug), body,
            [this](const QJsonObject&, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              view_->FetchBoards();
              view_->FetchBoard();
              accept();
            });
      }
    });
    outer->addWidget(buttons);

    if (!create_) {
      auto* danger = new QHBoxLayout();
      danger->addStretch(1);
      auto* remove = new QPushButton(QStringLiteral("Delete board…"), this);
      remove->setObjectName(QStringLiteral("kanbanDanger"));
      connect(remove, &QPushButton::clicked, this, [this]() {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("Delete board"),
            QStringLiteral("Archive board \"%1\"? Its cards stay on disk and the "
                           "board can be restored from the archive.")
                .arg(view_->meta_.name));
        if (answer != QMessageBox::Yes) return;
        view_->Request(
            QStringLiteral("DELETE"),
            QStringLiteral("/boards/%1").arg(view_->meta_.slug), {},
            [this](const QJsonObject&, const QString& error) {
              if (!error.isEmpty()) {
                status_->setText(error);
                return;
              }
              accept();
              view_->FetchBoards();
              view_->SelectBoard(QStringLiteral("default"), true);
            });
      });
      danger->addWidget(remove);
      outer->addLayout(danger);
    }
  }

  void ApplyStyle() {
    setStyleSheet(
        QStringLiteral(
            "QDialog#kanbanBoardSettingsDialog { background: %1; color: %2; }"
            "QLabel#kanbanDialogTitle { color: %2; font-size: 18px; "
            "font-weight: 600; }"
            "QLabel#kanbanMuted { color: %3; font-size: 10px; }"
            "QLineEdit, QComboBox { background: %4; color: %2; "
            "border: 1px solid %5; border-radius: 7px; padding: 5px 7px; }"
            "QPushButton#kanbanPrimary { background: %6; color: %7; "
            "border: none; border-radius: 7px; padding: 7px 14px; "
            "font-weight: 600; }"
            "QPushButton#kanbanSecondary { background: transparent; color: %3; "
            "border: 1px solid %5; border-radius: 7px; padding: 6px 12px; }"
            "QPushButton#kanbanDanger { background: transparent; color: %8; "
            "border: 1px solid %8; border-radius: 7px; padding: 6px 12px; }")
            .arg(theme_.background.name(), theme_.foreground.name(),
                 theme_.text_secondary.name(), theme_.surface.name(),
                 theme_.stroke.name(), theme_.accent.name(),
                 theme_.accent_text.name(), theme_.danger.name()));
    update();
  }

  KanbanView* view_ = nullptr;
  Theme theme_;
  bool create_ = true;
  QLineEdit* name_ = nullptr;
  QLineEdit* slug_ = nullptr;
  QLineEdit* description_ = nullptr;
  QComboBox* project_ = nullptr;
  QLineEdit* workdir_ = nullptr;
  QLabel* status_ = nullptr;
  QPushButton* save_ = nullptr;
};

// ---------------------------------------------------------------------------
// Orchestration dialog
// ---------------------------------------------------------------------------

class KanbanOrchestrationDialog : public QDialog {
 public:
  KanbanOrchestrationDialog(KanbanView* view, Theme theme,
                            QWidget* parent = nullptr)
      : QDialog(parent ? parent : view), view_(view), theme_(std::move(theme)) {
    setObjectName(QStringLiteral("kanbanOrchestrationDialog"));
    setWindowTitle(QStringLiteral("Kanban orchestration"));
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    resize(760, 640);
    BuildUi();
    ApplyStyle();
  }

 private:
  void BuildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(18, 16, 18, 14);
    outer->setSpacing(9);
    auto* heading = new QLabel(QStringLiteral("Orchestration"), this);
    heading->setObjectName(QStringLiteral("kanbanDialogTitle"));
    outer->addWidget(heading);
    auto* hint = new QLabel(
        QStringLiteral("The orchestrator profile decomposes vague cards; the "
                       "default assignee claims cards with no assignee. Profile "
                       "descriptions are routing hints for the decomposer."),
        this);
    hint->setObjectName(QStringLiteral("kanbanMuted"));
    hint->setWordWrap(true);
    outer->addWidget(hint);

    auto* knobs = new QFormLayout();
    knobs->setHorizontalSpacing(12);
    knobs->setVerticalSpacing(8);
    orchestrator_ = new QComboBox(this);
    orchestrator_->addItem(QStringLiteral("Active profile (default)"), QString());
    for (const kanban::KanbanProfile& profile : view_->profiles_) {
      orchestrator_->addItem(profile.name, profile.name);
    }
    const int orchestrator_index =
        orchestrator_->findData(view_->orchestration_.orchestrator_profile);
    orchestrator_->setCurrentIndex(std::max(0, orchestrator_index));
    knobs->addRow(QStringLiteral("Orchestrator"), orchestrator_);
    default_assignee_ = new QComboBox(this);
    default_assignee_->addItem(QStringLiteral("Active profile (default)"), QString());
    for (const kanban::KanbanProfile& profile : view_->profiles_) {
      default_assignee_->addItem(profile.name, profile.name);
    }
    const int assignee_index =
        default_assignee_->findData(view_->orchestration_.default_assignee);
    default_assignee_->setCurrentIndex(std::max(0, assignee_index));
    knobs->addRow(QStringLiteral("Default assignee"), default_assignee_);
    auto_decompose_ = new QCheckBox(
        QStringLiteral("Auto-decompose large cards into children"), this);
    auto_decompose_->setChecked(view_->orchestration_.auto_decompose);
    knobs->addRow(QString(), auto_decompose_);
    auto_promote_ = new QCheckBox(
        QStringLiteral("Promote children when every parent is done"), this);
    auto_promote_->setChecked(view_->orchestration_.auto_promote_children);
    knobs->addRow(QString(), auto_promote_);
    outer->addLayout(knobs);

    outer->addWidget(SectionLabel(QStringLiteral("PROFILE DESCRIPTIONS"), this));
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(7);
    for (const kanban::KanbanProfile& profile : view_->profiles_) {
      auto* row = new QWidget(host);
      auto* row_layout = new QVBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(3);
      auto* label = new QLabel(
          QStringLiteral("%1%2%3")
              .arg(profile.name,
                   profile.model.isEmpty() ? QString()
                                           : QStringLiteral(" · ") + profile.model,
                   profile.is_default ? QStringLiteral(" · default") : QString()),
          row);
      label->setStyleSheet(QStringLiteral("font-weight: 600;"));
      row_layout->addWidget(label);
      auto* edit_row = new QHBoxLayout();
      auto* description = new QLineEdit(profile.description, row);
      description->setPlaceholderText(
          QStringLiteral("What does this profile handle?"));
      edit_row->addWidget(description, 1);
      auto* save = new QPushButton(QStringLiteral("Save"), row);
      save->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(save, &QPushButton::clicked, this,
              [this, name = profile.name, description]() {
                status_->setText(QStringLiteral("Saving %1…").arg(name));
                view_->Request(
                    QStringLiteral("PATCH"),
                    QStringLiteral("/profiles/%1").arg(name),
                    QJsonObject{{QStringLiteral("description"),
                                 description->text()}},
                    [this](const QJsonObject&, const QString& error) {
                      status_->setText(error.isEmpty()
                                           ? QStringLiteral("Profile description saved.")
                                           : error);
                    });
              });
      edit_row->addWidget(save);
      auto* auto_button = new QPushButton(QStringLiteral("Auto"), row);
      auto_button->setObjectName(QStringLiteral("kanbanSecondary"));
      connect(auto_button, &QPushButton::clicked, this,
              [this, name = profile.name, description]() {
                status_->setText(QStringLiteral("Describing %1…").arg(name));
                view_->Request(
                    QStringLiteral("POST"),
                    QStringLiteral("/profiles/%1/describe-auto").arg(name),
                    QJsonObject{{QStringLiteral("overwrite"), true}},
                    [this, description](const QJsonObject& data,
                                        const QString& error) {
                      if (!error.isEmpty()) {
                        status_->setText(error);
                        return;
                      }
                      const bool ok =
                          data.value(QStringLiteral("ok")).toBool();
                      const QString reason =
                          data.value(QStringLiteral("reason")).toString();
                      if (ok) {
                        description->setText(
                            data.value(QStringLiteral("description"))
                                .toString());
                        status_->setText(QStringLiteral("Description generated."));
                      } else {
                        status_->setText(reason.isEmpty()
                                             ? QStringLiteral("Describer did not run.")
                                             : reason);
                      }
                    });
              });
      edit_row->addWidget(auto_button);
      row_layout->addLayout(edit_row);
      layout->addWidget(row);
    }
    layout->addStretch(1);
    scroll->setWidget(host);
    outer->addWidget(scroll, 1);

    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("kanbanMuted"));
    status_->setWordWrap(true);
    outer->addWidget(status_);

    auto* buttons = new QDialogButtonBox(this);
    auto* cancel = buttons->addButton(QStringLiteral("Close"),
                                      QDialogButtonBox::RejectRole);
    cancel->setObjectName(QStringLiteral("kanbanSecondary"));
    auto* save = buttons->addButton(QStringLiteral("Save knobs"),
                                    QDialogButtonBox::AcceptRole);
    save->setObjectName(QStringLiteral("kanbanPrimary"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, [this]() {
      status_->setText(QStringLiteral("Saving…"));
      view_->Request(
          QStringLiteral("PUT"), QStringLiteral("/orchestration"),
          QJsonObject{{QStringLiteral("orchestrator_profile"),
                       orchestrator_->currentData().toString()},
                      {QStringLiteral("default_assignee"),
                       default_assignee_->currentData().toString()},
                      {QStringLiteral("auto_decompose"),
                       auto_decompose_->isChecked()},
                      {QStringLiteral("auto_promote_children"),
                       auto_promote_->isChecked()}},
          [this](const QJsonObject& data, const QString& error) {
            if (!error.isEmpty()) {
              status_->setText(error);
              return;
            }
            view_->orchestration_ = kanban::ParseOrchestration(data);
            view_->UpdateHeader();
            status_->setText(QStringLiteral("Orchestration saved."));
          });
    });
    outer->addWidget(buttons);
  }

  static QLabel* SectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("kanbanSection"));
    return label;
  }

  void ApplyStyle() {
    setStyleSheet(
        QStringLiteral(
            "QDialog#kanbanOrchestrationDialog { background: %1; color: %2; }"
            "QLabel#kanbanDialogTitle { color: %2; font-size: 18px; "
            "font-weight: 600; }"
            "QLabel#kanbanSection { color: %3; font-size: 9px; "
            "font-weight: 700; letter-spacing: 0.8px; }"
            "QLabel#kanbanMuted { color: %3; font-size: 10px; }"
            "QLineEdit, QComboBox { background: %4; color: %2; "
            "border: 1px solid %5; border-radius: 7px; padding: 5px 7px; }"
            "QPushButton#kanbanPrimary { background: %6; color: %7; "
            "border: none; border-radius: 7px; padding: 7px 14px; "
            "font-weight: 600; }"
            "QPushButton#kanbanSecondary { background: transparent; color: %3; "
            "border: 1px solid %5; border-radius: 7px; padding: 5px 10px; }")
            .arg(theme_.background.name(), theme_.foreground.name(),
                 theme_.text_secondary.name(), theme_.surface.name(),
                 theme_.stroke.name(), theme_.accent.name(),
                 theme_.accent_text.name()));
    update();
  }

  KanbanView* view_ = nullptr;
  Theme theme_;
  QComboBox* orchestrator_ = nullptr;
  QComboBox* default_assignee_ = nullptr;
  QCheckBox* auto_decompose_ = nullptr;
  QCheckBox* auto_promote_ = nullptr;
  QLabel* status_ = nullptr;
};


// ---------------------------------------------------------------------------
// KanbanView
// ---------------------------------------------------------------------------

KanbanView::KanbanView(Theme theme, QWidget* parent)
    : QWidget(parent), theme_(std::move(theme)) {
  setObjectName(QStringLiteral("kanbanView"));
  BuildUi();
  ApplyStyle();

  poll_timer_ = new QTimer(this);
  poll_timer_->setInterval(4000);
  connect(poll_timer_, &QTimer::timeout, this, [this]() {
    if (!isVisible()) {
      return;
    }
    ++poll_tick_;
    FetchBoard();
    if (poll_tick_ % 3 == 0) {
      FetchBoards();
    }
    if (poll_tick_ % 15 == 0) {
      FetchRoster();
      FetchOrchestration();
      FetchProjects();
    }
  });
}

void KanbanView::BuildUi() {
  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(14, 10, 14, 8);
  outer->setSpacing(8);

  auto* header = new QWidget(this);
  header->setObjectName(QStringLiteral("kanbanHeader"));
  auto* header_row = new QHBoxLayout(header);
  header_row->setContentsMargins(0, 0, 0, 0);
  header_row->setSpacing(7);

  count_ = new QLabel(header);
  count_->setObjectName(QStringLiteral("kanbanCount"));
  count_->setMinimumWidth(110);
  header_row->addWidget(count_);

  board_combo_ = new QComboBox(header);
  board_combo_->setObjectName(QStringLiteral("kanbanBoardCombo"));
  board_combo_->setMinimumWidth(170);
  board_combo_->setMaximumWidth(250);
  board_combo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  connect(board_combo_, &QComboBox::activated, this, [this](int index) {
    const QString slug = board_combo_->itemData(index).toString();
    if (!slug.isEmpty()) {
      SelectBoard(slug, true);
    }
  });
  header_row->addWidget(board_combo_);

  board_menu_ = new QToolButton(header);
  board_menu_->setObjectName(QStringLiteral("kanbanIconButton"));
  board_menu_->setText(QStringLiteral("▾"));
  board_menu_->setToolTip(QStringLiteral("Boards"));
  connect(board_menu_, &QToolButton::clicked, this, [this]() {
    ShowBoardMenu();
  });
  header_row->addWidget(board_menu_);

  filter_button_ = new QToolButton(header);
  filter_button_->setObjectName(QStringLiteral("kanbanIconButton"));
  filter_button_->setText(QStringLiteral("Filter"));
  filter_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  filter_button_->setToolTip(QStringLiteral("Filter lanes and tenants"));
  connect(filter_button_, &QToolButton::clicked, this, [this]() {
    ShowFilterMenu();
  });
  header_row->addWidget(filter_button_);

  search_ = new QLineEdit(header);
  search_->setObjectName(QStringLiteral("kanbanSearch"));
  search_->setPlaceholderText(QStringLiteral("Search cards…"));
  search_->setClearButtonEnabled(true);
  search_->setFixedWidth(190);
  connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
    if (canvas_ != nullptr) {
      canvas_->SetSearch(text);
    }
  });
  header_row->addWidget(search_);

  orchestration_button_ = new QToolButton(header);
  orchestration_button_->setObjectName(QStringLiteral("kanbanIconButton"));
  orchestration_button_->setText(QStringLiteral("Orchestration"));
  orchestration_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
  orchestration_button_->setToolTip(
      QStringLiteral("Orchestrator profile, default assignee, routing"));
  connect(orchestration_button_, &QToolButton::clicked, this,
          [this]() { ShowOrchestration(); });
  header_row->addWidget(orchestration_button_);
  header_row->addStretch(1);

  new_task_ = new QPushButton(QStringLiteral("New task"), header);
  new_task_->setObjectName(QStringLiteral("kanbanPrimary"));
  connect(new_task_, &QPushButton::clicked, this,
          [this]() { ShowNewTask(QStringLiteral("triage")); });
  header_row->addWidget(new_task_);
  outer->addWidget(header);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("kanbanStatus"));
  status_->setWordWrap(true);
  status_->hide();
  outer->addWidget(status_);

  canvas_ = new KanbanBoardCanvas(theme_, this);
  canvas_->on_open = [this](const QString& id) { OpenTask(id); };
  canvas_->on_add = [this](const QString& column) {
    AddTaskToColumn(column);
  };
  canvas_->on_move = [this](const QStringList& ids, const QString& column) {
    BulkMove(ids, column);
  };
  canvas_->on_delete = [this](const QString& id) { DeleteTask(id); };
  canvas_->on_bulk_assign = [this](const QStringList& ids,
                                   const QString& assignee) {
    BulkAssign(ids, assignee);
  };
  canvas_->on_bulk_priority = [this](const QStringList& ids, int priority) {
    BulkPriority(ids, priority);
  };
  canvas_->on_bulk_archive = [this](const QStringList& ids) {
    BulkArchive(ids);
  };
  canvas_->on_bulk_delete = [this](const QStringList& ids) {
    BulkDelete(ids);
  };
  canvas_->on_content_width = [this](int content, int viewport) {
    if (horizontal_ == nullptr) {
      return;
    }
    const int maximum = std::max(0, content - viewport);
    if (horizontal_->maximum() != maximum) {
      horizontal_->setRange(0, maximum);
      horizontal_->setPageStep(std::max(1, viewport));
      horizontal_->setSingleStep(48);
    }
    horizontal_->setVisible(maximum > 0);
  };
  canvas_->on_horizontal_delta = [this](int delta) {
    if (horizontal_ != nullptr) {
      horizontal_->setValue(horizontal_->value() + delta);
    }
  };
  outer->addWidget(canvas_, 1);

  horizontal_ = new QScrollBar(Qt::Horizontal, this);
  horizontal_->setObjectName(QStringLiteral("kanbanHScroll"));
  horizontal_->hide();
  connect(horizontal_, &QScrollBar::valueChanged, this, [this](int value) {
    if (canvas_ != nullptr) {
      canvas_->SetHorizontalOffset(value);
    }
  });
  outer->addWidget(horizontal_);
}

void KanbanView::ApplyStyle() {
  setStyleSheet(
      QStringLiteral(
          "QWidget#kanbanView { background: %1; color: %2; }"
          "QWidget#kanbanHeader { background: transparent; }"
          "QLabel#kanbanCount { color: %2; font-size: 12px; "
          "font-weight: 600; }"
          "QLabel#kanbanStatus { color: %3; font-size: 10px; }"
          "QComboBox#kanbanBoardCombo, QLineEdit#kanbanSearch { "
          "background: %4; color: %2; border: 1px solid %5; "
          "border-radius: 7px; padding: 4px 8px; }"
          "QLineEdit#kanbanSearch { padding: 3px 8px; }"
          "QToolButton#kanbanIconButton { background: transparent; "
          "color: %3; border: 1px solid %5; border-radius: 7px; "
          "padding: 4px 9px; }"
          "QToolButton#kanbanIconButton:hover { color: %2; background: %6; }"
          "QPushButton#kanbanPrimary { background: %7; color: %8; "
          "border: none; border-radius: 7px; padding: 6px 13px; "
          "font-weight: 600; }"
          "QPushButton#kanbanPrimary:hover { background: %2; }")
          .arg(theme_.background.name(), theme_.foreground.name(),
               theme_.text_secondary.name(), theme_.surface.name(),
               theme_.stroke.name(), theme_.hover.name(),
               theme_.accent.name(), theme_.accent_text.name()));
  if (canvas_ != nullptr) {
    canvas_->SetTheme(theme_);
  }
  update();
}

void KanbanView::SetTheme(const Theme& theme) {
  theme_ = theme;
  ApplyStyle();
  if (task_dialog_ != nullptr) {
    task_dialog_->SetTheme(theme);
  }
}

void KanbanView::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  if (poll_timer_ != nullptr) {
    poll_timer_->start();
  }
  if (selected_board_.isEmpty()) {
    Activate();
  } else {
    Refresh();
  }
}

void KanbanView::hideEvent(QHideEvent* event) {
  QWidget::hideEvent(event);
  if (poll_timer_ != nullptr) {
    poll_timer_->stop();
  }
}

void KanbanView::Activate() {
  const QString pref =
      get_pref ? get_pref(QStringLiteral("kanban.board")) : QString();
  if (!pref.isEmpty()) {
    selected_board_ = pref;
  }
  FetchBoards();
  FetchBoard();
  FetchRoster();
  FetchProjects();
  FetchOrchestration();
  if (poll_timer_ != nullptr) {
    poll_timer_->start();
  }
}

void KanbanView::NewTask() { ShowNewTask(QStringLiteral("triage")); }

void KanbanView::Refresh() {
  FetchBoard();
  if (profiles_.isEmpty()) {
    FetchRoster();
  }
  if (projects_.isEmpty()) {
    FetchProjects();
  }
  if (orchestration_.active_profile.isEmpty()) {
    FetchOrchestration();
  }
}

QString KanbanView::BoardPath(const QString& path) const {
  const bool scoped =
      path == QStringLiteral("/board") || path.startsWith(QStringLiteral("/board?")) ||
      path.startsWith(QStringLiteral("/tasks")) ||
      path.startsWith(QStringLiteral("/links")) ||
      path.startsWith(QStringLiteral("/attachments")) ||
      path.startsWith(QStringLiteral("/dispatch"));
  if (!scoped || selected_board_.isEmpty()) {
    return path;
  }
  return path + (path.contains('?') ? QStringLiteral("&")
                                    : QStringLiteral("?")) +
         QStringLiteral("board=") + selected_board_;
}

void KanbanView::Request(const QString& method, const QString& path,
                         const QJsonObject& body, JsonCallback done) {
  if (!on_request) {
    if (done) {
      done(QJsonObject(), QStringLiteral("Kanban is not connected."));
    }
    return;
  }
  on_request(method, BoardPath(path), body,
             [this, done](const QJsonObject& data, const QString& error) {
               if (!error.isEmpty()) {
                 ShowError(error);
               }
               if (done) {
                 done(data, error);
               }
             });
}

void KanbanView::FetchBoards() {
  if (boards_pending_) {
    return;
  }
  boards_pending_ = true;
  Request(QStringLiteral("GET"), QStringLiteral("/boards"), {},
          [this](const QJsonObject& data, const QString& error) {
            boards_pending_ = false;
            if (!error.isEmpty()) {
              return;
            }
            boards_ = kanban::ParseBoards(data);
            const QString current =
                data.value(QStringLiteral("current")).toString();
            bool selected_exists = !selected_board_.isEmpty();
            if (selected_exists) {
              selected_exists = false;
              for (const kanban::KanbanBoardMeta& meta : boards_) {
                if (meta.slug == selected_board_) {
                  selected_exists = true;
                  break;
                }
              }
            }
            bool changed_board = false;
            if (!selected_exists) {
              selected_board_.clear();
              const QString pref =
                  get_pref ? get_pref(QStringLiteral("kanban.board"))
                           : QString();
              for (const kanban::KanbanBoardMeta& meta : boards_) {
                if (!pref.isEmpty() && meta.slug == pref) {
                  selected_board_ = meta.slug;
                  break;
                }
              }
              if (selected_board_.isEmpty()) {
                for (const kanban::KanbanBoardMeta& meta : boards_) {
                  if (meta.slug == current || meta.is_current) {
                    selected_board_ = meta.slug;
                    break;
                  }
                }
              }
              if (selected_board_.isEmpty() && !boards_.isEmpty()) {
                selected_board_ = boards_.front().slug;
              }
              changed_board = !selected_board_.isEmpty();
            }
            QStringList signature_parts;
            for (const kanban::KanbanBoardMeta& meta : boards_) {
              signature_parts << meta.slug << meta.name
                              << QString::number(meta.total)
                              << (meta.archived ? QStringLiteral("1")
                                                : QStringLiteral("0"));
            }
            const QString signature = signature_parts.join(QLatin1Char('\x1f'));
            if (signature != boards_signature_) {
              boards_signature_ = signature;
              board_combo_->clear();
              for (const kanban::KanbanBoardMeta& meta : boards_) {
                board_combo_->addItem(
                    meta.total > 0
                        ? QStringLiteral("%1  (%2)")
                              .arg(meta.name)
                              .arg(meta.total)
                        : meta.name,
                    meta.slug);
              }
            }
            const int index = board_combo_->findData(selected_board_);
            if (index >= 0 && board_combo_->currentIndex() != index) {
              board_combo_->setCurrentIndex(index);
            }
            if (changed_board) {
              SelectBoard(selected_board_, false);
            } else if (board_.tasks.isEmpty() && board_.columns.isEmpty() &&
                       !selected_board_.isEmpty()) {
              FetchBoard();
            } else {
              meta_ = CurrentMeta();
              UpdateHeader();
            }
          });
}

void KanbanView::FetchBoard() {
  if (selected_board_.isEmpty()) {
    return;
  }
  // A switch can happen while the previous board is still in flight. Let the
  // new board start immediately and drop the old reply when it lands.
  if (board_pending_ && board_request_slug_ == selected_board_) {
    return;
  }
  const int request_id = ++board_request_id_;
  const QString slug = selected_board_;
  board_pending_ = true;
  board_request_slug_ = slug;
  Request(
      QStringLiteral("GET"),
      QStringLiteral("/board?include_archived=1"),
      {}, [this, request_id, slug](const QJsonObject& data,
                                   const QString& error) {
        if (request_id != board_request_id_) {
          return;
        }
        board_pending_ = false;
        if (!error.isEmpty() || selected_board_ != slug) {
          return;
        }
        kanban::KanbanBoard board = kanban::ParseBoard(data);
        if (board.board.isEmpty()) {
          board.board = slug;
        }
        SetBoard(board, CurrentMeta());
      });
}

void KanbanView::FetchRoster() {
  if (roster_pending_) {
    return;
  }
  roster_pending_ = true;
  Request(QStringLiteral("GET"), QStringLiteral("/profiles"), {},
          [this](const QJsonObject& data, const QString& error) {
            roster_pending_ = false;
            if (!error.isEmpty()) {
              return;
            }
            profiles_ = kanban::ParseProfiles(data);
            QStringList assignees = board_.assignees;
            for (const kanban::KanbanProfile& profile : profiles_) {
              if (!assignees.contains(profile.name)) {
                assignees.push_back(profile.name);
              }
            }
            canvas_->SetProfiles(assignees);
          });
}

void KanbanView::FetchProjects() {
  if (projects_pending_) {
    return;
  }
  projects_pending_ = true;
  Request(QStringLiteral("GET"), QStringLiteral("/projects"), {},
          [this](const QJsonObject& data, const QString& error) {
            projects_pending_ = false;
            if (!error.isEmpty()) {
              return;
            }
            projects_ = kanban::ParseProjects(data);
          });
}

void KanbanView::FetchOrchestration() {
  if (orchestration_pending_) {
    return;
  }
  orchestration_pending_ = true;
  Request(QStringLiteral("GET"), QStringLiteral("/orchestration"), {},
          [this](const QJsonObject& data, const QString& error) {
            orchestration_pending_ = false;
            if (!error.isEmpty()) {
              return;
            }
            orchestration_ = kanban::ParseOrchestration(data);
            canvas_->SetDefaultAssignee(
                orchestration_.resolved_default_assignee);
          });
}

void KanbanView::SelectBoard(const QString& slug, bool persist) {
  if (slug.isEmpty()) {
    return;
  }
  selected_board_ = slug;
  if (persist && set_pref) {
    set_pref(QStringLiteral("kanban.board"), slug);
  }
  board_ = kanban::KanbanBoard();
  meta_ = CurrentMeta();
  canvas_->SetBoard(board_, meta_, true);
  horizontal_->setValue(0);
  const int index = board_combo_->findData(slug);
  if (index >= 0) {
    board_combo_->setCurrentIndex(index);
  }
  UpdateHeader();
  FetchBoard();
}

void KanbanView::SetBoard(const kanban::KanbanBoard& board,
                          const kanban::KanbanBoardMeta& meta) {
  board_ = board;
  meta_ = meta;
  if (board_.board.isEmpty()) {
    board_.board = selected_board_;
  }
  board_.columns = kanban::OrderedColumns(board_.columns);
  canvas_->SetBoard(board_, meta_);
  QStringList assignees = board_.assignees;
  for (const kanban::KanbanProfile& profile : profiles_) {
    if (!assignees.contains(profile.name)) {
      assignees.push_back(profile.name);
    }
  }
  canvas_->SetProfiles(assignees);
  canvas_->SetDefaultAssignee(orchestration_.resolved_default_assignee);
  const QString search = search_ != nullptr ? search_->text() : QString();
  canvas_->SetSearch(search);
  canvas_->SetFilter(canvas_->AssigneeFilter(), canvas_->TenantFilter(),
                     canvas_->ShowsArchived());
  UpdateHeader();
}

void KanbanView::ShowTask(const QString& task_id) {
  if (task_id.isEmpty()) {
    return;
  }
  if (task_dialog_ == nullptr) {
    task_dialog_ = new KanbanTaskDialog(this, theme_);
    connect(task_dialog_, &QDialog::finished, this, [this]() {
      if (task_dialog_ != nullptr) {
        task_dialog_->deleteLater();
        task_dialog_ = nullptr;
      }
      task_dialog_id_.clear();
    });
  }
  task_dialog_id_ = task_id;
  task_dialog_->SetStatus(QStringLiteral("Loading task…"));
  if (!task_dialog_->isVisible()) {
    task_dialog_->show();
    task_dialog_->raise();
    task_dialog_->activateWindow();
  }
  Request(
      QStringLiteral("GET"),
      QStringLiteral("/tasks/%1").arg(task_id), {},
      [this, task_id](const QJsonObject& data, const QString& error) {
        if (task_dialog_ == nullptr || task_dialog_id_ != task_id) {
          return;
        }
        if (!error.isEmpty()) {
          task_dialog_->SetDetailError(error);
          return;
        }
        kanban::KanbanTaskDetail detail = kanban::ParseTaskDetail(data);
        if (detail.task.id.isEmpty()) {
          detail.task.id = task_id;
        }
        QStringList assignees;
        for (const kanban::KanbanProfile& profile : profiles_) {
          assignees.push_back(profile.name);
        }
        for (const QString& name : board_.assignees) {
          if (!assignees.contains(name)) {
            assignees.push_back(name);
          }
        }
        task_dialog_->SetDetail(detail, board_.tasks, board_.columns, assignees,
                                orchestration_.resolved_default_assignee);
      });
}

void KanbanView::ShowNewTask(const QString& column) {
  auto* dialog = new KanbanNewTaskDialog(this, theme_, column, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void KanbanView::CreateTask(const kanban::KanbanNewTask& task) {
  const QString requested_status = task.status;
  Request(QStringLiteral("POST"), QStringLiteral("/tasks"),
          kanban::BuildCreatePayload(task),
          [this, requested_status](const QJsonObject& data,
                                   const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            QString id = data.value(QStringLiteral("id")).toString();
            if (id.isEmpty()) {
              id = data.value(QStringLiteral("task_id")).toString();
            }
            if (id.isEmpty()) {
              id = data.value(QStringLiteral("task"))
                       .toObject()
                       .value(QStringLiteral("id"))
                       .toString();
            }
            ShowCreated(id, requested_status);
          });
}

void KanbanView::ShowCreated(const QString& task_id,
                             const QString& requested_status) {
  FetchBoard();
  FetchBoards();
  if (!requested_status.isEmpty() &&
      requested_status != QStringLiteral("triage") &&
      requested_status != QStringLiteral("todo") &&
      !task_id.isEmpty() && !kanban::LockedTarget(requested_status)) {
    PatchTask(task_id, kanban::BuildMovePayload(requested_status),
              QStringLiteral("Created in %1")
                  .arg(kanban::ColumnLabel(requested_status)));
  } else {
    ShowStatus(QStringLiteral("Task created."));
  }
}

void KanbanView::ShowBoardMenu() {
  QMenu menu(this);
  for (const kanban::KanbanBoardMeta& meta : boards_) {
    auto* action = menu.addAction(
        meta.archived ? QStringLiteral("%1 (archived)").arg(meta.name)
                      : meta.name);
    action->setCheckable(true);
    action->setChecked(meta.slug == selected_board_);
    connect(action, &QAction::triggered, this,
            [this, slug = meta.slug]() { SelectBoard(slug, true); });
  }
  if (!boards_.isEmpty()) {
    menu.addSeparator();
  }
  menu.addAction(QStringLiteral("New board…"), this,
                 [this]() { ShowBoardSettings(true); });
  menu.addAction(QStringLiteral("Board settings…"), this,
                 [this]() { ShowBoardSettings(false); });
  menu.addAction(QStringLiteral("Orchestration…"), this,
                 [this]() { ShowOrchestration(); });
  menu.exec(QCursor::pos());
}

void KanbanView::ShowBoardSettings(bool create) {
  if (!create && selected_board_.isEmpty()) {
    ShowStatus(QStringLiteral("Select a board first."));
    return;
  }
  FetchProjects();
  auto* dialog =
      new KanbanBoardSettingsDialog(this, theme_, create, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void KanbanView::ShowOrchestration() {
  FetchRoster();
  auto* dialog = new KanbanOrchestrationDialog(this, theme_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void KanbanView::ShowFilterMenu() {
  QMenu menu(this);
  QStringList assignees = board_.assignees;
  for (const kanban::KanbanProfile& profile : profiles_) {
    if (!assignees.contains(profile.name)) {
      assignees.push_back(profile.name);
    }
  }
  auto* all = menu.addAction(QStringLiteral("All profiles"));
  all->setCheckable(true);
  all->setChecked(canvas_->AssigneeFilter().isEmpty());
  connect(all, &QAction::triggered, this, [this]() {
    canvas_->SetFilter(QString(), canvas_->TenantFilter(),
                       canvas_->ShowsArchived());
    filter_button_->setText(QStringLiteral("Filter"));
  });
  QMenu* by_profile = menu.addMenu(QStringLiteral("By profile"));
  for (const QString& name : assignees) {
    auto* action = by_profile->addAction(name);
    action->setCheckable(true);
    action->setChecked(canvas_->AssigneeFilter() == name);
    connect(action, &QAction::triggered, this, [this, name]() {
      canvas_->SetFilter(name, canvas_->TenantFilter(),
                         canvas_->ShowsArchived());
      filter_button_->setText(name);
    });
  }
  QMenu* by_tenant = menu.addMenu(QStringLiteral("Tenant"));
  auto* any_tenant = by_tenant->addAction(QStringLiteral("Any tenant"));
  any_tenant->setCheckable(true);
  any_tenant->setChecked(canvas_->TenantFilter().isEmpty());
  connect(any_tenant, &QAction::triggered, this, [this]() {
    canvas_->SetFilter(canvas_->AssigneeFilter(), QString(),
                       canvas_->ShowsArchived());
  });
  for (const QString& tenant : board_.tenants) {
    auto* action = by_tenant->addAction(tenant);
    action->setCheckable(true);
    action->setChecked(canvas_->TenantFilter() == tenant);
    connect(action, &QAction::triggered, this, [this, tenant]() {
      canvas_->SetFilter(canvas_->AssigneeFilter(), tenant,
                         canvas_->ShowsArchived());
    });
  }
  menu.addSeparator();
  auto* archived = menu.addAction(QStringLiteral("Show archived cards"));
  archived->setCheckable(true);
  archived->setChecked(canvas_->ShowsArchived());
  connect(archived, &QAction::triggered, this, [this](bool checked) {
    canvas_->SetFilter(canvas_->AssigneeFilter(), canvas_->TenantFilter(),
                       checked);
  });
  menu.exec(QCursor::pos());
}

void KanbanView::PatchTask(const QString& task_id, const QJsonObject& body,
                           const QString& success) {
  if (task_id.isEmpty()) {
    return;
  }
  Request(QStringLiteral("PATCH"),
          QStringLiteral("/tasks/%1").arg(task_id), body,
          [this, task_id, success](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            ShowStatus(success);
            FetchBoard();
            if (task_dialog_ != nullptr && task_dialog_id_ == task_id) {
              ShowTask(task_id);
            }
          });
}

void KanbanView::DeleteTask(const QString& task_id) {
  if (task_id.isEmpty()) {
    return;
  }
  Request(QStringLiteral("DELETE"),
          QStringLiteral("/tasks/%1").arg(task_id), {},
          [this, task_id](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            if (task_dialog_ != nullptr && task_dialog_id_ == task_id) {
              task_dialog_->close();
            }
            ShowStatus(QStringLiteral("Task deleted."));
            FetchBoard();
            FetchBoards();
          });
}

void KanbanView::BulkMove(const QStringList& ids, const QString& column) {
  if (ids.isEmpty() || column.isEmpty()) {
    return;
  }
  if (ids.size() == 1) {
    PatchTask(ids.front(), kanban::BuildMovePayload(column),
              QStringLiteral("Moved to %1").arg(kanban::ColumnLabel(column)));
    return;
  }
  QJsonArray array;
  for (const QString& id : ids) {
    array.push_back(id);
  }
  Request(QStringLiteral("POST"), QStringLiteral("/tasks/bulk"),
          QJsonObject{{QStringLiteral("ids"), array},
                      {QStringLiteral("status"), column}},
          [this, column, count = ids.size()](const QJsonObject&,
                                             const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            ShowStatus(QStringLiteral("Moved %1 cards to %2")
                           .arg(count)
                           .arg(kanban::ColumnLabel(column)));
            FetchBoard();
          });
}

void KanbanView::BulkAssign(const QStringList& ids,
                            const QString& assignee) {
  if (ids.isEmpty()) {
    return;
  }
  QJsonArray array;
  for (const QString& id : ids) {
    array.push_back(id);
  }
  // "" is meaningful to /tasks/bulk (unassign), so it must be present.
  Request(QStringLiteral("POST"), QStringLiteral("/tasks/bulk"),
          QJsonObject{{QStringLiteral("ids"), array},
                      {QStringLiteral("assignee"), assignee}},
          [this, assignee](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            ShowStatus(assignee.isEmpty()
                           ? QStringLiteral("Cards unassigned.")
                           : QStringLiteral("Assigned to %1.").arg(assignee));
            FetchBoard();
          });
}

void KanbanView::BulkPriority(const QStringList& ids, int priority) {
  if (ids.isEmpty()) {
    return;
  }
  QJsonArray array;
  for (const QString& id : ids) {
    array.push_back(id);
  }
  Request(QStringLiteral("POST"), QStringLiteral("/tasks/bulk"),
          QJsonObject{{QStringLiteral("ids"), array},
                      {QStringLiteral("priority"), priority}},
          [this, priority](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            ShowStatus(QStringLiteral("Priority set to %1.")
                           .arg(kanban::PriorityLabel(priority)));
            FetchBoard();
          });
}

void KanbanView::BulkArchive(const QStringList& ids) {
  if (ids.isEmpty()) {
    return;
  }
  QJsonArray array;
  for (const QString& id : ids) {
    array.push_back(id);
  }
  Request(QStringLiteral("POST"), QStringLiteral("/tasks/bulk"),
          QJsonObject{{QStringLiteral("ids"), array},
                      {QStringLiteral("archive"), true}},
          [this](const QJsonObject&, const QString& error) {
            if (!error.isEmpty()) {
              return;
            }
            canvas_->ClearSelection();
            ShowStatus(QStringLiteral("Cards archived."));
            FetchBoard();
          });
}

void KanbanView::BulkDelete(const QStringList& ids) {
  if (ids.isEmpty()) {
    return;
  }
  const auto answer = QMessageBox::question(
      this, QStringLiteral("Delete cards"),
      QStringLiteral("Delete %1 card(s) permanently? This cannot be undone.")
          .arg(ids.size()));
  if (answer != QMessageBox::Yes) {
    return;
  }
  for (const QString& id : ids) {
    Request(QStringLiteral("DELETE"), QStringLiteral("/tasks/%1").arg(id), {},
            [](const QJsonObject&, const QString&) {});
  }
  canvas_->ClearSelection();
  ShowStatus(QStringLiteral("Cards deleted."));
  FetchBoard();
  FetchBoards();
}

void KanbanView::MoveTask(const QString& task_id, const QString& column) {
  BulkMove(QStringList{task_id}, column);
}

void KanbanView::AddTaskToColumn(const QString& column) {
  ShowNewTask(column);
}

void KanbanView::OpenTask(const QString& task_id) { ShowTask(task_id); }

void KanbanView::ShowError(const QString& error) {
  error_text_ = error;
  ShowStatus(error);
  if (status_ != nullptr) {
    status_->setStyleSheet(
        QStringLiteral("color: %1; font-size: 10px;").arg(theme_.danger.name()));
  }
}

void KanbanView::ShowStatus(const QString& text) {
  error_text_.clear();
  if (status_ == nullptr) {
    return;
  }
  status_->setStyleSheet(
      QStringLiteral("color: %1; font-size: 10px;")
          .arg(theme_.text_secondary.name()));
  status_->setText(text);
  status_->setVisible(!text.isEmpty());
}

void KanbanView::SetBusy(bool busy) {
  if (busy) {
    ++busy_requests_;
  } else {
    busy_requests_ = std::max(0, busy_requests_ - 1);
  }
  if (new_task_ != nullptr) {
    new_task_->setEnabled(busy_requests_ == 0);
  }
}

void KanbanView::UpdateHeader() {
  if (count_ == nullptr) {
    return;
  }
  int running = 0;
  int live = 0;
  for (const kanban::KanbanTask& task : board_.tasks) {
    if (task.archived) {
      continue;
    }
    ++live;
    if (task.status == QStringLiteral("running")) {
      ++running;
    }
  }
  const QString name = meta_.name.isEmpty() ? selected_board_ : meta_.name;
  const QString text =
      running > 0
          ? QStringLiteral("%1 · %2 cards · %3 running")
                .arg(name)
                .arg(live)
                .arg(running)
          : QStringLiteral("%1 · %2 cards").arg(name).arg(live);
  if (count_->text() != text) {
    count_->setText(text);
  }
  if (board_combo_ != nullptr && !selected_board_.isEmpty()) {
    const int index = board_combo_->findData(selected_board_);
    if (index >= 0 && board_combo_->currentIndex() != index) {
      board_combo_->setCurrentIndex(index);
    }
  }
}

kanban::KanbanBoardMeta KanbanView::CurrentMeta() const {
  for (const kanban::KanbanBoardMeta& meta : boards_) {
    if (meta.slug == selected_board_) {
      return meta;
    }
  }
  kanban::KanbanBoardMeta meta;
  meta.slug = selected_board_;
  meta.name = selected_board_;
  return meta;
}

void KanbanView::SetBoardForTest(const kanban::KanbanBoard& board,
                                 const kanban::KanbanBoardMeta& meta) {
  selected_board_ = meta.slug;
  SetBoard(board, meta);
  resize(1200, 640);
  if (canvas_ != nullptr) {
    canvas_->resize(1180, 560);
    canvas_->LayoutForTest();
    canvas_->update();
  }
}

QVector<QRectF> KanbanView::TaskRectsForTest() const {
  return canvas_ != nullptr ? canvas_->TaskRects() : QVector<QRectF>();
}

QStringList KanbanView::VisibleColumnsForTest() const {
  return board_.columns;
}

int KanbanView::VisibleTaskCountForTest() const {
  return canvas_ != nullptr ? canvas_->VisibleTaskCount() : 0;
}

QStringList KanbanView::SelectionForTest() const {
  return canvas_ == nullptr ? QStringList() : canvas_->Selection().values();
}

QColor KanbanView::CanvasBackgroundForTest() const {
  return canvas_ == nullptr ? QColor() : canvas_->BackgroundForTest();
}

void KanbanView::SelectForTest(const QString& task_id) {
  if (canvas_ != nullptr) {
    canvas_->SelectOnly(task_id);
  }
}

}  // namespace hermes::ui
