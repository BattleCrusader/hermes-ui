// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/request_bar.h"

#include <QDesktopServices>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QTextLayout>
#include <QTextOption>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "src/paint_utils.h"

namespace hermes::ui {
namespace {

constexpr int kCardMargin = 24;
constexpr int kCardPadding = 12;
constexpr int kButtonHeight = 26;
constexpr int kButtonGap = 8;
constexpr int kChoicePaddingX = 10;
constexpr int kChoicePaddingY = 7;
constexpr int kChoiceMinHeight = 30;

// A choice is a full-width row, not a half-width grid cell. Long labels wrap
// inside the pill; QPushButton only draws a single line, so paint the wrapped
// text ourselves and publish heightForWidth() to the surrounding VBoxLayout.
class WrappedChoiceButton : public QPushButton {
 public:
  WrappedChoiceButton(const QString& text, const Theme& theme, QWidget* parent)
      : QPushButton(text, parent), theme_(theme) {
    setCursor(Qt::PointingHandCursor);
    setFont(theme_.UiFont(11, QFont::Normal));
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumWidth(0);
    setProperty("selected", false);
  }

  void SetSelected(bool selected) {
    if (property("selected").toBool() == selected) {
      return;
    }
    setProperty("selected", selected);
    update();
  }

  bool hasHeightForWidth() const override { return true; }

  int heightForWidth(int width) const override {
    const int text_width = std::max(1, width - 2 * kChoicePaddingX - 2);
    return std::max(kChoiceMinHeight,
                    TextHeight(text_width) + 2 * kChoicePaddingY);
  }

  QSize sizeHint() const override {
    const int ideal_width = std::clamp(
        fontMetrics().horizontalAdvance(text()) + 2 * kChoicePaddingX + 4, 220,
        520);
    return QSize(ideal_width, heightForWidth(ideal_width));
  }

  QSize minimumSizeHint() const override { return QSize(0, kChoiceMinHeight); }

 protected:
  void enterEvent(QEnterEvent* event) override {
    QPushButton::enterEvent(event);
    update();
  }

  void leaveEvent(QEvent* event) override {
    QPushButton::leaveEvent(event);
    update();
  }

  void paintEvent(QPaintEvent* event) override {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const bool selected = property("selected").toBool();
    QColor background = selected ? theme_.accent : theme_.surface;
    QColor border = selected ? theme_.accent : theme_.stroke;
    QColor text_color =
        selected ? QColor(QStringLiteral("#FFFFFF")) : theme_.foreground;
    if (!isEnabled()) {
      background = theme_.surface;
      border = theme_.stroke;
      text_color = theme_.text_tertiary;
    } else if (isDown()) {
      background = background.darker(108);
    } else if (underMouse()) {
      background = background.lighter(110);
    }

    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setBrush(background);
    painter.setPen(QPen(border, 1));
    painter.drawRoundedRect(box, 6, 6);

    const QRect text_rect = rect().adjusted(kChoicePaddingX, kChoicePaddingY,
                                            -kChoicePaddingX, -kChoicePaddingY);
    if (text_rect.width() > 0 && text_rect.height() > 0) {
      QTextOption option;
      option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
      QTextLayout layout(text(), font());
      layout.setTextOption(option);
      qreal text_height = 0;
      layout.beginLayout();
      while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) {
          break;
        }
        line.setLineWidth(text_rect.width());
        line.setPosition(QPointF(0, text_height));
        text_height += line.height();
      }
      layout.endLayout();

      painter.save();
      painter.setClipRect(text_rect);
      painter.setPen(text_color);
      const qreal top =
          text_rect.top() +
          std::max<qreal>(0, (text_rect.height() - text_height) / 2);
      layout.draw(&painter, QPointF(text_rect.left(), top));
      painter.restore();
    }

    if (hasFocus()) {
      const QColor focus =
          selected ? QColor(QStringLiteral("#FFFFFF")) : theme_.accent;
      painter.setBrush(Qt::NoBrush);
      painter.setPen(QPen(focus, 1, Qt::DashLine));
      painter.drawRoundedRect(box.adjusted(2, 2, -2, -2), 5, 5);
    }
  }

 private:
  int TextHeight(int width) const {
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    QTextLayout layout(text(), font());
    layout.setTextOption(option);
    qreal height = 0;
    layout.beginLayout();
    while (true) {
      QTextLine line = layout.createLine();
      if (!line.isValid()) {
        break;
      }
      line.setLineWidth(width);
      height += line.height();
    }
    layout.endLayout();
    return static_cast<int>(std::ceil(height));
  }

  Theme theme_;
};

// The clarify tool marks its recommended option by appending this suffix to
// the first choice. It is display text, never part of the answer.
QString BareChoice(const QString& choice) {
  const QString kSuffix = QStringLiteral(" (Recommended)");
  if (choice.endsWith(kSuffix)) {
    return choice.left(choice.size() - kSuffix.size()).trimmed();
  }
  return choice;
}

}  // namespace

RequestBar::RequestBar(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme) {
  setObjectName(QStringLiteral("requestBar"));
  layout_ = new QVBoxLayout(this);
  layout_->setContentsMargins(kCardMargin + kCardPadding, 12,
                              kCardMargin + kCardPadding, 12);
  layout_->setSpacing(6);
  setMouseTracking(true);
  setVisible(false);
  setAccessibleName(QStringLiteral("Agent request"));
}

void RequestBar::SetTheme(const Theme& theme) {
  theme_ = theme;
  if (has_request_) {
    Rebuild();
  }
}

void RequestBar::SetRequest(const PendingRequest& request) {
  // Streaming revisions publish every 60 ms. Rebuilding the same card would
  // destroy whatever the user has typed, so only a different request id
  // rebuilds the form.
  if (has_request_ && request_.id == request.id &&
      request_.method == request.method &&
      request_.revision == request.revision) {
    return;
  }
  request_ = request;
  has_request_ = true;
  Rebuild();
  setVisible(true);
}

void RequestBar::Clear() {
  has_request_ = false;
  ClearContent();
  setVisible(false);
}

void RequestBar::ClearContent() {
  inputs_.clear();
  choice_buttons_.clear();
  selected_.clear();
  connection_inputs_.clear();
  value_input_ = nullptr;
  send_button_ = nullptr;
  skip_button_ = nullptr;
  while (QLayoutItem* item = layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) {
      delete widget;
    }
    delete item;
  }
}

void RequestBar::AddHeader(const QString& title, const QString& detail) {
  auto* title_label = new QLabel(title, this);
  title_label->setFont(theme_.UiFont(12, QFont::DemiBold));
  title_label->setStyleSheet(
      QStringLiteral("QLabel { color: %1; background: transparent; }")
          .arg(theme_.foreground.name()));
  title_label->setAttribute(Qt::WA_TransparentForMouseEvents);
  layout_->addWidget(title_label);

  if (detail.isEmpty()) {
    return;
  }
  auto* detail_label = new QLabel(detail, this);
  detail_label->setFont(theme_.MonoFont(11, QFont::Normal));
  detail_label->setWordWrap(true);
  detail_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  detail_label->setStyleSheet(
      QStringLiteral("QLabel { color: %1; background: transparent; }")
          .arg(theme_.text_secondary.name()));
  layout_->addWidget(detail_label);
}

QPushButton* RequestBar::MakeButton(const QString& label, bool primary,
                                    bool danger) {
  auto* button = new QPushButton(label, this);
  button->setCursor(Qt::PointingHandCursor);
  button->setFont(theme_.UiFont(11, QFont::DemiBold));
  button->setFixedHeight(kButtonHeight);
  button->setFocusPolicy(Qt::StrongFocus);
  const QColor base =
      danger ? theme_.danger : (primary ? theme_.accent : theme_.surface);
  const QColor text = (danger || primary) ? QColor(QStringLiteral("#FFFFFF"))
                                          : theme_.foreground;
  const QColor border = (danger || primary) ? base : theme_.stroke;
  button->setStyleSheet(
      QStringLiteral(
          "QPushButton { background: %1; color: %2; border: 1px solid %3; "
          "border-radius: 6px; padding: 0 10px; }"
          "QPushButton:hover { background: %4; }"
          "QPushButton:disabled { color: %5; background: %6; }")
          .arg(base.name(), text.name(), border.name(),
               base.lighter(110).name(), theme_.text_tertiary.name(),
               theme_.surface.name()));
  return button;
}

QLineEdit* RequestBar::MakeInput(const QString& placeholder, bool secret) {
  auto* edit = new QLineEdit(this);
  edit->setFont(theme_.UiFont(12, QFont::Normal));
  edit->setPlaceholderText(placeholder);
  edit->setEchoMode(secret ? QLineEdit::Password : QLineEdit::Normal);
  edit->setFixedHeight(28);
  edit->setClearButtonEnabled(false);
  edit->setStyleSheet(
      QStringLiteral(
          "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
          "border-radius: 6px; padding: 0 8px; "
          "selection-background-color: %4; }")
          .arg(theme_.background.name(), theme_.foreground.name(),
               theme_.stroke.name(), theme_.selected.name()));
  return edit;
}

bool RequestBar::HasConnectionRequest() const {
  return has_request_ && request_.method == QStringLiteral("connection");
}

bool RequestBar::HasConnectionDisplay() const {
  return HasConnectionRequest() && request_.connection.targets.empty() &&
         !request_.connection.suggested.empty();
}

bool RequestBar::AnswerConnectionDisplay(const QString& text) {
  const QString trimmed = text.trimmed();
  if (!HasConnectionDisplay() || trimmed.isEmpty() || !on_respond) {
    return false;
  }
  QJsonObject result;
  result.insert(QStringLiteral("said"), trimmed);
  result.insert(QStringLiteral("settled_by"), QStringLiteral("continue"));
  Send(result);
  return true;
}

bool RequestBar::ContinueConnection() {
  if (!HasConnectionRequest() || !on_respond) {
    return false;
  }
  SendConnectionContinue();
  return true;
}

void RequestBar::Rebuild() {
  ClearContent();
  AddHeader(request_.title, request_.detail);
  if (request_.method == QStringLiteral("approval")) {
    AddApproval();
  } else if (request_.method == QStringLiteral("clarify")) {
    AddClarify();
  } else if (request_.method == QStringLiteral("connection")) {
    AddConnection();
  } else {
    AddValueInput();
  }
  layout_->activate();
  UpdateHeight();
  update();
}

void RequestBar::UpdateHeight() {
  if (!has_request_ || width() <= 0) {
    return;
  }
  // A dynamic form (up to five clarify questions) reserves exactly its
  // content height; the chat view takes the rest. Re-query after a width
  // change so wrapped choices can grow the card.
  const int content_height = layout_->hasHeightForWidth()
                                 ? layout_->heightForWidth(width())
                                 : layout_->sizeHint().height();
  const int next_height = std::max(76, content_height);
  if (height() != next_height) {
    setFixedHeight(next_height);
  }
}

void RequestBar::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  UpdateHeight();
}

void RequestBar::AddApproval() {
  auto* row = new QWidget(this);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 2, 0, 0);
  row_layout->setSpacing(kButtonGap);
  row_layout->addStretch(1);
  for (const QString& choice : request_.choices) {
    QString label = choice;
    if (choice == QStringLiteral("once")) {
      label = QStringLiteral("Allow once");
    } else if (choice == QStringLiteral("session")) {
      label = QStringLiteral("Allow session");
    } else if (choice == QStringLiteral("always")) {
      label = QStringLiteral("Always");
    } else if (choice == QStringLiteral("deny")) {
      label = QStringLiteral("Deny");
    }
    const bool deny = choice == QStringLiteral("deny");
    QPushButton* button = MakeButton(label, !deny, deny);
    connect(button, &QPushButton::clicked, this, [this, choice]() {
      QJsonObject result;
      result.insert(QStringLiteral("choice"), choice);
      Send(result);
    });
    row_layout->addWidget(button);
  }
  layout_->addWidget(row);
}

void RequestBar::AddClarify() {
  inputs_.reserve(request_.questions.size());
  choice_buttons_.reserve(request_.questions.size());
  selected_.reserve(request_.questions.size());
  for (std::size_t index = 0; index < request_.questions.size(); ++index) {
    const ClarifyQuestion& question = request_.questions[index];
    auto* label = new QLabel(question.question, this);
    label->setFont(theme_.UiFont(12, QFont::Normal));
    label->setWordWrap(true);
    label->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(theme_.foreground.name()));
    layout_->addWidget(label);

    std::vector<QPushButton*> buttons;
    if (!question.choices.isEmpty()) {
      auto* choices_host = new QWidget(this);
      auto* choices_layout = new QVBoxLayout(choices_host);
      choices_layout->setContentsMargins(0, 0, 0, 0);
      choices_layout->setSpacing(6);
      for (const QString& choice : question.choices) {
        auto* button = new WrappedChoiceButton(choice, theme_, choices_host);
        connect(button, &QPushButton::clicked, this, [this, index, choice]() {
          OnChoice(static_cast<int>(index), choice);
        });
        choices_layout->addWidget(button);
        buttons.push_back(button);
      }
      layout_->addWidget(choices_host);
    }
    choice_buttons_.push_back(std::move(buttons));
    selected_.push_back(QStringList());

    QLineEdit* edit =
        MakeInput(question.choices.isEmpty()
                      ? QStringLiteral("Type an answer")
                      : (question.multi_select
                             ? QStringLiteral("Additional answer (optional)")
                             : QStringLiteral("Type a custom answer")),
                  false);
    const int input_index = static_cast<int>(index);
    connect(edit, &QLineEdit::textEdited, this,
            [this]() { UpdateSendEnabled(); });
    connect(edit, &QLineEdit::returnPressed, this, [this, input_index]() {
      if (request_.questions.size() == 1) {
        SubmitClarify();
      } else {
        FocusNextInput(input_index);
      }
    });
    inputs_.push_back(edit);
    layout_->addWidget(edit);
  }

  auto* row = new QWidget(this);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 2, 0, 0);
  row_layout->setSpacing(kButtonGap);
  row_layout->addStretch(1);
  skip_button_ = MakeButton(QStringLiteral("Skip"), false, false);
  connect(skip_button_, &QPushButton::clicked, this, [this]() { Skip(); });
  row_layout->addWidget(skip_button_);
  send_button_ = MakeButton(QStringLiteral("Send"), true, false);
  connect(send_button_, &QPushButton::clicked, this,
          [this]() { SubmitClarify(); });
  row_layout->addWidget(send_button_);
  layout_->addWidget(row);
  UpdateSendEnabled();
}

void RequestBar::AddConnection() {
  const ConnectionOperation& operation = request_.connection;
  if (!operation.suggested.isEmpty()) {
    auto* hint =
        new QLabel(QStringLiteral("Suggested connectors: %1")
                       .arg(operation.suggested.join(QStringLiteral(", "))),
                   this);
    hint->setFont(theme_.UiFont(12, QFont::Normal));
    hint->setWordWrap(true);
    hint->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(theme_.foreground.name()));
    layout_->addWidget(hint);
  }

  for (const ConnectionTarget& target : operation.targets) {
    auto* name = new QLabel(this);
    QString state = target.state;
    if (state.isEmpty()) {
      state = QStringLiteral("pending");
    }
    name->setText(
        QStringLiteral("%1 · %2 · %3").arg(target.name, target.kind, state));
    name->setFont(theme_.UiFont(12, QFont::DemiBold));
    name->setWordWrap(true);
    name->setStyleSheet(
        QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(theme_.foreground.name()));
    layout_->addWidget(name);

    QString detail = target.instructions;
    if (detail.isEmpty()) {
      detail = target.detail;
    }
    if (!target.discovery_error.isEmpty()) {
      detail = detail.isEmpty()
                   ? target.discovery_error
                   : detail + QStringLiteral("  -  ") + target.discovery_error;
    }
    if (!detail.isEmpty()) {
      auto* detail_label = new QLabel(detail, this);
      detail_label->setFont(theme_.MonoFont(11, QFont::Normal));
      detail_label->setWordWrap(true);
      detail_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
      detail_label->setStyleSheet(
          QStringLiteral("QLabel { color: %1; background: transparent; }")
              .arg(theme_.text_secondary.name()));
      layout_->addWidget(detail_label);
    }

    for (const ConnectionEnvField& field : target.required_env) {
      QLineEdit* edit = MakeInput(
          field.prompt.isEmpty() ? field.name : field.prompt, field.secret);
      if (!field.default_value.isEmpty()) {
        edit->setText(field.default_value);
      }
      connection_inputs_.push_back(
          ConnectionInput{target.name, field.name, edit});
      layout_->addWidget(edit);
    }

    auto* row = new QWidget(this);
    auto* row_layout = new QHBoxLayout(row);
    row_layout->setContentsMargins(0, 0, 0, 0);
    row_layout->setSpacing(kButtonGap);
    if (!target.connect_url.isEmpty()) {
      QPushButton* open = MakeButton(QStringLiteral("Open link"), false, false);
      const QString url = target.connect_url;
      connect(open, &QPushButton::clicked, this,
              [url]() { QDesktopServices::openUrl(QUrl(url)); });
      row_layout->addWidget(open);
    }
    row_layout->addStretch(1);
    const bool finished = target.resolved ||
                          target.state == QStringLiteral("connected") ||
                          target.state == QStringLiteral("skipped");
    if (!finished) {
      QPushButton* skip = MakeButton(QStringLiteral("Skip"), false, false);
      connect(skip, &QPushButton::clicked, this, [this, target, skip]() {
        skip->setEnabled(false);
        SendConnectionAnswer(target.name, QStringLiteral("skipped"));
      });
      row_layout->addWidget(skip);

      QString label = QStringLiteral("Approve");
      if (target.action == QStringLiteral("connect") ||
          target.action == QStringLiteral("reconnect") ||
          target.action == QStringLiteral("authorize")) {
        label = QStringLiteral("I've connected");
      } else if (target.action == QStringLiteral("enable")) {
        label = QStringLiteral("Enable");
      }
      QPushButton* approve = MakeButton(label, true, false);
      connect(approve, &QPushButton::clicked, this, [this, target, approve]() {
        approve->setEnabled(false);
        SendConnectionAnswer(target.name, QStringLiteral("approved"));
      });
      row_layout->addWidget(approve);
    }
    layout_->addWidget(row);
  }

  auto* row = new QWidget(this);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 2, 0, 0);
  row_layout->setSpacing(kButtonGap);
  row_layout->addStretch(1);
  QPushButton* skip_all = MakeButton(QStringLiteral("Not now"), false, false);
  connect(skip_all, &QPushButton::clicked, this, [this, skip_all]() {
    skip_all->setEnabled(false);
    SendConnectionContinue();
  });
  row_layout->addWidget(skip_all);
  QPushButton* cont = MakeButton(QStringLiteral("Continue"), true, false);
  connect(cont, &QPushButton::clicked, this, [this, cont]() {
    cont->setEnabled(false);
    SendConnectionContinue();
  });
  row_layout->addWidget(cont);
  layout_->addWidget(row);
}

void RequestBar::AddValueInput() {
  value_input_ =
      MakeInput(request_.placeholder.isEmpty() ? QStringLiteral("Value")
                                               : request_.placeholder,
                request_.secret);
  connect(value_input_, &QLineEdit::textChanged, this,
          [this]() { UpdateSendEnabled(); });
  connect(value_input_, &QLineEdit::returnPressed, this,
          [this]() { SubmitValue(); });
  layout_->addWidget(value_input_);

  auto* row = new QWidget(this);
  auto* row_layout = new QHBoxLayout(row);
  row_layout->setContentsMargins(0, 2, 0, 0);
  row_layout->setSpacing(kButtonGap);
  row_layout->addStretch(1);
  skip_button_ = MakeButton(QStringLiteral("Skip"), false, false);
  connect(skip_button_, &QPushButton::clicked, this, [this]() { Skip(); });
  row_layout->addWidget(skip_button_);
  send_button_ = MakeButton(QStringLiteral("Continue"), true, false);
  connect(send_button_, &QPushButton::clicked, this,
          [this]() { SubmitValue(); });
  row_layout->addWidget(send_button_);
  layout_->addWidget(row);
  UpdateSendEnabled();
}

void RequestBar::UpdateChoiceStyles() {
  for (std::size_t question_index = 0; question_index < choice_buttons_.size();
       ++question_index) {
    const ClarifyQuestion& question = request_.questions[question_index];
    for (std::size_t choice_index = 0;
         choice_index < choice_buttons_[question_index].size();
         ++choice_index) {
      QPushButton* button = choice_buttons_[question_index][choice_index];
      const QString choice = question.choices[choice_index];
      const bool selected =
          selected_[question_index].contains(BareChoice(choice));
      static_cast<WrappedChoiceButton*>(button)->SetSelected(selected);
    }
  }
  UpdateSendEnabled();
}

void RequestBar::OnChoice(int question_index, const QString& choice) {
  if (question_index < 0 ||
      question_index >= static_cast<int>(request_.questions.size())) {
    return;
  }
  ClarifyQuestion& question =
      request_.questions[static_cast<std::size_t>(question_index)];
  QStringList& selected = selected_[static_cast<std::size_t>(question_index)];
  const QString bare = BareChoice(choice);
  if (question.multi_select) {
    if (selected.contains(bare)) {
      selected.removeAll(bare);
    } else {
      selected.push_back(bare);
    }
  } else {
    selected = {bare};
    QLineEdit* input = inputs_[static_cast<std::size_t>(question_index)];
    const QSignalBlocker blocker(input);
    input->setText(bare);
  }
  UpdateChoiceStyles();
}

QString RequestBar::AnswerFor(int index) const {
  if (index < 0 || index >= static_cast<int>(request_.questions.size())) {
    return QString();
  }
  const ClarifyQuestion& question =
      request_.questions[static_cast<std::size_t>(index)];
  const QString draft =
      inputs_[static_cast<std::size_t>(index)]->text().trimmed();
  const QStringList& selected = selected_[static_cast<std::size_t>(index)];
  if (question.multi_select) {
    QStringList combined = selected;
    if (!draft.isEmpty()) {
      combined.push_back(draft);
    }
    if (combined.isEmpty()) {
      return QString();
    }
    QJsonArray array;
    for (const QString& value : combined) {
      array.push_back(value);
    }
    return QString::fromUtf8(
        QJsonDocument(array).toJson(QJsonDocument::Compact));
  }
  if (!draft.isEmpty()) {
    return draft;
  }
  return selected.isEmpty() ? QString() : selected.first();
}

void RequestBar::UpdateSendEnabled() {
  if (send_button_ == nullptr) {
    return;
  }
  if (request_.method == QStringLiteral("clarify")) {
    bool any = false;
    for (int index = 0; index < static_cast<int>(request_.questions.size());
         ++index) {
      if (!AnswerFor(index).isEmpty()) {
        any = true;
        break;
      }
    }
    send_button_->setEnabled(any);
    return;
  }
  send_button_->setEnabled(value_input_ != nullptr &&
                           !value_input_->text().trimmed().isEmpty());
}

void RequestBar::FocusNextInput(int index) {
  for (std::size_t offset = 1; offset <= inputs_.size(); ++offset) {
    const std::size_t next =
        (static_cast<std::size_t>(index) + offset) % inputs_.size();
    if (inputs_[next]->text().trimmed().isEmpty()) {
      inputs_[next]->setFocus(Qt::OtherFocusReason);
      return;
    }
  }
  SubmitClarify();
}

void RequestBar::SubmitClarify() {
  if (!has_request_ || request_.questions.empty()) {
    return;
  }
  QJsonObject answers;
  bool any = false;
  for (int index = 0; index < static_cast<int>(request_.questions.size());
       ++index) {
    const QString answer = AnswerFor(index);
    const QString qid = request_.questions[static_cast<std::size_t>(index)].qid;
    if (answer.isEmpty()) {
      answers.insert(qid, QJsonValue::Null);
    } else {
      answers.insert(qid, answer);
      any = true;
    }
  }
  if (!any) {
    return;
  }
  QJsonObject result;
  result.insert(QStringLiteral("answers"), answers);
  result.insert(QStringLiteral("outcome"), QStringLiteral("submitted"));
  Send(result);
}

void RequestBar::SendConnectionAnswer(const QString& name,
                                      const QString& status) {
  QJsonObject target;
  target.insert(QStringLiteral("name"), name);
  target.insert(QStringLiteral("status"), status);
  QJsonObject env;
  for (const ConnectionInput& input : connection_inputs_) {
    if (input.target != name || input.edit == nullptr) {
      continue;
    }
    const QString value = input.edit->text();
    if (!value.isEmpty()) {
      env.insert(input.name, value);
    }
  }
  if (!env.isEmpty()) {
    target.insert(QStringLiteral("env"), env);
  }
  QJsonObject result;
  result.insert(QStringLiteral("targets"), QJsonArray{target});
  Send(result);
}

void RequestBar::SendConnectionContinue() {
  QJsonObject result;
  result.insert(QStringLiteral("settled_by"), QStringLiteral("continue"));
  Send(result);
}

void RequestBar::SubmitValue() {
  if (!has_request_ || value_input_ == nullptr) {
    return;
  }
  const QString value = value_input_->text();
  value_input_->clear();
  QJsonObject result;
  result.insert(QStringLiteral("value"), value);
  Send(result);
}

void RequestBar::Skip() {
  if (!has_request_) {
    return;
  }
  if (request_.method == QStringLiteral("clarify")) {
    // An empty result is the gateway's cancel-all: no answers key.
    QJsonObject result;
    result.insert(QStringLiteral("outcome"), QStringLiteral("cancelled"));
    Send(result);
    return;
  }
  if (request_.method == QStringLiteral("approval")) {
    QJsonObject result;
    result.insert(QStringLiteral("choice"), QStringLiteral("deny"));
    Send(result);
    return;
  }
  QJsonObject result;
  result.insert(QStringLiteral("value"), QString());
  Send(result);
}

void RequestBar::Send(const QJsonObject& result) {
  if (!has_request_ || !on_respond) {
    return;
  }
  on_respond(request_.id, result);
}

void RequestBar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (!has_request_) {
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRectF card(kCardMargin, 4, width() - kCardMargin * 2, height() - 8);
  const QColor card_fill = theme_.background.lightness() > 128
                               ? QColor(QStringLiteral("#FFF8E6"))
                               : QColor(QStringLiteral("#2A2218"));
  FillRounded(painter, card, 10, card_fill);
  StrokeRounded(painter, card, 10, theme_.warning);
  DrawCodicon(painter, QRectF(card.left() + 12, card.top() + 10, 18, 18),
              0xEAA2, theme_.warning, 13);
}

}  // namespace hermes::ui
