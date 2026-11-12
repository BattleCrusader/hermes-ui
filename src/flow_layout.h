// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_FLOW_LAYOUT_H_
#define HERMES_UI_FLOW_LAYOUT_H_

#include <QLayout>
#include <QLayoutItem>
#include <QList>
#include <QRect>
#include <QSize>
#include <QWidget>
#include <algorithm>

namespace hermes::ui {

// A wrapping row of widgets. The right rail can shrink to 260 px, so action
// rows must flow onto a second line rather than forcing horizontal overflow.
// A zero minimum width is important: it keeps the containing QScrollArea from
// inheriting the un-wrapped width of the buttons.
class FlowLayout : public QLayout {
 public:
  explicit FlowLayout(QWidget* parent = nullptr, int margin = 0,
                      int horizontal_spacing = 4, int vertical_spacing = 4)
      : QLayout(parent),
        horizontal_spacing_(horizontal_spacing),
        vertical_spacing_(vertical_spacing) {
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(horizontal_spacing);
  }

  explicit FlowLayout(int margin = 0, int horizontal_spacing = 4,
                      int vertical_spacing = 4)
      : horizontal_spacing_(horizontal_spacing),
        vertical_spacing_(vertical_spacing) {
    setContentsMargins(margin, margin, margin, margin);
    setSpacing(horizontal_spacing);
  }

  ~FlowLayout() override {
    while (QLayoutItem* item = takeAt(0)) {
      delete item;
    }
  }

  void addItem(QLayoutItem* item) override { items_.append(item); }

  int count() const override { return items_.size(); }

  QLayoutItem* itemAt(int index) const override {
    return index >= 0 && index < items_.size() ? items_.at(index) : nullptr;
  }

  QLayoutItem* takeAt(int index) override {
    if (index < 0 || index >= items_.size()) {
      return nullptr;
    }
    return items_.takeAt(index);
  }

  Qt::Orientations expandingDirections() const override { return {}; }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int width) const override {
    return doLayout(QRect(0, 0, width, 0), true);
  }

  QSize minimumSize() const override { return QSize(0, 0); }

  QSize sizeHint() const override {
    QSize hint;
    for (QLayoutItem* item : items_) {
      const QSize item_hint = ItemSize(item);
      hint.setWidth(hint.width() + item_hint.width() + horizontal_spacing_);
      hint.setHeight(std::max(hint.height(), item_hint.height()));
    }
    if (!items_.isEmpty()) {
      hint.rwidth() -= horizontal_spacing_;
    }
    const QMargins margins = contentsMargins();
    return hint + QSize(margins.left() + margins.right(),
                        margins.top() + margins.bottom());
  }

  void setGeometry(const QRect& rect) override {
    QLayout::setGeometry(rect);
    doLayout(rect, false);
  }

 private:
  // QWidgetItem::sizeHint() is the style hint, not the fixed/minimum size a
  // caller may have set. Clamp it so fixed-size buttons keep the height they
  // asked for instead of collapsing to their font height.
  static QSize ItemSize(QLayoutItem* item) {
    // QWidgetItem caches the style hint separately from the widget's own
    // fixed/minimum size, so ask the widget directly when there is one.
    if (QWidget* widget = item->widget()) {
      return item->sizeHint()
          .expandedTo(widget->minimumSize())
          .boundedTo(widget->maximumSize());
    }
    return item->sizeHint()
        .expandedTo(item->minimumSize())
        .boundedTo(item->maximumSize());
  }

  int doLayout(const QRect& rect, bool test_only) const {
    const QMargins margins = contentsMargins();
    const QRect effective = rect.adjusted(margins.left(), margins.top(),
                                          -margins.right(), -margins.bottom());
    int x = 0;
    int y = 0;
    int line_height = 0;
    for (QLayoutItem* item : items_) {
      if (item->isEmpty()) {
        continue;
      }
      const QSize hint = ItemSize(item);
      if (x > 0 && x + hint.width() > effective.width()) {
        x = 0;
        y += line_height + vertical_spacing_;
        line_height = 0;
      }
      if (!test_only) {
        const QRect target(effective.x() + x, effective.y() + y, hint.width(),
                           hint.height());
        // QWidgetItem clamps to a style-derived maximum even after the widget
        // was given a fixed size, which re-centres the widget and breaks the
        // row grid. Geometry is ultimately the widget's to own.
        if (QWidget* widget = item->widget()) {
          widget->setGeometry(target);
        } else {
          item->setGeometry(target);
        }
      }
      x += hint.width() + horizontal_spacing_;
      line_height = std::max(line_height, hint.height());
    }
    return y + line_height + margins.top() + margins.bottom();
  }

  QList<QLayoutItem*> items_;
  int horizontal_spacing_ = 4;
  int vertical_spacing_ = 4;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_FLOW_LAYOUT_H_
