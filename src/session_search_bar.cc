// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/session_search_bar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QTimer>
#include <QVBoxLayout>
#include <utility>

#include "src/search_completion.h"

namespace hermes::ui {

SessionSearchBar::SessionSearchBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("sessionSearchBar"));
  setAttribute(Qt::WA_StyledBackground, true);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 7, 24, 5);
  root->setSpacing(5);

  auto* row = new QHBoxLayout();
  row->setContentsMargins(0, 0, 0, 0);
  row->setSpacing(8);
  auto* icon = new QLabel(QStringLiteral("⌕"), this);
  icon->setObjectName(QStringLiteral("sessionSearchIcon"));
  row->addWidget(icon);
  input_ = new QLineEdit(this);
  input_->setObjectName(QStringLiteral("sessionSearchInput"));
  input_->setPlaceholderText(QStringLiteral("Search all sessions"));
  input_->setClearButtonEnabled(true);
  input_->installEventFilter(this);
  row->addWidget(input_, 1);
  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("sessionSearchStatus"));
  row->addWidget(status_);
  root->addLayout(row);

  results_ = new QListWidget(this);
  results_->setObjectName(QStringLiteral("sessionSearchResults"));
  results_->setWordWrap(true);
  results_->setUniformItemSizes(false);
  results_->setMaximumHeight(220);
  results_->hide();
  root->addWidget(results_);

  timer_ = new QTimer(this);
  timer_->setSingleShot(true);
  timer_->setInterval(250);
  QObject::connect(timer_, &QTimer::timeout, this, [this]() { RunSearch(); });
  QObject::connect(input_, &QLineEdit::textChanged, this,
                   [this]() { ScheduleSearch(); });
  QObject::connect(input_, &QLineEdit::returnPressed, this, [this]() {
    if (results_->count() > 0) {
      OpenResult(0);
    } else {
      RunSearch();
    }
  });
  QObject::connect(results_, &QListWidget::itemClicked, this,
                   [this](QListWidgetItem* item) {
                     if (item != nullptr) {
                       OpenResult(results_->row(item));
                     }
                   });
  QObject::connect(results_, &QListWidget::itemActivated, this,
                   [this](QListWidgetItem* item) {
                     if (item != nullptr) {
                       OpenResult(results_->row(item));
                     }
                   });

  AttachContextCompleter(input_, [this]() {
    return suggestions_provider_ ? suggestions_provider_() : QStringList();
  });
  SetTheme(theme_);
}

SessionSearchBar::~SessionSearchBar() = default;

void SessionSearchBar::SetTheme(const Theme& theme) {
  theme_ = theme;
  setStyleSheet(
      QStringLiteral(
          "QWidget#sessionSearchBar { background: %1; border-bottom: 1px "
          "solid %2; }"
          "QLabel#sessionSearchIcon { color: %5; }"
          "QLabel#sessionSearchStatus { color: %5; }"
          "QLineEdit#sessionSearchInput { background: %2; color: %3; "
          "border: 1px solid %2; border-radius: 6px; padding: 4px 8px; }"
          "QLineEdit#sessionSearchInput:focus { border: 1px solid %4; }"
          "QListWidget#sessionSearchResults { background: %6; color: %3; "
          "border: 1px solid %2; border-radius: 6px; padding: 3px; }"
          "QListWidget#sessionSearchResults::item { padding: 5px 6px; "
          "border-radius: 4px; }"
          "QListWidget#sessionSearchResults::item:hover, "
          "QListWidget#sessionSearchResults::item:selected { "
          "background: %7; }")
          .arg(theme.background.name(), theme.stroke.name(),
               theme.foreground.name(), theme.accent.name(),
               theme.text_secondary.name(), theme.surface.name(),
               theme.hover.name()));
  auto* icon = findChild<QLabel*>(QStringLiteral("sessionSearchIcon"));
  if (icon != nullptr) {
    icon->setFont(theme_.UiFont(14, QFont::DemiBold));
  }
  if (status_ != nullptr) {
    status_->setFont(theme_.UiFont(11, QFont::Normal));
  }
  if (input_ != nullptr) {
    input_->setFont(theme_.UiFont(13, QFont::Normal));
  }
  if (results_ != nullptr) {
    results_->setFont(theme_.UiFont(12, QFont::Normal));
  }
  update();
}

void SessionSearchBar::SetSearchHandler(
    std::function<void(const QString&,
                       std::function<void(const QJsonObject&, const QString&)>)>
        handler) {
  handler_ = std::move(handler);
}

void SessionSearchBar::SetSuggestionsProvider(
    std::function<QStringList()> provider) {
  suggestions_provider_ = std::move(provider);
}

void SessionSearchBar::FocusSearch() {
  show();
  input_->setFocus(Qt::ShortcutFocusReason);
  input_->selectAll();
  if (!input_->text().trimmed().isEmpty()) {
    ScheduleSearch();
  }
}

void SessionSearchBar::ClearSearch() {
  timer_->stop();
  ++generation_;
  input_->clear();
  status_->clear();
  ClearResults();
}

QString SessionSearchBar::Query() const { return input_->text().trimmed(); }

void SessionSearchBar::TriggerSearchForTest() { RunSearch(); }

bool SessionSearchBar::eventFilter(QObject* watched, QEvent* event) {
  if (watched == input_ && event->type() == QEvent::KeyPress) {
    auto* key_event = static_cast<QKeyEvent*>(event);
    if (key_event->key() == Qt::Key_Escape) {
      ClearSearch();
      return true;
    }
  }
  return QWidget::eventFilter(watched, event);
}

void SessionSearchBar::ScheduleSearch() {
  ++generation_;
  ClearResults();
  const QString query = Query();
  if (query.isEmpty()) {
    status_->clear();
    return;
  }
  status_->setText(QStringLiteral("Searching…"));
  timer_->start();
}

void SessionSearchBar::RunSearch() {
  const QString query = Query();
  if (query.isEmpty()) {
    ClearResults();
    status_->clear();
    return;
  }
  if (!handler_) {
    status_->setText(QStringLiteral("Session search is not available."));
    return;
  }
  const quint64 generation = generation_;
  QPointer<SessionSearchBar> alive(this);
  handler_(query, [alive, generation](const QJsonObject& result,
                                      const QString& error) {
    if (alive == nullptr || alive->generation_ != generation) {
      return;
    }
    if (!error.isEmpty()) {
      alive->ClearResults();
      alive->status_->setText(error);
      return;
    }
    alive->RenderResults(result);
  });
}

void SessionSearchBar::RenderResults(const QJsonObject& result) {
  ClearResults();
  const QJsonArray rows = result.value(QStringLiteral("results")).toArray();
  if (rows.isEmpty()) {
    status_->setText(QStringLiteral("No sessions match this search."));
    results_->hide();
    return;
  }
  int shown = 0;
  for (const QJsonValue& value : rows) {
    const QJsonObject entry = value.toObject();
    const QString session_id =
        entry.value(QStringLiteral("session_id"))
            .toString(entry.value(QStringLiteral("id")).toString());
    if (session_id.isEmpty()) {
      continue;
    }
    QString title = entry.value(QStringLiteral("title")).toString().trimmed();
    if (title.isEmpty()) {
      title = session_id;
    }
    QString snippet =
        entry.value(QStringLiteral("snippet")).toString().simplified();
    if (snippet.size() > 160) {
      snippet = snippet.left(157) + QStringLiteral("…");
    }
    QString text = title;
    if (!snippet.isEmpty()) {
      text += QStringLiteral("\n") + snippet;
    }
    const int count = entry.value(QStringLiteral("message_count")).toInt();
    if (count > 0) {
      text += QStringLiteral("\n%1 message(s)").arg(count);
    }
    auto* item = new QListWidgetItem(text, results_);
    item->setData(Qt::UserRole, session_id);
    item->setToolTip(session_id);
    ++shown;
  }
  if (shown == 0) {
    status_->setText(QStringLiteral("No sessions match this search."));
    results_->hide();
    return;
  }
  status_->setText(QStringLiteral("%1 result(s)").arg(shown));
  results_->show();
}

void SessionSearchBar::ClearResults() {
  results_->clear();
  results_->hide();
}

void SessionSearchBar::OpenResult(int row) {
  if (row < 0 || row >= results_->count()) {
    return;
  }
  const QString session_id = results_->item(row)->data(Qt::UserRole).toString();
  ClearResults();
  status_->clear();
  if (session_id.isEmpty()) {
    return;
  }
  if (on_session_open) {
    on_session_open(session_id);
  }
}

}  // namespace hermes::ui
