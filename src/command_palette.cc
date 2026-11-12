// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/command_palette.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

#include "src/paint_utils.h"

namespace hermes::ui {
namespace {

constexpr int kCardWidth = 600;
constexpr int kCardHeight = 390;
constexpr int kRowHeight = 44;

// Two-line row: title on top, hint/group below. QListWidget's stock delegate
// cannot vary color or font per line, so the palette paints its own rows.
class CommandDelegate : public QStyledItemDelegate {
 public:
  explicit CommandDelegate(const Theme& theme, QObject* parent)
      : QStyledItemDelegate(parent), theme_(theme) {}

  void SetTheme(const Theme& theme) { theme_ = theme; }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if ((option.state & QStyle::State_Selected) != 0) {
      FillRounded(*painter, QRectF(option.rect).adjusted(2, 1, -2, -1), 6,
                  theme_.selected);
    }
    const QRect row = option.rect.adjusted(10, 3, -10, -3);
    painter->setFont(theme_.UiFont(12, QFont::DemiBold));
    painter->setPen(theme_.foreground);
    painter->drawText(row.adjusted(0, 0, 0, -16),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      index.data(Qt::DisplayRole).toString());
    painter->setFont(theme_.UiFont(10, QFont::Normal));
    painter->setPen(theme_.text_tertiary);
    painter->drawText(row.adjusted(0, 18, 0, 0),
                      Qt::AlignVCenter | Qt::AlignLeft,
                      index.data(Qt::UserRole + 1).toString());
    const QString group = index.data(Qt::UserRole + 2).toString();
    if (!group.isEmpty()) {
      painter->drawText(row, Qt::AlignVCenter | Qt::AlignRight, group);
    }
    painter->restore();
  }

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(0, kRowHeight);
  }

 private:
  Theme theme_;
};

}  // namespace

CommandPalette::CommandPalette(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("commandPalette"));
  setAttribute(Qt::WA_StyledBackground, false);
  setVisible(false);
  setFocusPolicy(Qt::StrongFocus);
  setAccessibleName(QStringLiteral("Command palette"));

  card_ = new QFrame(this);
  card_->setObjectName(QStringLiteral("paletteCard"));
  auto* card_layout = new QVBoxLayout(card_);
  card_layout->setContentsMargins(12, 12, 12, 10);
  card_layout->setSpacing(6);

  auto* search_row = new QWidget(card_);
  auto* search_layout = new QHBoxLayout(search_row);
  search_layout->setContentsMargins(4, 0, 4, 0);
  search_layout->setSpacing(8);
  auto* icon = new QLabel(search_row);
  icon->setFont(theme_.IconFont(14));
  icon->setText(QString(QChar(0xEA6D)));
  icon->setStyleSheet(
      QStringLiteral(
          "QLabel { color: %1; font-family: 'codicon'; font-size: 14px; }")
          .arg(theme_.text_tertiary.name()));
  search_layout->addWidget(icon);
  search_ = new QLineEdit(search_row);
  search_->setObjectName(QStringLiteral("paletteSearch"));
  search_->setFont(theme_.UiFont(13, QFont::Normal));
  search_->setPlaceholderText(
      QStringLiteral("Search commands, profiles and sessions"));
  search_->setClearButtonEnabled(false);
  search_->setFixedHeight(32);
  search_layout->addWidget(search_, 1);
  card_layout->addWidget(search_row);

  list_ = new QListWidget(card_);
  list_->setObjectName(QStringLiteral("paletteList"));
  list_->setFont(theme_.UiFont(12, QFont::Normal));
  list_->setFrameShape(QFrame::NoFrame);
  list_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list_->setSelectionMode(QAbstractItemView::SingleSelection);
  list_->setItemDelegate(new CommandDelegate(theme_, list_));
  list_->setFocusPolicy(Qt::NoFocus);
  card_layout->addWidget(list_, 1);

  empty_ = new QLabel(QStringLiteral("No matching command"), card_);
  empty_->setFont(theme_.UiFont(12, QFont::Normal));
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setStyleSheet(
      QStringLiteral("QLabel { color: %1; }").arg(theme_.text_tertiary.name()));
  empty_->setVisible(false);
  card_layout->addWidget(empty_);

  auto* footer =
      new QLabel(QStringLiteral("↑↓ navigate    ↵ run    esc close"), card_);
  footer->setObjectName(QStringLiteral("paletteFooter"));
  footer->setFont(theme_.UiFont(10, QFont::Normal));
  footer->setStyleSheet(
      QStringLiteral("QLabel { color: %1; }").arg(theme_.text_tertiary.name()));
  card_layout->addWidget(footer);

  search_->installEventFilter(this);
  list_->installEventFilter(this);
  connect(search_, &QLineEdit::textChanged, this,
          [this](const QString&) { Filter(); });
  connect(list_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem*) { RunCurrent(); });
  connect(list_, &QListWidget::itemActivated, this,
          [this](QListWidgetItem*) { RunCurrent(); });
  SetTheme(theme);
}

void CommandPalette::SetTheme(const Theme& theme) {
  theme_ = theme;
  card_->setStyleSheet(
      QStringLiteral(
          "QFrame#paletteCard { background: %1; border: 1px solid %2; "
          "border-radius: 12px; }")
          .arg(theme_.surface.name(), theme_.stroke_strong.name()));
  search_->setStyleSheet(
      QStringLiteral(
          "QLineEdit#paletteSearch { background: transparent; border: none; "
          "padding: 0; color: %1; selection-background-color: %2; }")
          .arg(theme_.foreground.name(), theme_.selected.name()));
  list_->setStyleSheet(
      QStringLiteral(
          "QListWidget#paletteList { background: transparent; border: none; "
          "outline: none; }"
          "QListWidget#paletteList QScrollBar:vertical { background: "
          "transparent; "
          "width: 10px; margin: 0; }"
          "QListWidget#paletteList QScrollBar::handle:vertical { background: "
          "%1; "
          "border-radius: 5px; min-height: 20px; }"
          "QListWidget#paletteList QScrollBar::add-line:vertical, "
          "QListWidget#paletteList QScrollBar::sub-line:vertical { height: 0; "
          "}")
          .arg(theme_.text_tertiary.name()));
  if (auto* delegate = dynamic_cast<CommandDelegate*>(list_->itemDelegate())) {
    delegate->SetTheme(theme_);
  }
  list_->viewport()->update();
  update();
}

void CommandPalette::SetCommands(std::vector<PaletteCommand> commands) {
  commands_ = std::move(commands);
  if (IsOpen()) {
    Filter();
  }
}

void CommandPalette::Open() {
  if (parentWidget() != nullptr) {
    setGeometry(parentWidget()->rect());
  }
  LayoutCard();
  show();
  raise();
  search_->clear();
  Filter();
  search_->setFocus(Qt::ShortcutFocusReason);
}

void CommandPalette::OpenWith(const QString& query) {
  Open();
  if (search_ != nullptr) {
    search_->setText(query);
    search_->setFocus();
    Filter();
  }
}

void CommandPalette::Close() {
  hide();
  if (parentWidget() != nullptr) {
    parentWidget()->setFocus(Qt::OtherFocusReason);
  }
}

void CommandPalette::LayoutCard() {
  const int width = std::min(kCardWidth, std::max(320, this->width() - 80));
  const int height = std::min(kCardHeight, std::max(220, this->height() - 80));
  card_->setGeometry((this->width() - width) / 2,
                     std::max(60, (this->height() - height) / 3), width,
                     height);
}

void CommandPalette::Filter() {
  list_->clear();
  const QString query = search_->text().trimmed().toLower();
  int added = 0;
  for (std::size_t index = 0; index < commands_.size(); ++index) {
    const PaletteCommand& command = commands_[index];
    const QString haystack = (command.title + QLatin1Char(' ') + command.hint +
                              QLatin1Char(' ') + command.group)
                                 .toLower();
    if (!query.isEmpty() && !haystack.contains(query)) {
      continue;
    }
    auto* item = new QListWidgetItem(command.title, list_);
    item->setData(Qt::UserRole, static_cast<int>(index));
    item->setData(Qt::UserRole + 1,
                  command.hint.isEmpty() ? command.group : command.hint);
    item->setData(Qt::UserRole + 2,
                  command.hint.isEmpty() ? QString() : command.group);
    item->setSizeHint(QSize(0, kRowHeight));
    ++added;
  }
  empty_->setVisible(added == 0);
  if (added > 0) {
    list_->setCurrentRow(0);
  }
}

void CommandPalette::MoveSelection(int delta) {
  if (list_->count() == 0) {
    return;
  }
  const int current = list_->currentRow();
  const int next = std::clamp(current + delta, 0, list_->count() - 1);
  list_->setCurrentRow(next);
}

void CommandPalette::RunCurrent() {
  if (!IsOpen()) {
    return;
  }
  QListWidgetItem* item = list_->currentItem();
  if (item == nullptr) {
    return;
  }
  const int index = item->data(Qt::UserRole).toInt();
  if (index < 0 || index >= static_cast<int>(commands_.size())) {
    return;
  }
  const PaletteCommand command = commands_[static_cast<std::size_t>(index)];
  Close();
  if (command.run) {
    command.run();
  }
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
  if (!IsOpen() || event->type() != QEvent::KeyPress) {
    return QWidget::eventFilter(watched, event);
  }
  auto* key_event = static_cast<QKeyEvent*>(event);
  switch (key_event->key()) {
    case Qt::Key_Down:
      MoveSelection(1);
      return true;
    case Qt::Key_Up:
      MoveSelection(-1);
      return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      RunCurrent();
      return true;
    case Qt::Key_Escape:
      Close();
      return true;
    default:
      break;
  }
  return QWidget::eventFilter(watched, event);
}

void CommandPalette::mousePressEvent(QMouseEvent* event) {
  if (!card_->geometry().contains(event->pos())) {
    Close();
    return;
  }
  QWidget::mousePressEvent(event);
}

void CommandPalette::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  LayoutCard();
}

void CommandPalette::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 112));
}

}  // namespace hermes::ui
