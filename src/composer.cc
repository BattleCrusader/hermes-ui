// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/composer.h"

#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFontMetricsF>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <vector>

#include "src/model_visibility.h"
#include "src/native_prefs.h"
#include "src/paint_utils.h"
#include "src/slash_commands.h"

namespace hermes::ui {
namespace {

constexpr int kSendSize = 26;
constexpr int kEditorHeight = 40;
constexpr int kComposerHeight = 116;
constexpr int kStatusLineHeight = 18;

QString CssColor(const QColor& color) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

}  // namespace

Composer::Composer(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("composer"));
  setFixedHeight(kComposerHeight);
  setMouseTracking(true);
  setAcceptDrops(true);
  editor_ = new QTextEdit(this);
  editor_->setFrameShape(QFrame::NoFrame);
  editor_->setAcceptRichText(false);
  editor_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editor_->setPlaceholderText(QStringLiteral("Ask Hermes..."));
  editor_->setAcceptDrops(false);
  editor_->installEventFilter(this);
  UpdateEditorStyle();

  model_button_ = new QPushButton(this);
  model_button_->setObjectName(QStringLiteral("modelPicker"));
  model_button_->setAccessibleName(QStringLiteral("Choose model"));
  model_button_->setFlat(true);
  model_button_->setCursor(Qt::PointingHandCursor);
  connect(model_button_, &QPushButton::clicked, this,
          [this]() { ShowModelMenu(); });

  reasoning_button_ = new QPushButton(this);
  reasoning_button_->setObjectName(QStringLiteral("reasoningPill"));
  reasoning_button_->setAccessibleName(QStringLiteral("Reasoning effort"));
  reasoning_button_->setFlat(true);
  reasoning_button_->setCursor(Qt::PointingHandCursor);
  connect(reasoning_button_, &QPushButton::clicked, this,
          [this]() { ShowReasoningMenu(); });
  model_button_->setText(model_label_);
  reasoning_button_->setText(reasoning_);
  UpdateChipStyles();
  slash_commands_ = SlashCommandCatalog();
  activity_timer_ = new QTimer(this);
  activity_timer_->setInterval(140);
  connect(activity_timer_, &QTimer::timeout, this, [this]() {
    ++activity_tick_;
    update();
  });

  // Completion popup: a tool window so it can float above the composer card
  // without stealing focus from the editor (typing keeps filtering it).
  slash_menu_ = new QListWidget(nullptr);
  slash_menu_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                              Qt::NoDropShadowWindowHint);
  slash_menu_->setObjectName(QStringLiteral("slashMenu"));
  slash_menu_->setAttribute(Qt::WA_ShowWithoutActivating);
  slash_menu_->setFocusPolicy(Qt::NoFocus);
  slash_menu_->setUniformItemSizes(true);
  slash_menu_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  slash_menu_->setMouseTracking(true);
  connect(slash_menu_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem*) { AcceptSlashCommand(); });
  mention_menu_ = new QListWidget(nullptr);
  mention_menu_->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                                Qt::NoDropShadowWindowHint);
  mention_menu_->setObjectName(QStringLiteral("mentionMenu"));
  mention_menu_->setAttribute(Qt::WA_ShowWithoutActivating);
  mention_menu_->setFocusPolicy(Qt::NoFocus);
  mention_menu_->setUniformItemSizes(true);
  mention_menu_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  connect(mention_menu_, &QListWidget::itemClicked, this,
          [this](QListWidgetItem*) { AcceptMention(); });
  connect(editor_, &QTextEdit::textChanged, this, [this]() {
    UpdateSlashMenu();
    UpdateMentionMenu();
  });
}

Composer::~Composer() {
  delete slash_menu_;
  slash_menu_ = nullptr;
  delete mention_menu_;
  mention_menu_ = nullptr;
}

void Composer::SetModelCatalog(const std::vector<ModelProvider>& providers,
                               const QString& provider, const QString& model) {
  providers_ = providers;
  provider_ = provider;
  if (!model.isEmpty()) {
    SetModelLabel(model);
  }
  update();
}

void Composer::SetHiddenModels(const QSet<QString>& hidden) {
  hidden_models_ = hidden;
  update();
}

QDialog* Composer::BuildModelVisibilityDialog() {
  auto* dialog = new QDialog(this);
  dialog->setObjectName(QStringLiteral("modelVisibilityDialog"));
  dialog->setWindowTitle(QStringLiteral("Visible models"));
  dialog->setMinimumWidth(380);
  // A QScrollArea viewport paints QPalette::Base, which defaults to the light
  // platform palette even when QDialog is dark. Set both the palette and the
  // stylesheet so a dark theme cannot produce light checkbox text on a white
  // scrolling surface.
  QPalette dialog_palette = dialog->palette();
  dialog_palette.setColor(QPalette::Window, theme_.surface);
  dialog_palette.setColor(QPalette::Base, theme_.surface);
  dialog_palette.setColor(QPalette::WindowText, theme_.foreground);
  dialog_palette.setColor(QPalette::Text, theme_.foreground);
  dialog->setPalette(dialog_palette);
  dialog->setStyleSheet(
      QStringLiteral(
          "QDialog#modelVisibilityDialog { background: %1; color: %2; }"
          "QScrollArea#modelVisibilityScroll { background: %1; border: none; }"
          "QScrollArea#modelVisibilityScroll > QWidget > QWidget { "
          "background: %1; }"
          "QWidget#modelVisibilityContent { background: %1; }"
          "QLabel { color: %2; }"
          "QLabel#modelVisibilityHint { color: %3; }"
          "QCheckBox { color: %2; spacing: 8px; }"
          "QCheckBox::indicator { width: 16px; height: 16px; "
          "border: 1px solid %5; border-radius: 4px; background: %4; }"
          "QCheckBox::indicator:hover { border-color: %7; }"
          "QCheckBox::indicator:checked { background: %7; border-color: %7; "
          "image: url(:/check.svg); }"
          "QCheckBox::indicator:indeterminate { background: %7; "
          "border-color: %7; image: url(:/dash.svg); }"
          "QPushButton { background: %4; color: %2; border: 1px solid %5; "
          "border-radius: 8px; padding: 6px 12px; }"
          "QPushButton:hover { background: %6; }"
          "QPushButton#modelVisibilitySave { background: %7; color: white; "
          "border-color: %7; }")
          .arg(theme_.surface.name(), theme_.foreground.name(),
               theme_.text_secondary.name(), theme_.surface_raised.name(),
               CssColor(theme_.stroke), CssColor(theme_.hover),
               theme_.accent.name()));
  auto* root = new QVBoxLayout(dialog);
  auto* hint = new QLabel(
      QStringLiteral("Unchecked models stay in the catalog but disappear from "
                     "the composer picker."),
      dialog);
  hint->setObjectName(QStringLiteral("modelVisibilityHint"));
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* scroll = new QScrollArea(dialog);
  scroll->setObjectName(QStringLiteral("modelVisibilityScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setMaximumHeight(360);
  QPalette scroll_palette = scroll->palette();
  scroll_palette.setColor(QPalette::Window, theme_.surface);
  scroll_palette.setColor(QPalette::Base, theme_.surface);
  scroll_palette.setColor(QPalette::WindowText, theme_.foreground);
  scroll_palette.setColor(QPalette::Text, theme_.foreground);
  scroll->setPalette(scroll_palette);
  scroll->viewport()->setPalette(scroll_palette);
  auto* content = new QWidget(scroll);
  content->setObjectName(QStringLiteral("modelVisibilityContent"));
  content->setAutoFillBackground(true);
  QPalette content_palette = content->palette();
  content_palette.setColor(QPalette::Window, theme_.surface);
  content_palette.setColor(QPalette::WindowText, theme_.foreground);
  content->setPalette(content_palette);
  auto* form = new QVBoxLayout(content);
  form->setContentsMargins(0, 0, 0, 0);
  form->setSpacing(4);
  QHash<QString, QCheckBox*> boxes;
  struct ProviderGroup {
    QCheckBox* header = nullptr;
    std::vector<QCheckBox*> boxes;
  };
  const auto sync_provider = [](const ProviderGroup& group) {
    int checked = 0;
    for (QCheckBox* box : group.boxes) {
      if (box->isChecked()) {
        ++checked;
      }
    }
    const Qt::CheckState state =
        checked == 0 ? Qt::Unchecked
        : checked == static_cast<int>(group.boxes.size())
            ? Qt::Checked
            : Qt::PartiallyChecked;
    if (group.header->checkState() != state) {
      group.header->setCheckState(state);
    }
  };
  std::vector<ProviderGroup> provider_groups;
  bool first_provider = true;
  for (const ModelProvider& provider : providers_) {
    if (!first_provider) {
      form->addSpacing(4);
    }
    first_provider = false;
    const QString provider_name =
        provider.name.isEmpty() ? provider.slug : provider.name;
    auto* provider_box = new QCheckBox(provider_name, content);
    provider_box->setObjectName(
        QStringLiteral("modelVisibilityProvider_%1").arg(provider.slug));
    provider_box->setFont(theme_.UiFont(12, QFont::DemiBold));
    provider_box->setTristate(true);
    provider_box->setToolTip(
        QStringLiteral("Select or clear all %1 models").arg(provider_name));
    provider_box->setAccessibleName(
        QStringLiteral("All %1 models").arg(provider_name));
    form->addWidget(provider_box);

    ProviderGroup group;
    group.header = provider_box;
    for (const QString& model : provider.models) {
      const QString key = ModelVisibilityKey(provider.slug, model);
      auto* box = new QCheckBox(model, content);
      box->setObjectName(
          QStringLiteral("modelVisibility_%1_%2").arg(provider.slug, model));
      box->setChecked(!hidden_models_.contains(key));
      boxes.insert(key, box);
      group.boxes.push_back(box);
      form->addWidget(box);
    }
    sync_provider(group);
    QObject::connect(
        provider_box, &QCheckBox::clicked, dialog, [group, sync_provider]() {
          const bool all_checked = std::all_of(
              group.boxes.begin(), group.boxes.end(),
              [](const QCheckBox* box) { return box->isChecked(); });
          for (QCheckBox* box : group.boxes) {
            box->setChecked(!all_checked);
          }
          sync_provider(group);
        });
    for (QCheckBox* box : group.boxes) {
      QObject::connect(box, &QCheckBox::toggled, dialog,
                       [group, sync_provider]() { sync_provider(group); });
    }
    provider_groups.push_back(std::move(group));
  }
  form->addStretch(1);
  scroll->setWidget(content);
  root->addWidget(scroll);

  auto* actions = new QHBoxLayout();
  auto* show_all = new QPushButton(QStringLiteral("Show all"), dialog);
  show_all->setObjectName(QStringLiteral("modelVisibilityAll"));
  actions->addWidget(show_all);
  actions->addStretch(1);
  auto* cancel = new QPushButton(QStringLiteral("Cancel"), dialog);
  cancel->setObjectName(QStringLiteral("modelVisibilityCancel"));
  actions->addWidget(cancel);
  auto* save = new QPushButton(QStringLiteral("Save"), dialog);
  save->setObjectName(QStringLiteral("modelVisibilitySave"));
  save->setDefault(true);
  actions->addWidget(save);
  root->addLayout(actions);

  QObject::connect(show_all, &QPushButton::clicked, dialog,
                   [boxes, provider_groups, sync_provider]() {
                     for (QCheckBox* box : boxes) {
                       box->setChecked(true);
                     }
                     for (const ProviderGroup& group : provider_groups) {
                       sync_provider(group);
                     }
                   });
  QObject::connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
  QObject::connect(
      save, &QPushButton::clicked, dialog, [this, dialog, boxes]() {
        QSet<QString> hidden;
        for (auto it = boxes.constBegin(); it != boxes.constEnd(); ++it) {
          if (!it.value()->isChecked()) {
            hidden.insert(it.key());
          }
        }
        SetHiddenModels(hidden);
        SetNativePref(QStringLiteral("native.model_visibility"),
                      FormatHiddenModels(hidden));
        dialog->accept();
      });
  return dialog;
}

QDialog* Composer::ShowModelVisibilityDialogForTest() {
  return BuildModelVisibilityDialog();
}

void Composer::ShowModelVisibilityDialog() {
  QDialog* dialog = BuildModelVisibilityDialog();
  dialog->exec();
  dialog->deleteLater();
}

namespace {

QString CompactTokens(qint64 value) {
  if (value >= 1000000) {
    return QStringLiteral("%1m").arg(value / 1000000.0, 0, 'f', 1);
  }
  if (value >= 1000) {
    const double thousands = value / 1000.0;
    return QStringLiteral("%1k").arg(thousands, 0, 'f',
                                     thousands < 10.0 ? 1 : 0);
  }
  return QString::number(value);
}

}  // namespace

void Composer::SetUsage(qint64 context_used, qint64 context_max) {
  if (context_used_ == context_used && context_max_ == context_max) {
    return;
  }
  context_used_ = context_used;
  context_max_ = context_max;
  update();
}

QString Composer::TokenUsageText() const {
  if (context_max_ <= 0) {
    return QString();
  }
  const int percent = static_cast<int>(std::min<qint64>(
      100, (context_used_ * 100 + context_max_ / 2) / context_max_));
  return QStringLiteral("%1 / %2 · %3%")
      .arg(CompactTokens(context_used_), CompactTokens(context_max_))
      .arg(percent);
}

void Composer::SetVoiceState(bool recording, const QString& partial,
                             const QString& status) {
  if (voice_recording_ == recording && voice_partial_ == partial &&
      voice_status_ == status) {
    return;
  }
  voice_recording_ = recording;
  voice_partial_ = partial;
  voice_status_ = status;
  update();
}

QString Composer::VoiceStatusText() const {
  if (!voice_partial_.trimmed().isEmpty()) {
    return voice_partial_.trimmed();
  }
  if (voice_recording_) {
    return QStringLiteral("Listening…");
  }
  return voice_status_;
}

void Composer::SetPinnedContextCount(int count) {
  const int next = std::max(0, count);
  if (pinned_context_count_ == next) {
    return;
  }
  pinned_context_count_ = next;
  update();
}

void Composer::SetHistory(const QStringList& history) {
  history_ = history;
  history_cursor_ = -1;
  history_draft_.clear();
}

void Composer::SetPathLister(
    std::function<void(const QString&, PathListCallback)> lister) {
  path_lister_ = std::move(lister);
  path_cache_.clear();
  path_pending_.clear();
  ++mention_generation_;
}

void Composer::SetMentionRoot(const QString& root) {
  if (mention_root_ == root) {
    return;
  }
  mention_root_ = root;
  path_cache_.clear();
  path_pending_.clear();
  ++mention_generation_;
}

bool Composer::MentionMenuVisible() const {
  return mention_menu_ != nullptr && mention_menu_->isVisible();
}

QStringList Composer::MentionMenuEntries() const {
  QStringList entries;
  if (mention_menu_ == nullptr) {
    return entries;
  }
  for (int i = 0; i < mention_menu_->count(); ++i) {
    entries.append(mention_menu_->item(i)->text());
  }
  return entries;
}

QString Composer::MentionDisplayPath(const QString& absolute) const {
  if (mention_root_.isEmpty()) {
    return absolute;
  }
  QString root = mention_root_;
  if (!root.endsWith(QLatin1Char('/'))) {
    root += QLatin1Char('/');
  }
  return absolute.startsWith(root) ? absolute.mid(root.size()) : absolute;
}

void Composer::UpdateMentionMenu() {
  if (mention_menu_ == nullptr || !path_lister_ || mention_root_.isEmpty()) {
    HideMentionMenu();
    return;
  }
  const QTextCursor cursor = editor_->textCursor();
  const QString before = Text().left(cursor.position());
  const int at = before.lastIndexOf(QLatin1Char('@'));
  if (at < 0) {
    HideMentionMenu();
    return;
  }
  if (at > 0) {
    const QChar previous = before.at(at - 1);
    if (!previous.isSpace() && previous != QLatin1Char('(') &&
        previous != QLatin1Char('[')) {
      HideMentionMenu();
      return;
    }
  }
  const QString token = before.mid(at);
  if (token.contains(QLatin1Char(' ')) || token.contains(QLatin1Char('\n')) ||
      token.startsWith(QStringLiteral("@file:")) ||
      token.startsWith(QStringLiteral("@folder:"))) {
    HideMentionMenu();
    return;
  }
  const QString query = token.mid(1);
  QString directory_part;
  QString prefix = query;
  const int slash = query.lastIndexOf(QLatin1Char('/'));
  if (slash >= 0) {
    directory_part = query.left(slash + 1);
    prefix = query.mid(slash + 1);
  }

  QString directory;
  if (directory_part.isEmpty()) {
    directory = mention_root_;
  } else if (directory_part.startsWith(QLatin1Char('/'))) {
    directory = directory_part;
  } else if (directory_part.startsWith(QStringLiteral("~/"))) {
    directory = mention_root_ + directory_part.mid(1);
  } else {
    directory = mention_root_;
    if (!directory.endsWith(QLatin1Char('/'))) {
      directory += QLatin1Char('/');
    }
    directory += directory_part;
  }
  while (directory.size() > 1 && directory.endsWith(QLatin1Char('/'))) {
    directory.chop(1);
  }

  if (!path_cache_.contains(directory)) {
    if (!path_pending_.contains(directory)) {
      path_pending_.insert(directory);
      const int generation = mention_generation_;
      QPointer<Composer> self(this);
      path_lister_(directory,
                   [self, directory, generation](const QJsonArray& entries,
                                                 const QString& error) {
                     if (self == nullptr) {
                       return;
                     }
                     self->path_pending_.remove(directory);
                     if (generation != self->mention_generation_) {
                       return;
                     }
                     if (!error.isEmpty()) {
                       self->HideMentionMenu();
                       return;
                     }
                     self->path_cache_.insert(directory, entries);
                     self->UpdateMentionMenu();
                   });
    }
    return;
  }
  const QJsonArray entries = path_cache_.value(directory);
  mention_menu_->clear();
  for (const QJsonValue& value : entries) {
    const QJsonObject entry = value.toObject();
    const QString name = entry.value(QStringLiteral("name")).toString();
    if (!name.startsWith(prefix, Qt::CaseInsensitive)) {
      continue;
    }
    const bool is_directory =
        entry.value(QStringLiteral("isDirectory")).toBool();
    auto* item = new QListWidgetItem(
        is_directory ? name + QLatin1Char('/') : name, mention_menu_);
    const QString absolute = entry.value(QStringLiteral("path")).toString();
    item->setData(Qt::UserRole, absolute);
    item->setData(Qt::UserRole + 1, is_directory);
  }
  if (mention_menu_->count() == 0) {
    HideMentionMenu();
    return;
  }
  mention_directory_ = directory;
  mention_prefix_ = prefix;
  mention_token_ = token;
  mention_menu_->setCurrentRow(0);
  const QRectF card = CardRect();
  const int width = std::min<int>(440, std::max<int>(240, card.width()));
  const QPoint origin = mapToGlobal(QPoint(static_cast<int>(card.left()) + 14,
                                           static_cast<int>(card.top()) - 188));
  mention_menu_->setGeometry(
      origin.x(), origin.y(), width,
      std::min<int>(180, mention_menu_->count() * 24 + 8));
  mention_menu_->show();
}

void Composer::HideMentionMenu() {
  if (mention_menu_ != nullptr && mention_menu_->isVisible()) {
    mention_menu_->hide();
  }
}

void Composer::AcceptMention() {
  if (mention_menu_ == nullptr || mention_menu_->currentItem() == nullptr) {
    return;
  }
  QListWidgetItem* item = mention_menu_->currentItem();
  const QString absolute = item->data(Qt::UserRole).toString();
  const bool is_directory = item->data(Qt::UserRole + 1).toBool();
  const QString display = MentionDisplayPath(absolute);
  QString replacement = QStringLiteral("@") + display;
  if (is_directory && !replacement.endsWith(QLatin1Char('/'))) {
    replacement += QLatin1Char('/');
  }
  const QTextCursor cursor = editor_->textCursor();
  const int end = cursor.position();
  const int start = end - mention_token_.size();
  QTextCursor edit = cursor;
  edit.setPosition(std::max(0, start));
  edit.setPosition(end, QTextCursor::KeepAnchor);
  edit.insertText(is_directory ? replacement : replacement + QLatin1Char(' '));
  editor_->setTextCursor(edit);
  HideMentionMenu();
  if (is_directory) {
    UpdateMentionMenu();
  } else {
    FocusEditor();
  }
}

QString Composer::PromotePathRefs(const QString& text) {
  static const QRegularExpression bare_path(QStringLiteral(
      R"((?<![\w@/])@((?!(?:file|folder|url|image|tool|line|terminal|session|git):)[^\s@:]*/[^\s@:]*))"));
  QString out;
  int cursor = 0;
  auto matches = bare_path.globalMatch(text);
  while (matches.hasNext()) {
    const QRegularExpressionMatch match = matches.next();
    const QString path = match.captured(1);
    QString trimmed = path;
    while (trimmed.endsWith(QLatin1Char('/'))) {
      trimmed.chop(1);
    }
    if (trimmed.isEmpty()) {
      continue;
    }
    trimmed.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    out += text.mid(cursor, match.capturedStart() - cursor);
    out += path.endsWith(QLatin1Char('/'))
               ? QStringLiteral("@folder:\"%1\"").arg(trimmed)
               : QStringLiteral("@file:\"%1\"").arg(trimmed);
    cursor = match.capturedEnd();
  }
  return out + text.mid(cursor);
}

void Composer::SetReasoning(const QString& effort) {
  if (effort.isEmpty()) {
    return;
  }
  reasoning_ = effort;
  if (reasoning_button_ != nullptr) {
    reasoning_button_->setText(effort);
    reasoning_button_->setToolTip(
        QStringLiteral("Reasoning effort: %1").arg(effort));
  }
  update();
}

void Composer::ShowModelMenu() {
  QMenu menu(this);
  menu.setFont(theme_.UiFont(11, QFont::Normal));
  menu.setStyleSheet(
      QStringLiteral(
          "QMenu { background: %1; color: %2; border: 1px solid %3; "
          "padding: 4px; }"
          "QMenu::item { padding: 5px 18px 5px 10px; border-radius: 5px; }"
          "QMenu::item:selected { background: %4; }")
          .arg(theme_.surface.name(), theme_.foreground.name(),
               theme_.stroke.name(), theme_.selected.name()));
  const std::vector<ModelProvider> visible =
      VisibleModelCatalog(providers_, hidden_models_);
  for (const ModelProvider& provider : visible) {
    QMenu* submenu =
        menu.addMenu(provider.name.isEmpty() ? provider.slug : provider.name);
    submenu->setFont(theme_.UiFont(11, QFont::Normal));
    for (const QString& model : provider.models) {
      QAction* action = submenu->addAction(model);
      connect(action, &QAction::triggered, this, [this, provider, model]() {
        SetModelLabel(model);
        if (on_model_selected) {
          on_model_selected(provider.slug, model);
        }
      });
    }
  }
  if (providers_.empty()) {
    QAction* none = menu.addAction(QStringLiteral("No models available"));
    none->setEnabled(false);
  } else if (visible.empty()) {
    QAction* none = menu.addAction(QStringLiteral("All models hidden"));
    none->setEnabled(false);
  }
  menu.addSeparator();
  QAction* visibility = menu.addAction(QStringLiteral("Visible models…"));
  connect(visibility, &QAction::triggered, this,
          [this]() { ShowModelVisibilityDialog(); });
  menu.exec(
      model_button_->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 4)));
}

void Composer::ShowReasoningMenu() {
  QMenu menu(this);
  menu.setFont(theme_.UiFont(11, QFont::Normal));
  menu.setStyleSheet(
      QStringLiteral(
          "QMenu { background: %1; color: %2; border: 1px solid %3; "
          "padding: 4px; }"
          "QMenu::item { padding: 5px 18px 5px 10px; border-radius: 5px; }"
          "QMenu::item:selected { background: %4; }")
          .arg(theme_.surface.name(), theme_.foreground.name(),
               theme_.stroke.name(), theme_.selected.name()));
  for (const QString& level :
       {QStringLiteral("off"), QStringLiteral("low"), QStringLiteral("medium"),
        QStringLiteral("high")}) {
    QAction* action = menu.addAction(level);
    action->setCheckable(true);
    action->setChecked(level == reasoning_);
    connect(action, &QAction::triggered, this, [this, level]() {
      SetReasoning(level);
      if (on_reasoning_selected) {
        on_reasoning_selected(level);
      }
    });
  }
  menu.exec(
      reasoning_button_->mapToGlobal(QPoint(0, -menu.sizeHint().height() - 4)));
}

void Composer::UpdateChipStyles() {
  if (model_button_ == nullptr || reasoning_button_ == nullptr) {
    return;
  }
  const QString common = QStringLiteral(
      "QPushButton { border: none; text-align: left; padding: "
      "0 0 0 10px; font-size: 11px; font-weight: 600; }");
  model_button_->setStyleSheet(
      common +
      QStringLiteral("QPushButton { background: transparent; color: %1; "
                     "} QPushButton:hover { color: %2; }")
          .arg(theme_.text_secondary.name(), theme_.foreground.name()));
  const QColor pill = Mix(theme_.surface, theme_.foreground, 0.08);
  const QColor pill_hover = Mix(theme_.surface, theme_.foreground, 0.14);
  reasoning_button_->setStyleSheet(
      common +
      QStringLiteral("QPushButton { background: %1; color: %2; "
                     "border-radius: 10px; padding: 0 8px; } "
                     "QPushButton:hover { background: %3; }")
          .arg(pill.name(), theme_.text_secondary.name(), pill_hover.name()));
}

void Composer::SetTheme(const Theme& theme) {
  theme_ = theme;
  UpdateEditorStyle();
  UpdateChipStyles();
  update();
}

void Composer::UpdateEditorStyle() {
  editor_->setFont(theme_.UiFont(13, QFont::Normal));
  QPalette palette = editor_->palette();
  palette.setColor(QPalette::Text, theme_.foreground);
  palette.setColor(QPalette::PlaceholderText, theme_.text_tertiary);
  palette.setColor(QPalette::Highlight, theme_.selected);
  palette.setColor(QPalette::HighlightedText, theme_.foreground);
  palette.setColor(QPalette::Base, Qt::transparent);
  editor_->setPalette(palette);
  editor_->setStyleSheet(
      QStringLiteral("QTextEdit { background: transparent; border: "
                     "none; padding: 0px; color: %1; }")
          .arg(theme_.foreground.name()));
}

void Composer::SetText(const QString& text) {
  editor_->setPlainText(text);
  QTextCursor cursor = editor_->textCursor();
  cursor.movePosition(QTextCursor::End);
  editor_->setTextCursor(cursor);
  // textChanged fires inside setPlainText, before the caret reaches the end,
  // so the popup update is repeated once the caret is where the user sees it.
  UpdateSlashMenu();
  UpdateMentionMenu();
}

void Composer::SetModelLabel(const QString& model) {
  if (model.isEmpty() || model_label_ == model) {
    return;
  }
  model_label_ = model;
  if (model_button_ != nullptr) {
    model_button_->setText(model);
    model_button_->setToolTip(model);
  }
  update();
}

void Composer::SetSlashCommands(std::vector<SlashCommand> commands) {
  if (commands.empty()) {
    return;
  }
  slash_commands_ = std::move(commands);
  if (SlashMenuVisible()) {
    UpdateSlashMenu();
  }
}

void Composer::SetRunning(bool running, qint64 started_ms) {
  if (running_ == running) {
    if (running && started_ms > 0 && started_ms != running_since_ms_) {
      running_since_ms_ = started_ms;
      update();
    }
    return;
  }
  running_ = running;
  if (running) {
    running_since_ms_ =
        started_ms > 0 ? started_ms : QDateTime::currentMSecsSinceEpoch();
    activity_tick_ = 0;
    if (activity_timer_ != nullptr) {
      activity_timer_->start();
    }
    setFixedHeight(kComposerHeight + kStatusLineHeight);
    emit HeightChanged(kComposerHeight + kStatusLineHeight);
  } else {
    if (activity_timer_ != nullptr) {
      activity_timer_->stop();
    }
    setFixedHeight(kComposerHeight);
    emit HeightChanged(kComposerHeight);
  }
  update();
}

void Composer::SetQueuedPrompts(int count) {
  const int bounded = std::max(0, count);
  if (queued_prompts_ == bounded) {
    return;
  }
  queued_prompts_ = bounded;
  update();
}

void Composer::Clear() {
  editor_->clear();
  attachments_.clear();
  update();
}

QString Composer::Text() const { return editor_->toPlainText(); }

void Composer::FocusEditor() { editor_->setFocus(Qt::ShortcutFocusReason); }

QRectF Composer::CardRect() const {
  return QRectF(0, 4, width(), height() - 10);
}

QRectF Composer::SendRect() const {
  const qreal action_bottom =
      CardRect().bottom() - 10 - (running_ ? kStatusLineHeight : 0);
  return QRectF(CardRect().right() - 14 - kSendSize, action_bottom - kSendSize,
                kSendSize, kSendSize);
}

QRectF Composer::AttachRect() const {
  const qreal action_bottom =
      CardRect().bottom() - 10 - (running_ ? kStatusLineHeight : 0);
  return QRectF(CardRect().left() + 14, action_bottom - kSendSize, 18,
                kSendSize);
}

QRectF Composer::VoiceRect() const {
  const QRectF attach = AttachRect();
  return QRectF(attach.right() + 4, attach.top(), 20, attach.height());
}

QRectF Composer::AttachmentChipRect(int index) const {
  if (index < 0 || index >= static_cast<int>(attachments_.size())) {
    return QRectF();
  }
  const QRectF card = CardRect();
  const QFontMetricsF metrics(theme_.UiFont(10, QFont::Normal));
  const int top = static_cast<int>(card.top() + 16 + kEditorHeight);
  int left = static_cast<int>(card.left() + 14);
  const int max_right = static_cast<int>(card.right() - 18);
  const auto width_for = [&](int item) {
    return std::min<qreal>(
        180.0, metrics.horizontalAdvance(attachments_[item].first) + 50.0);
  };
  for (int i = 0; i < index; ++i) {
    left += static_cast<int>(width_for(i)) + 6;
  }
  const qreal width = width_for(index);
  if (left + width > max_right) {
    return QRectF();
  }
  return QRectF(left, top, width, 20);
}

int Composer::VisibleAttachmentCount() const {
  for (int i = 0; i < static_cast<int>(attachments_.size()); ++i) {
    if (AttachmentChipRect(i).isNull()) {
      return i;
    }
  }
  return static_cast<int>(attachments_.size());
}

QRectF Composer::AttachmentDrawerRect() const {
  const int visible = VisibleAttachmentCount();
  if (visible >= static_cast<int>(attachments_.size())) {
    return QRectF();
  }
  const QRectF card = CardRect();
  int left = static_cast<int>(card.left() + 14);
  if (visible > 0) {
    left = static_cast<int>(AttachmentChipRect(visible - 1).right()) + 6;
  }
  const int hidden = static_cast<int>(attachments_.size()) - visible;
  const QFontMetricsF metrics(theme_.UiFont(10, QFont::Normal));
  const qreal width = std::max<qreal>(
      34.0, metrics.horizontalAdvance(QStringLiteral("+%1").arg(hidden)) + 20);
  return QRectF(left, card.top() + 16 + kEditorHeight, width, 20);
}

void Composer::ShowAttachmentDrawer() {
  const int visible = VisibleAttachmentCount();
  if (visible >= static_cast<int>(attachments_.size())) {
    return;
  }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("composerAttachmentDrawer"));
  menu->setAttribute(Qt::WA_DeleteOnClose);
  for (int i = visible; i < static_cast<int>(attachments_.size()); ++i) {
    QAction* action =
        menu->addAction(QStringLiteral("Remove %1").arg(attachments_[i].first));
    action->setData(i);
    QObject::connect(action, &QAction::triggered, this,
                     [this, i]() { RemoveAttachment(i); });
  }
  menu->popup(mapToGlobal(AttachmentDrawerRect().bottomLeft().toPoint()));
}

QMenu* Composer::ShowAttachmentDrawerForTest() {
  const int visible = VisibleAttachmentCount();
  if (visible >= static_cast<int>(attachments_.size())) {
    return nullptr;
  }
  auto* menu = new QMenu(this);
  menu->setObjectName(QStringLiteral("composerAttachmentDrawer"));
  for (int i = visible; i < static_cast<int>(attachments_.size()); ++i) {
    QAction* action =
        menu->addAction(QStringLiteral("Remove %1").arg(attachments_[i].first));
    QObject::connect(action, &QAction::triggered, this,
                     [this, i]() { RemoveAttachment(i); });
  }
  return menu;
}

void Composer::RemoveAttachment(int index) {
  if (index < 0 || index >= static_cast<int>(attachments_.size())) {
    return;
  }
  attachments_.erase(attachments_.begin() + index);
  update();
}

QRectF Composer::ModelChipRect() const {
  const qreal action_bottom =
      CardRect().bottom() - 10 - (running_ ? kStatusLineHeight : 0);
  return QRectF(VoiceRect().right() + 8, action_bottom - kSendSize + 3, 156,
                20);
}

QRectF Composer::ReasoningChipRect() const {
  const QRectF model = ModelChipRect();
  return QRectF(model.right() + 8, model.top(), 78, model.height());
}

void Composer::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const QRectF card = CardRect();
  DrawSoftShadow(painter, card, theme_.radius_large, 22, 10);
  FillRounded(painter, card, theme_.radius_large, theme_.surface);
  StrokeRounded(painter, card, theme_.radius_large,
                editor_focused_ ? theme_.accent : theme_.stroke,
                editor_focused_ ? 1.2 : 1.0);

  DrawCodicon(painter, AttachRect(), 0xEA7B, theme_.text_tertiary, 13);
  const QRectF voice = VoiceRect();
  if (voice_recording_) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme_.accent);
    painter.drawEllipse(voice.adjusted(1, 3, -1, -3));
    DrawCodicon(painter, voice, 0xEC1C, QColor(QStringLiteral("#FFFFFF")), 13);
  } else {
    DrawCodicon(painter, voice, 0xEC12, theme_.text_tertiary, 13);
  }

  // Staged attachment chips: name plus a remove affordance. A chip only shows
  // when it fits the row; extra files stay attached but unlisted, and the
  // prompt still names every path.
  painter.setFont(theme_.UiFont(10, QFont::Normal));
  for (int i = 0; i < static_cast<int>(attachments_.size()); ++i) {
    const QRectF chip_rect = AttachmentChipRect(i);
    if (chip_rect.isNull()) {
      break;
    }
    FillRounded(painter, chip_rect, 9, theme_.hover);
    StrokeRounded(painter, chip_rect, 9, theme_.stroke, 1.0);
    const QPixmap thumb = AttachmentThumbnail(attachments_[i].second);
    qreal text_left = chip_rect.left() + 8;
    if (!thumb.isNull()) {
      painter.drawPixmap(
          QRectF(chip_rect.left() + 5, chip_rect.top() + 3, 14, 14).toRect(),
          thumb);
      text_left = chip_rect.left() + 23;
    }
    const QRectF text_rect(text_left, chip_rect.top(),
                           chip_rect.right() - 18 - text_left,
                           chip_rect.height());
    painter.setPen(theme_.text_secondary);
    painter.drawText(text_rect, Qt::AlignVCenter | Qt::AlignLeft,
                     QFontMetricsF(painter.font())
                         .elidedText(attachments_[i].first, Qt::ElideMiddle,
                                     static_cast<int>(text_rect.width())));
    painter.setPen(theme_.text_tertiary);
    painter.drawText(
        QRectF(chip_rect.right() - 18, chip_rect.top(), 14, chip_rect.height()),
        Qt::AlignVCenter | Qt::AlignCenter, QStringLiteral("×"));
  }

  const QRectF drawer = AttachmentDrawerRect();
  if (!drawer.isNull()) {
    FillRounded(painter, drawer, 9, theme_.hover);
    StrokeRounded(painter, drawer, 9, theme_.stroke, 1.0);
    painter.setPen(theme_.text_secondary);
    painter.drawText(
        drawer, Qt::AlignCenter,
        QStringLiteral("+%1").arg(static_cast<int>(attachments_.size()) -
                                  VisibleAttachmentCount()));
  }

  const QRectF chip = ModelChipRect();
  FillRounded(painter, chip, 10, theme_.hover);
  DrawCodicon(painter, QRectF(chip.right() - 18, chip.top(), 14, chip.height()),
              0xEAB4, theme_.text_tertiary, 9);

  const QRectF send = SendRect();
  const QString voice_text = VoiceStatusText();
  if (!voice_text.isEmpty()) {
    const QRectF reasoning = ReasoningChipRect();
    painter.setFont(theme_.UiFont(10, QFont::Normal));
    painter.setPen(theme_.accent);
    painter.drawText(
        QRectF(reasoning.right() + 10, reasoning.top(),
               std::max<qreal>(0, send.left() - reasoning.right() - 26),
               reasoning.height()),
        Qt::AlignVCenter | Qt::AlignLeft, voice_text);
  }

  const bool enabled = running_ || !Text().trimmed().isEmpty();
  QColor send_fill = running_ ? theme_.danger : theme_.accent;
  if (!enabled) {
    send_fill = Mix(theme_.surface, theme_.foreground, 0.10);
  } else if (send_hovered_) {
    send_fill = send_fill.darker(112);
  }
  painter.setPen(Qt::NoPen);
  painter.setBrush(send_fill);
  painter.drawEllipse(send);
  if (running_) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#FFFFFF"));
    painter.drawRoundedRect(
        QRectF(send.center().x() - 4, send.center().y() - 4, 8, 8), 2, 2);
  } else {
    DrawCodicon(painter, send, 0xEAA1,
                enabled ? QColor("#FFFFFF") : theme_.text_tertiary, 12);
  }

  if (running_) {
    // Live work indicator on its own line under the action row: three dots
    // marching plus elapsed time, so it never competes with the chips or the
    // "enter to send" hint for horizontal space.
    const qint64 elapsed =
        (QDateTime::currentMSecsSinceEpoch() - running_since_ms_) / 1000;
    const QString label = QStringLiteral("generating %1s").arg(elapsed);
    const QRectF status(card.left() + 16, card.bottom() - kStatusLineHeight,
                        card.width() - 32, kStatusLineHeight);
    for (int i = 0; i < 3; ++i) {
      const bool lit = (activity_tick_ + i) % 3 == 0;
      const qreal radius = lit ? 2.6 : 1.6;
      const QPointF center(status.left() + 4 + i * 7, status.center().y());
      painter.setPen(Qt::NoPen);
      painter.setBrush(lit ? theme_.accent
                           : Mix(theme_.accent, theme_.surface, 0.55));
      painter.drawEllipse(center, radius, radius);
    }
    painter.setFont(theme_.UiFont(10, QFont::DemiBold));
    painter.setPen(theme_.accent);
    const qreal label_width =
        QFontMetricsF(painter.font()).horizontalAdvance(label);
    painter.drawText(QRectF(status.left() + 28, status.top(), label_width + 2,
                            status.height()),
                     Qt::AlignVCenter | Qt::AlignLeft, label);
    if (queued_prompts_ > 0) {
      painter.setFont(theme_.UiFont(10, QFont::Normal));
      painter.setPen(theme_.text_tertiary);
      painter.drawText(
          QRectF(status.left() + 36 + label_width, status.top(),
                 status.width() - label_width - 40, status.height()),
          Qt::AlignVCenter | Qt::AlignLeft,
          QStringLiteral("%1 queued").arg(queued_prompts_));
    }
  } else {
    painter.setFont(theme_.UiFont(10, QFont::Normal));
    painter.setPen(theme_.text_tertiary);
    painter.drawText(QRectF(send.left() - 120, send.top(), 108, send.height()),
                     Qt::AlignVCenter | Qt::AlignRight,
                     QStringLiteral("enter to send"));
  }
}

void Composer::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  const QRectF card = CardRect();
  editor_->setGeometry(static_cast<int>(card.left() + 14),
                       static_cast<int>(card.top() + 12),
                       static_cast<int>(card.width() - 28), kEditorHeight);
  if (model_button_ != nullptr) {
    const QRectF chip = ModelChipRect();
    model_button_->setGeometry(
        QRectF(chip.left() + 1, chip.top(), chip.width() - 20, chip.height())
            .toRect());
    reasoning_button_->setGeometry(ReasoningChipRect().toRect());
  }
}

QString Composer::SubmissionText() const {
  QString text = PromotePathRefs(Text().trimmed());
  if (text.isEmpty()) {
    return text;
  }
  if (!attachments_.empty()) {
    QStringList lines;
    for (const auto& attachment : attachments_) {
      lines.append(
          QStringLiteral("- %1 (%2)").arg(attachment.first, attachment.second));
    }
    text += QStringLiteral("\n\nAttached files:\n") + lines.join('\n');
  }
  return text;
}

QPixmap Composer::AttachmentThumbnail(const QString& path) {
  if (attachment_thumbnails_.contains(path)) {
    return attachment_thumbnails_.value(path);
  }
  QImageReader reader(path);
  reader.setAutoTransform(true);
  QSize size = reader.size();
  QPixmap thumb;
  if (size.isValid() && !size.isEmpty()) {
    size.scale(28, 28, Qt::KeepAspectRatio);
    reader.setScaledSize(size);
    const QImage image = reader.read();
    if (!image.isNull()) {
      thumb = QPixmap::fromImage(image);
    }
  }
  attachment_thumbnails_.insert(path, thumb);
  return thumb;
}

void Composer::Submit() {
  const QString text = SubmissionText();
  if (text.isEmpty()) {
    return;
  }
  emit Submitted(text);
  attachments_.clear();
  attachment_thumbnails_.clear();
  history_cursor_ = -1;
  history_draft_.clear();
  update();
}

bool Composer::SlashMenuVisible() const {
  return slash_menu_ != nullptr && slash_menu_->isVisible();
}

QStringList Composer::SlashMenuEntries() const {
  QStringList entries;
  if (slash_menu_ == nullptr) {
    return entries;
  }
  for (int i = 0; i < slash_menu_->count(); ++i) {
    entries.append(slash_menu_->item(i)->text());
  }
  return entries;
}

void Composer::HideSlashMenu() {
  if (slash_menu_ != nullptr && slash_menu_->isVisible()) {
    slash_menu_->hide();
  }
}

void Composer::UpdateSlashMenu() {
  if (slash_menu_ == nullptr) {
    return;
  }
  const QString text = Text();
  const bool candidate = text.startsWith(QLatin1Char('/')) &&
                         !text.contains(QLatin1Char('\n')) &&
                         !text.contains(QLatin1Char(' '));
  if (!candidate) {
    HideSlashMenu();
    return;
  }
  const auto matches = MatchSlashCommands(slash_commands_, text);
  if (matches.empty()) {
    HideSlashMenu();
    return;
  }
  slash_menu_->clear();
  for (const SlashCommand* command : matches) {
    const QString label = command->takes_args
                              ? QStringLiteral("%1 %2    %3")
                                    .arg(command->name, command->argument_hint,
                                         command->description)
                              : QStringLiteral("%1    %2")
                                    .arg(command->name, command->description);
    auto* item = new QListWidgetItem(label, slash_menu_);
    item->setData(Qt::UserRole, command->name);
  }
  slash_menu_->setCurrentRow(0);
  const QRectF card = CardRect();
  const int width = std::min<int>(440, std::max<int>(240, card.width()));
  const int height = std::min<int>(8, slash_menu_->count()) * 28 + 10;
  slash_menu_->resize(width, height);
  const QPoint anchor =
      mapToGlobal(QPoint(static_cast<int>(card.left()),
                         static_cast<int>(card.top()) - height - 6));
  slash_menu_->move(anchor);
  // QColor::name() drops alpha, which turned the 10%-black hover token into
  // an opaque near-black bar over the selected row; stylesheet colours must
  // keep the alpha channel.
  slash_menu_->setStyleSheet(
      QStringLiteral(
          "QListWidget { background: %1; color: %2; border: 1px solid %3; "
          "border-radius: 8px; padding: 4px; outline: none; "
          "selection-background-color: %4; selection-color: %2; }"
          "QListWidget::item { padding: 5px 8px; border-radius: 5px; "
          "color: %2; }"
          "QListWidget::item:selected { background: %4; color: %2; }")
          .arg(theme_.surface.name(QColor::HexArgb),
               theme_.foreground.name(QColor::HexArgb),
               theme_.stroke.name(QColor::HexArgb),
               theme_.hover.name(QColor::HexArgb)));
  slash_menu_->show();
}

void Composer::AcceptSlashCommand() {
  if (slash_menu_ == nullptr) {
    return;
  }
  QListWidgetItem* item = slash_menu_->currentItem();
  if (item == nullptr) {
    return;
  }
  const QString name = item->data(Qt::UserRole).toString();
  // Resolve against the active catalog (the gateway's list): commands the
  // built-in table does not know, like /steer, still take arguments.
  bool takes_args = false;
  const SlashCommand* builtin = FindSlashCommand(name);
  if (builtin != nullptr) {
    takes_args = builtin->takes_args;
  }
  for (const SlashCommand& candidate : slash_commands_) {
    if (candidate.name.compare(name, Qt::CaseInsensitive) == 0) {
      // Gateway catalog rows do not carry argument metadata, but most accept
      // one ("/steer <text>", "/profile [name]"), so completion offers a space.
      takes_args = true;
      break;
    }
  }
  const QString completed = takes_args ? name + QLatin1Char(' ') : name;
  SetText(completed);
  HideSlashMenu();
  FocusEditor();
}

std::vector<ClickTarget> Composer::ClickTargets() const {
  std::vector<ClickTarget> targets;
  targets.push_back({QStringLiteral("composer:attach"), AttachRect(), true});
  targets.push_back({QStringLiteral("composer:voice"), VoiceRect(), true});
  targets.push_back({QStringLiteral("composer:send"), SendRect()});
  for (int i = 0; i < static_cast<int>(attachments_.size()); ++i) {
    const QRectF rect = AttachmentChipRect(i);
    if (rect.isNull()) {
      break;
    }
    targets.push_back({QStringLiteral("composer:attachment:%1").arg(i), rect});
  }
  const QRectF drawer = AttachmentDrawerRect();
  if (!drawer.isNull()) {
    targets.push_back({QStringLiteral("composer:attachment-drawer"), drawer});
  }
  return targets;
}

void Composer::AddAttachment(const QString& name, const QString& path) {
  for (const auto& attachment : attachments_) {
    if (attachment.second == path) {
      return;
    }
  }
  attachments_.emplace_back(name, path);
  update();
}

QStringList Composer::AttachmentPaths() const {
  QStringList paths;
  for (const auto& attachment : attachments_) {
    paths.append(attachment.second);
  }
  return paths;
}

void Composer::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && !AttachmentDrawerRect().isNull() &&
      AttachmentDrawerRect().contains(event->position())) {
    ShowAttachmentDrawer();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton) {
    for (int i = 0; i < static_cast<int>(attachments_.size()); ++i) {
      const QRectF chip = AttachmentChipRect(i);
      if (!chip.isNull() && chip.contains(event->position())) {
        RemoveAttachment(i);
        event->accept();
        return;
      }
    }
  }
  if (event->button() == Qt::LeftButton && running_ && queued_prompts_ > 0) {
    const QRectF card = CardRect();
    const QRectF status(card.left() + 16, card.bottom() - kStatusLineHeight,
                        card.width() - 32, kStatusLineHeight);
    if (status.contains(event->position())) {
      emit QueuedReviewRequested();
      event->accept();
      return;
    }
  }
  if (event->button() == Qt::LeftButton &&
      AttachRect().contains(event->position())) {
    emit AttachRequested();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton &&
      VoiceRect().contains(event->position())) {
    emit VoiceToggleRequested();
    event->accept();
    return;
  }
  if (event->button() == Qt::LeftButton &&
      SendRect().contains(event->position())) {
    if (running_) {
      if (on_stop) {
        on_stop();
      }
      return;
    }
    if (!Text().trimmed().isEmpty()) {
      Submit();
      return;
    }
  }
  QWidget::mousePressEvent(event);
}

void Composer::mouseMoveEvent(QMouseEvent* event) {
  const bool hovered = SendRect().contains(event->position());
  if (hovered != send_hovered_) {
    send_hovered_ = hovered;
    setCursor(hovered ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void Composer::leaveEvent(QEvent* event) {
  send_hovered_ = false;
  update();
  QWidget::leaveEvent(event);
}

void Composer::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData() != nullptr && event->mimeData()->hasUrls()) {
    event->acceptProposedAction();
    return;
  }
  QWidget::dragEnterEvent(event);
}

void Composer::dropEvent(QDropEvent* event) {
  if (event->mimeData() == nullptr || !event->mimeData()->hasUrls()) {
    QWidget::dropEvent(event);
    return;
  }
  QStringList paths;
  for (const QUrl& url : event->mimeData()->urls()) {
    if (url.isLocalFile()) {
      paths.append(url.toLocalFile());
    }
  }
  if (!paths.isEmpty()) {
    emit FilesDropped(paths);
    event->acceptProposedAction();
    return;
  }
  QWidget::dropEvent(event);
}

bool Composer::eventFilter(QObject* watched, QEvent* event) {
  if (watched == editor_) {
    if (event->type() == QEvent::KeyPress) {
      auto* key_event = static_cast<QKeyEvent*>(event);
      if (MentionMenuVisible() && mention_menu_->count() > 0) {
        switch (key_event->key()) {
          case Qt::Key_Down: {
            const int next =
                (mention_menu_->currentRow() + 1) % mention_menu_->count();
            mention_menu_->setCurrentRow(next);
            return true;
          }
          case Qt::Key_Up: {
            const int previous =
                (mention_menu_->currentRow() - 1 + mention_menu_->count()) %
                mention_menu_->count();
            mention_menu_->setCurrentRow(previous);
            return true;
          }
          case Qt::Key_Tab:
          case Qt::Key_Return:
          case Qt::Key_Enter: {
            AcceptMention();
            return true;
          }
          case Qt::Key_Escape: {
            HideMentionMenu();
            return true;
          }
          default:
            break;
        }
      }
      const bool menu_open = SlashMenuVisible() && slash_menu_->count() > 0;
      if (menu_open) {
        switch (key_event->key()) {
          case Qt::Key_Down: {
            const int next =
                (slash_menu_->currentRow() + 1) % slash_menu_->count();
            slash_menu_->setCurrentRow(next);
            return true;
          }
          case Qt::Key_Up: {
            const int previous =
                (slash_menu_->currentRow() - 1 + slash_menu_->count()) %
                slash_menu_->count();
            slash_menu_->setCurrentRow(previous);
            return true;
          }
          case Qt::Key_Tab: {
            AcceptSlashCommand();
            return true;
          }
          case Qt::Key_Escape: {
            HideSlashMenu();
            return true;
          }
          case Qt::Key_Return:
          case Qt::Key_Enter: {
            // Enter completes an unfinished command, but submits when the
            // typed text already is the command (so "/new<Enter>" runs).
            const QString typed = Text().trimmed();
            const QString selected =
                slash_menu_->currentItem() == nullptr
                    ? QString()
                    : slash_menu_->currentItem()->data(Qt::UserRole).toString();
            if (typed.compare(selected, Qt::CaseInsensitive) != 0) {
              AcceptSlashCommand();
              return true;
            }
            break;
          }
          default:
            break;
        }
      }
      if (key_event->matches(QKeySequence::Paste)) {
        const QImage image = QGuiApplication::clipboard()->image();
        if (!image.isNull()) {
          const QString path =
              QDir::tempPath() +
              QStringLiteral("/hermes-paste-%1.png")
                  .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
          if (image.save(path, "PNG")) {
            emit FilesDropped({path});
            return true;
          }
        }
      }
      if ((key_event->key() == Qt::Key_Return ||
           key_event->key() == Qt::Key_Enter) &&
          (key_event->modifiers() & Qt::ShiftModifier) == 0) {
        if (running_) {
          const QString text = SubmissionText();
          if (text.isEmpty()) {
            return true;
          }
          if (key_event->modifiers() & Qt::ControlModifier) {
            emit QueueRequested(text);
          } else {
            emit SteerRequested(text);
          }
          return true;
        }
        Submit();
        return true;
      }
      if (key_event->modifiers() == Qt::NoModifier &&
          (key_event->key() == Qt::Key_Up ||
           key_event->key() == Qt::Key_Down) &&
          !history_.isEmpty()) {
        QTextCursor cursor = editor_->textCursor();
        const bool collapsed = !cursor.hasSelection();
        const bool at_start = cursor.position() == 0;
        const bool at_end = cursor.position() == editor_->toPlainText().size();
        if (key_event->key() == Qt::Key_Up &&
            (history_cursor_ >= 0 || (collapsed && at_start))) {
          if (history_cursor_ < 0) {
            history_draft_ = Text();
            history_cursor_ = 0;
          } else if (history_cursor_ < history_.size() - 1) {
            ++history_cursor_;
          } else {
            return true;
          }
          SetText(history_.at(history_cursor_));
          editor_->moveCursor(QTextCursor::End);
          return true;
        }
        if (key_event->key() == Qt::Key_Down && history_cursor_ >= 0 &&
            (collapsed && at_end)) {
          if (history_cursor_ > 0) {
            --history_cursor_;
            SetText(history_.at(history_cursor_));
          } else {
            history_cursor_ = -1;
            SetText(history_draft_);
            history_draft_.clear();
          }
          editor_->moveCursor(QTextCursor::End);
          return true;
        }
      }
    } else if (event->type() == QEvent::FocusIn) {
      editor_focused_ = true;
      update();
    } else if (event->type() == QEvent::FocusOut) {
      editor_focused_ = false;
      update();
    }
  }
  return QWidget::eventFilter(watched, event);
}

}  // namespace hermes::ui
