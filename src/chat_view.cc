// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/chat_view.h"

#include <QAbstractTextDocumentLayout>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QImageReader>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStringList>
#include <QTemporaryFile>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "src/markdown.h"
#include "src/paint_utils.h"
#include "src/request_bar.h"

namespace hermes::ui {
namespace {

constexpr qreal kAvatarSize = 20;
constexpr qreal kContentLeft = 56;
constexpr qreal kRightPadding = 34;
constexpr qreal kMessageGap = 22;
constexpr qreal kMaxContentWidth = 720;
constexpr qreal kCodeLineHeight = 19;
constexpr qreal kCodeHeaderHeight = 28;
constexpr qreal kParagraphLineHeight = 20;
constexpr qreal kActionRowButton = 22;
constexpr qreal kActionRowGap = 4;
constexpr qreal kActionRowHeight = 24;
constexpr qreal kQuoteTextInsetLeft = 18;
constexpr qreal kQuoteTextInsetRight = 14;
constexpr qreal kQuoteTextInsetTop = 10;
constexpr qreal kQuoteTextInsetBottom = 10;
constexpr qreal kQuoteBarInset = 8;
constexpr qreal kQuoteBarWidth = 3;
constexpr qreal kQuoteRadius = 7;

// Forward declaration: hit targets are built before the renderer definition.
bool IsPreviewableLanguage(const QString& language);

QString FormatTurnDuration(double seconds) {
  const int total = qMax(0, qRound(seconds));
  if (total < 60) {
    return QStringLiteral("%1s").arg(total);
  }
  return QStringLiteral("%1:%2")
      .arg(total / 60)
      .arg(total % 60, 2, 10, QLatin1Char('0'));
}

QString ReactionFooterText(const ChatMessage& message) {
  if (message.reactions.empty()) {
    return QString();
  }
  QStringList order;
  QHash<QString, int> counts;
  for (const ChatReaction& reaction : message.reactions) {
    if (reaction.emoji.isEmpty()) {
      continue;
    }
    if (!counts.contains(reaction.emoji)) {
      order.push_back(reaction.emoji);
    }
    ++counts[reaction.emoji];
  }
  QStringList parts;
  for (const QString& emoji : order) {
    parts.push_back(
        QStringLiteral("%1 %2").arg(emoji).arg(counts.value(emoji)));
  }
  return parts.join(QStringLiteral("   "));
}

QString EscapeHtml(const QString& text) {
  QString escaped;
  escaped.reserve(text.size());
  for (const QChar character : text) {
    switch (character.unicode()) {
      case '&':
        escaped += QStringLiteral("&amp;");
        break;
      case '<':
        escaped += QStringLiteral("&lt;");
        break;
      case '>':
        escaped += QStringLiteral("&gt;");
        break;
      default:
        escaped += character;
        break;
    }
  }
  return escaped;
}

QString Span(const QString& color, const QString& text) {
  return QStringLiteral("<span style=\"color:%1;\">%2</span>")
      .arg(color, EscapeHtml(text));
}

QString HighlightLine(const QString& line, const QString& language,
                      const Theme& theme) {
  const QSet<QString> keywords = {
      QStringLiteral("void"),      QStringLiteral("int"),
      QStringLiteral("for"),       QStringLiteral("if"),
      QStringLiteral("else"),      QStringLiteral("return"),
      QStringLiteral("const"),     QStringLiteral("auto"),
      QStringLiteral("class"),     QStringLiteral("struct"),
      QStringLiteral("bool"),      QStringLiteral("true"),
      QStringLiteral("false"),     QStringLiteral("nullptr"),
      QStringLiteral("std"),       QStringLiteral("size_t"),
      QStringLiteral("include"),   QStringLiteral("define"),
      QStringLiteral("namespace"), QStringLiteral("using"),
  };
  QString out;
  for (int index = 0; index < line.size();) {
    const QChar character = line.at(index);
    if (language == QStringLiteral("bash") && character == '#') {
      out += Span(theme.code_comment.name(), line.mid(index));
      break;
    }
    if (language != QStringLiteral("bash") && character == '/' &&
        index + 1 < line.size() && line.at(index + 1) == '/') {
      out += Span(theme.code_comment.name(), line.mid(index));
      break;
    }
    if (character == '"') {
      int end = line.indexOf('"', index + 1);
      if (end < 0) {
        end = line.size() - 1;
      }
      out += Span(theme.code_string.name(), line.mid(index, end - index + 1));
      index = end + 1;
      continue;
    }
    if (character.isDigit()) {
      int end = index;
      while (end < line.size() &&
             (line.at(end).isDigit() || line.at(end) == '.')) {
        ++end;
      }
      out += Span(theme.code_number.name(), line.mid(index, end - index));
      index = end;
      continue;
    }
    if (character.isLetter() || character == '_') {
      int end = index;
      while (end < line.size() &&
             (line.at(end).isLetterOrNumber() || line.at(end) == '_')) {
        ++end;
      }
      const QString word = line.mid(index, end - index);
      if (keywords.contains(word)) {
        out += Span(theme.code_keyword.name(), word);
      } else {
        out += EscapeHtml(word);
      }
      index = end;
      continue;
    }
    out += EscapeHtml(QString(character));
    ++index;
  }
  return out;
}

QString HighlightCode(const QString& code, const QString& language,
                      const Theme& theme) {
  // Let the document's default font carry the size: it is already built from
  // MonoFont(), so the Appearance code scale applies. Only the family is
  // overridden here because Qt's default <pre> style pins "monospace".
  QString family = theme.mono_family;
  family.replace(QStringLiteral("&"), QStringLiteral("&amp;"));
  family.replace(QStringLiteral("\""), QStringLiteral("&quot;"));
  // The style attribute is double quoted, so the CSS family must be quoted
  // with HTML entities. Raw quotes terminate the attribute early and drop the
  // colour declaration, which leaves code unreadable on dark backgrounds.
  QString html = QStringLiteral(
                     "<pre style=\"margin:0;white-space:pre-wrap;"
                     "overflow-wrap:anywhere;font-family:&quot;%1&quot;;"
                     "color:%2;\">")
                     .arg(family, theme.code_text.name());
  const QStringList lines = code.split('\n');
  for (int index = 0; index < lines.size(); ++index) {
    if (index != 0) {
      html += QStringLiteral("\n");
    }
    html += HighlightLine(lines.at(index), language, theme);
  }
  html += QStringLiteral("</pre>");
  return html;
}

void ApplyLineHeight(QTextDocument* document, qreal line_height) {
  QTextCursor cursor(document);
  cursor.select(QTextCursor::Document);
  QTextBlockFormat format;
  format.setLineHeight(line_height, QTextBlockFormat::FixedHeight);
  format.setTopMargin(0);
  format.setBottomMargin(0);
  cursor.mergeBlockFormat(format);
}

std::shared_ptr<QTextDocument> MakeDocument(const QString& html,
                                            const QFont& font, qreal width,
                                            qreal line_height) {
  auto document = std::make_shared<QTextDocument>();
  document->setDocumentMargin(0);
  document->setDefaultFont(font);
  QTextOption option = document->defaultTextOption();
  option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  document->setDefaultTextOption(option);
  document->setHtml(html);
  document->setTextWidth(width);
  ApplyLineHeight(document.get(), line_height);
  return document;
}

QString ParagraphHtml(const QString& text, const Theme& theme) {
  QString escaped = InlineHtml(text);
  escaped.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
  // Let the document's default font carry the size so the appearance text
  // scale can grow body copy. Hard-coding 13px here made the setting a no-op
  // for paragraphs.
  return QStringLiteral("<div style=\"margin:0;color:%1;\">%2</div>")
      .arg(theme.foreground.name(), escaped);
}

void DrawStreamingCaret(QPainter& painter, const QTextDocument& document,
                        const QRectF& rect, const QColor& color) {
  const QTextBlock block = document.lastBlock();
  const QTextLayout* layout = block.layout();
  if (layout == nullptr || layout->lineCount() == 0) {
    painter.fillRect(QRectF(rect.left(), rect.top(), 2, 13), color);
    return;
  }
  const QTextLine line = layout->lineAt(layout->lineCount() - 1);
  const QRectF block_rect = document.documentLayout()->blockBoundingRect(block);
  const qreal x = rect.left() + line.naturalTextWidth() + 2;
  const qreal y =
      rect.top() + block_rect.top() + line.y() + (line.height() - 13) / 2.0;
  painter.fillRect(QRectF(x, y, 2, 13), color);
}

}  // namespace

class TranscriptWidget : public QWidget {
 public:
  explicit TranscriptWidget(const Theme& theme, QWidget* parent = nullptr);
  ~TranscriptWidget() override = default;

  void SetTheme(const Theme& theme);
  void SetSnapshot(std::shared_ptr<const ProfileSnapshot> snapshot);
  void SetProfileName(const QString& name);
  void SetRequest(const PendingRequest& request);
  void ClearRequest();
  bool HasConnectionRequest() const;
  bool AnswerConnectionDisplay(const QString& text);
  bool ContinueConnection();
  void SetRequestRespondHandler(
      std::function<void(const QString&, const QJsonObject&)> handler);
  void SetEditMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    edit_handler_ = std::move(handler);
  }
  void SetRegenerateMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    regenerate_handler_ = std::move(handler);
  }
  void SetReadAloudHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    read_aloud_handler_ = std::move(handler);
  }
  void SetPinMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    pin_handler_ = std::move(handler);
  }
  void SetBranchMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    branch_handler_ = std::move(handler);
  }
  void SetRewindMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    rewind_handler_ = std::move(handler);
  }
  void SetReactMessageHandler(
      std::function<void(const ChatMessage&, const QString&)> handler) {
    react_handler_ = std::move(handler);
  }
  void SetDeleteConversationHandler(std::function<void()> handler) {
    delete_conversation_handler_ = std::move(handler);
  }
  void TriggerMessageActionForTest(const QString& action, int message_index,
                                   const QString& value) {
    if (message_index < 0 ||
        message_index >= static_cast<int>(messages_.size())) {
      return;
    }
    const ChatMessage& message =
        messages_[static_cast<std::size_t>(message_index)].source;
    const QString body = MessageText(message);
    if (action == QStringLiteral("branch") && branch_handler_) {
      branch_handler_(message, body);
    } else if (action == QStringLiteral("pin") && pin_handler_) {
      pin_handler_(message, body);
    } else if (action == QStringLiteral("rewind") && rewind_handler_) {
      rewind_handler_(message, body);
    } else if (action == QStringLiteral("react") && react_handler_) {
      react_handler_(message, value);
    } else if (action == QStringLiteral("delete") &&
               delete_conversation_handler_) {
      delete_conversation_handler_();
    }
  }
  QString search_query() const { return search_query_; }
  int SearchMatchCount() const {
    return static_cast<int>(search_matches_.size());
  }
  int CurrentSearchMatch() const { return search_current_; }
  void SetSearchQuery(const QString& query) {
    search_query_ = query.trimmed();
    RecomputeSearch();
    update();
  }
  void FindNext() {
    if (search_matches_.empty()) {
      return;
    }
    search_current_ =
        (search_current_ + 1) % static_cast<int>(search_matches_.size());
    update();
  }
  void FindPrevious() {
    if (search_matches_.empty()) {
      return;
    }
    search_current_ = search_current_ <= 0
                          ? static_cast<int>(search_matches_.size()) - 1
                          : search_current_ - 1;
    update();
  }
  void ClearSearch() {
    search_query_.clear();
    search_matches_.clear();
    search_current_ = -1;
    update();
  }
  QRectF MessageRectForRow(qint64 row_id) const {
    if (row_id <= 0) {
      return QRectF();
    }
    for (const LaidMessage& message : messages_) {
      if (message.source.row_id == row_id) {
        return message.bounds;
      }
    }
    return QRectF();
  }
  QRectF CurrentMatchRect() const {
    if (search_current_ < 0 ||
        search_current_ >= static_cast<int>(search_matches_.size())) {
      return QRectF();
    }
    const std::size_t index = search_matches_[search_current_];
    if (index >= messages_.size()) {
      return QRectF();
    }
    return messages_[index].bounds;
  }
  int content_height() const { return content_height_; }
  void Rebuild();
  void RecomputeSearch() {
    search_matches_.clear();
    search_current_ = -1;
    if (search_query_.isEmpty()) {
      return;
    }
    for (std::size_t i = 0; i < messages_.size(); ++i) {
      if (MessageText(messages_[i].source)
              .contains(search_query_, Qt::CaseInsensitive)) {
        search_matches_.push_back(i);
      }
    }
    if (!search_matches_.empty()) {
      search_current_ = 0;
    }
  }
  std::vector<ClickTarget> ClickTargets() const {
    std::vector<ClickTarget> targets;
    for (std::size_t i = 0; i < messages_.size(); ++i) {
      if (!messages_[i].copy_rect.isNull()) {
        targets.push_back(
            {QStringLiteral("chat:copymsg:%1").arg(i), messages_[i].copy_rect});
      }
      if (!messages_[i].action_rect.isNull()) {
        targets.push_back({messages_[i].action_label == QStringLiteral("Edit")
                               ? QStringLiteral("chat:editmsg:%1").arg(i)
                               : QStringLiteral("chat:regenmsg:%1").arg(i),
                           messages_[i].action_rect});
      }
      if (!messages_[i].speak_rect.isNull()) {
        targets.push_back({QStringLiteral("chat:speakmsg:%1").arg(i),
                           messages_[i].speak_rect});
      }
      if (!messages_[i].branch_rect.isNull()) {
        targets.push_back({QStringLiteral("chat:branchmsg:%1").arg(i),
                           messages_[i].branch_rect});
      }
      if (!messages_[i].copy_full_rect.isNull()) {
        targets.push_back({QStringLiteral("chat:copyfullmsg:%1").arg(i),
                           messages_[i].copy_full_rect});
      }
      if (!messages_[i].react_rect.isNull()) {
        targets.push_back({QStringLiteral("chat:reactmsg:%1").arg(i),
                           messages_[i].react_rect});
      }
      if (!messages_[i].reasoning_header.isNull()) {
        targets.push_back({QStringLiteral("chat:reasoning:%1").arg(i),
                           messages_[i].reasoning_header});
      }
      for (std::size_t b = 0; b < messages_[i].blocks.size(); ++b) {
        const LaidBlock& block = messages_[i].blocks[b];
        if (block.source.kind == BlockKind::kCode) {
          targets.push_back({QStringLiteral("chat:copy:%1:%2").arg(i).arg(b),
                             CopyButtonRect(block)});
          if (IsPreviewableLanguage(block.source.language)) {
            targets.push_back(
                {QStringLiteral("chat:preview:%1:%2").arg(i).arg(b),
                 PreviewButtonRect(block), true});
          }
        } else if (block.source.kind == BlockKind::kTool) {
          targets.push_back({QStringLiteral("chat:tool:") + block.text,
                             QRectF(block.rect.left(), block.rect.top(),
                                    block.rect.width(), 28)});
        }
      }
    }
    return targets;
  }
  bool reasoning_expanded(std::size_t index) const {
    return index < messages_.size() && messages_[index].reasoning_expanded;
  }
  void SelectAllText();
  QString SelectedText() const;
  std::vector<std::pair<QRectF, QRectF>> BlockRectsForTest(
      BlockKind kind) const;
  QColor FirstCodeForegroundForTest() const;
  void SetContentMaxWidth(int pixels);
  void SetShowTimestamps(bool show);
  void SetReasoningCollapsedByDefault(bool collapsed);
  void SetAnimationsEnabled(bool enabled);
  void SetChatFontScale(double scale);
  void SetCodeFontScale(double scale);
  bool IsCaretAnimatingForTest() const;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;

 private:
  struct LaidBlock {
    MessageBlock source;
    QRectF rect;
    std::shared_ptr<QTextDocument> document;
    // Local image previews render in the transcript; remote URLs keep a card
    // until an image fetcher is wired in.
    QPixmap preview;
    // Tool rows carry their expansion key ("<message>:<block>") here.
    QString text;
    // A collapsed run of consecutive tool calls renders as one summary row.
    bool tool_group = false;
    int tool_count = 0;
    QString tool_names;
  };

  struct LaidMessage {
    ChatMessage source;
    std::size_t index = 0;
    // Per-message "Copy" chip (user and assistant bubbles).
    QRectF copy_rect;
    // Per-message Edit (user) / Retry (assistant) chip.
    QRectF action_rect;
    // Assistant hover action row, matching the desktop footer:
    // branch · copy · copy full · read aloud · refresh · react.
    QRectF action_row;
    QRectF branch_rect;
    QRectF copy_full_rect;
    QRectF speak_rect;
    QRectF react_rect;
    QString action_label;
    QRectF bounds;
    QRectF avatar;
    QRectF header;
    QRectF bubble;
    QRectF reasoning_header;
    QRectF reasoning_body;
    QRectF reaction_footer;
    QString reaction_text;
    std::shared_ptr<QTextDocument> bubble_document;
    std::shared_ptr<QTextDocument> reasoning_document;
    bool reasoning_expanded = false;
    std::vector<LaidBlock> blocks;
    bool is_user = false;
  };

  struct LayoutCache {
    int revision = -1;
    int width = 0;
    int content_height = 0;
    std::vector<LaidMessage> messages;
  };

  // Every painted text surface is either a QTextDocument (message bodies,
  // reasoning, code) or a single plain-text run (author/timestamp/reaction).
  // Keeping both in one ordered list makes drag selection cross block, code
  // and message boundaries without changing the paint path.
  struct TextRun {
    QRectF rect;
    std::shared_ptr<QTextDocument> document;
    QString plain;
    QFont font;
    Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignTop;
  };
  struct TextPosition {
    int run = -1;
    int offset = 0;
  };
  std::vector<TextRun> SelectableRuns() const;
  TextPosition TextPositionAt(const QPointF& position) const;
  static int DocumentOffsetAt(const QTextDocument& document,
                              const QPointF& local);
  static int PlainOffsetAt(const TextRun& run, const QPointF& position);
  static QString DocumentRange(const QTextDocument& document, int start,
                               int end);
  bool HasSelection() const;
  void ClearSelection();
  void PaintSelection(QPainter& painter) const;

  void BuildAssistantMessage(const ChatMessage& message, qreal width,
                             qreal start_y, std::size_t message_index,
                             LaidMessage* laid_out);
  static QString MessageText(const ChatMessage& message);
  void BuildSystemMessage(const ChatMessage& message, qreal width,
                          qreal start_y, LaidMessage* laid_out);
  void BuildUserMessage(const ChatMessage& message, qreal width, qreal start_y,
                        LaidMessage* laid_out);
  void PaintMessage(QPainter& painter, const LaidMessage& message) const;
  void PaintCodeBlock(QPainter& painter, const LaidBlock& block) const;
  // Rendered preview for generated SVG/RTF/HTML/Markdown/CSV blocks.
  void OpenCodePreview(const QString& text, const QString& language);
  void PaintImageBlock(QPainter& painter, const LaidBlock& block) const;
  void PaintArtifactBlock(QPainter& painter, const LaidBlock& block) const;
  void PaintToolRow(QPainter& painter, const LaidBlock& block) const;
  QRectF BlockTextRect(const LaidBlock& block) const;
  void PaintReasoning(QPainter& painter, const LaidMessage& message) const;
  void PaintReactionFooter(QPainter& painter, const LaidMessage& message) const;
  void PaintMessageActions(QPainter& painter, const LaidMessage& message) const;
  void LayoutMessageActions(LaidMessage* message, const ChatMessage& source);
  static QRectF CopyButtonRect(const LaidBlock& block);
  static QRectF PreviewButtonRect(const LaidBlock& block);
  static QString BodyText(const ChatMessage& message);
  static QString ActionTooltip(const QString& action);
  QFont MonoFont(int size, QFont::Weight weight) const;
  qreal CodeLineHeight() const;

  Theme theme_;
  Theme base_theme_;
  std::shared_ptr<const ProfileSnapshot> snapshot_;
  QString profile_name_;
  RequestBar* request_bar_ = nullptr;
  bool has_request_ = false;
  std::vector<LaidMessage> messages_;
  std::map<QString, LayoutCache> layout_cache_;
  // Collapsible reasoning state: key is "<profile>#<message index>", so it
  // survives a re-layout but not a profile switch (where indices change).
  std::map<QString, bool> reasoning_expanded_;
  QString copied_block_;
  int hovered_message_ = -1;
  QString hovered_action_;
  QString search_query_;
  std::vector<std::size_t> search_matches_;
  int search_current_ = -1;
  std::function<void(const ChatMessage&, const QString&)> edit_handler_;
  std::function<void(const ChatMessage&, const QString&)> regenerate_handler_;
  std::function<void(const ChatMessage&, const QString&)> branch_handler_;
  std::function<void(const ChatMessage&, const QString&)> rewind_handler_;
  std::function<void(const ChatMessage&, const QString&)> react_handler_;
  std::function<void()> delete_conversation_handler_;
  std::function<void(const ChatMessage&, const QString&)> read_aloud_handler_;
  std::function<void(const ChatMessage&, const QString&)> pin_handler_;
  std::set<QString> expanded_tools_;
  TextPosition selection_anchor_;
  TextPosition selection_cursor_;
  bool selecting_ = false;
  int content_height_ = 0;
  bool caret_visible_ = true;
  QTimer* caret_timer_ = nullptr;
  qreal content_max_width_ = kMaxContentWidth;
  bool show_timestamps_ = true;
  bool reasoning_collapsed_ = true;
  bool animations_enabled_ = true;
  double chat_font_scale_ = 1.0;
  double code_scale_ = 1.0;
};

TranscriptWidget::TranscriptWidget(const Theme& theme, QWidget* parent)
    : QWidget(parent), theme_(theme), base_theme_(theme) {
  request_bar_ = new RequestBar(theme_, this);
  request_bar_->hide();
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  caret_timer_ = new QTimer(this);
  caret_timer_->setInterval(530);
  connect(caret_timer_, &QTimer::timeout, this, [this]() {
    caret_visible_ = !caret_visible_;
    update();
  });
  caret_timer_->start();
}

void TranscriptWidget::SetTheme(const Theme& theme) {
  base_theme_ = theme;
  theme_ = theme;
  theme_.font_scale = base_theme_.font_scale * chat_font_scale_;
  if (request_bar_ != nullptr) {
    request_bar_->SetTheme(theme_);
  }
  layout_cache_.clear();
  Rebuild();
  update();
}

QFont TranscriptWidget::MonoFont(int size, QFont::Weight weight) const {
  // Code scales with UI scale and its own setting, not with chat text scale:
  // the two scales are independent controls on the Appearance page.
  Theme code_theme = theme_;
  code_theme.font_scale = base_theme_.font_scale * code_scale_;
  return code_theme.MonoFont(size, weight);
}

qreal TranscriptWidget::CodeLineHeight() const {
  return kCodeLineHeight * base_theme_.font_scale * code_scale_;
}

void TranscriptWidget::SetContentMaxWidth(int pixels) {
  const qreal next = std::clamp(pixels, 520, 1200);
  if (qFuzzyCompare(content_max_width_, next)) {
    return;
  }
  content_max_width_ = next;
  layout_cache_.clear();
  Rebuild();
  update();
}

void TranscriptWidget::SetShowTimestamps(bool show) {
  if (show_timestamps_ == show) {
    return;
  }
  show_timestamps_ = show;
  layout_cache_.clear();
  Rebuild();
  update();
}

void TranscriptWidget::SetReasoningCollapsedByDefault(bool collapsed) {
  if (reasoning_collapsed_ == collapsed) {
    return;
  }
  reasoning_collapsed_ = collapsed;
  reasoning_expanded_.clear();
  layout_cache_.clear();
  Rebuild();
  update();
}

void TranscriptWidget::SetAnimationsEnabled(bool enabled) {
  if (animations_enabled_ == enabled) {
    return;
  }
  animations_enabled_ = enabled;
  if (!animations_enabled_) {
    caret_timer_->stop();
    caret_visible_ = false;
  } else if (!caret_timer_->isActive()) {
    caret_visible_ = true;
    caret_timer_->start();
  }
  update();
}

void TranscriptWidget::SetChatFontScale(double scale) {
  const double next = std::clamp(scale, 0.8, 1.4);
  if (qFuzzyCompare(chat_font_scale_, next)) {
    return;
  }
  chat_font_scale_ = next;
  theme_.font_scale = base_theme_.font_scale * chat_font_scale_;
  if (request_bar_ != nullptr) {
    request_bar_->SetTheme(theme_);
  }
  layout_cache_.clear();
  Rebuild();
  update();
}

void TranscriptWidget::SetCodeFontScale(double scale) {
  const double next = std::clamp(scale, 0.8, 1.5);
  if (qFuzzyCompare(code_scale_, next)) {
    return;
  }
  code_scale_ = next;
  layout_cache_.clear();
  Rebuild();
  update();
}

bool TranscriptWidget::IsCaretAnimatingForTest() const {
  return animations_enabled_ && caret_timer_ != nullptr &&
         caret_timer_->isActive();
}

void TranscriptWidget::SetRequest(const PendingRequest& request) {
  has_request_ = true;
  const qreal width = std::max<qreal>(320, this->width());
  const qreal card_width =
      std::max<qreal>(280, width - kContentLeft - kRightPadding);
  request_bar_->setFixedWidth(static_cast<int>(card_width));
  request_bar_->SetRequest(request);
  request_bar_->show();
  layout_cache_.clear();
  Rebuild();
  update();
}

void TranscriptWidget::ClearRequest() {
  if (!has_request_) {
    return;
  }
  has_request_ = false;
  request_bar_->Clear();
  request_bar_->hide();
  layout_cache_.clear();
  Rebuild();
  update();
}

bool TranscriptWidget::HasConnectionRequest() const {
  return request_bar_ != nullptr && request_bar_->HasConnectionRequest();
}

bool TranscriptWidget::AnswerConnectionDisplay(const QString& text) {
  return request_bar_ != nullptr && request_bar_->AnswerConnectionDisplay(text);
}

bool TranscriptWidget::ContinueConnection() {
  return request_bar_ != nullptr && request_bar_->ContinueConnection();
}

void TranscriptWidget::SetRequestRespondHandler(
    std::function<void(const QString&, const QJsonObject&)> handler) {
  request_bar_->on_respond = std::move(handler);
}

QRectF TranscriptWidget::CopyButtonRect(const LaidBlock& block) {
  return QRectF(block.rect.right() - 34, block.rect.top(), 30,
                kCodeHeaderHeight);
}

QRectF TranscriptWidget::PreviewButtonRect(const LaidBlock& block) {
  return QRectF(block.rect.right() - 116, block.rect.top(), 56,
                kCodeHeaderHeight);
}

std::vector<TranscriptWidget::TextRun> TranscriptWidget::SelectableRuns()
    const {  // NOLINT(whitespace/indent_namespace)
  std::vector<TextRun> runs;
  const auto add_plain = [&runs](const QRectF& rect, const QString& text,
                                 const QFont& font, Qt::Alignment alignment) {
    if (rect.isNull() || text.isEmpty()) {
      return;
    }
    TextRun run;
    run.rect = rect;
    run.plain = text;
    run.font = font;
    run.alignment = alignment;
    runs.push_back(std::move(run));
  };
  const auto add_document = [&runs](const QRectF& rect,
                                    const std::shared_ptr<QTextDocument>& doc) {
    if (rect.isNull() || !doc) {
      return;
    }
    TextRun run;
    run.rect = rect;
    run.document = doc;
    runs.push_back(std::move(run));
  };

  for (const LaidMessage& message : messages_) {
    if (message.source.role == MessageRole::kSystem || message.is_user) {
      if (message.bubble_document) {
        const QPointF origin =
            message.is_user
                ? QPointF(message.bubble.left() + 12, message.bubble.top() + 10)
                : QPointF(message.bubble.left() + 13, message.bubble.top() + 6);
        add_document(QRectF(origin, message.bubble_document->size()),
                     message.bubble_document);
      }
      add_plain(message.reaction_footer, message.reaction_text,
                theme_.UiFont(10, QFont::Normal),
                Qt::AlignRight | Qt::AlignVCenter);
      continue;
    }

    if (!message.header.isNull()) {
      add_plain(message.header, message.source.author,
                theme_.UiFont(12, QFont::DemiBold),
                Qt::AlignLeft | Qt::AlignVCenter);
      const QFontMetrics author_metrics(theme_.UiFont(12, QFont::DemiBold));
      const qreal time_x =
          message.header.left() +
          author_metrics.horizontalAdvance(message.source.author) + 8;
      if (show_timestamps_) {
        add_plain(QRectF(time_x, message.header.top(),
                         std::max<qreal>(0, message.header.right() - time_x),
                         message.header.height()),
                  message.source.timestamp.toString("h:mm AP"),
                  theme_.UiFont(10, QFont::Normal),
                  Qt::AlignLeft | Qt::AlignVCenter);
      }
    }

    if (!message.reasoning_header.isNull()) {
      const QRectF header = message.reasoning_header;
      add_plain(
          QRectF(header.left() + 26, header.top(), header.width() - 34,
                 header.height()),
          message.source.reasoning_streaming ? QStringLiteral("Thinking...")
                                             : QStringLiteral("Thought"),
          theme_.UiFont(11, QFont::DemiBold), Qt::AlignLeft | Qt::AlignVCenter);
      add_plain(QRectF(header.right() - 90, header.top(), 82, header.height()),
                message.reasoning_expanded ? QStringLiteral("hide")
                                           : QStringLiteral("show"),
                theme_.UiFont(10, QFont::Normal),
                Qt::AlignRight | Qt::AlignVCenter);
    }

    if (message.reasoning_expanded && message.reasoning_document &&
        !message.reasoning_body.isNull()) {
      add_document(QRectF(message.reasoning_body.topLeft(),
                          message.reasoning_document->size()),
                   message.reasoning_document);
    }

    for (const LaidBlock& block : message.blocks) {
      const bool code_like = block.source.kind == BlockKind::kCode ||
                             block.source.kind == BlockKind::kDiff ||
                             block.source.kind == BlockKind::kMath ||
                             block.source.kind == BlockKind::kMermaid;
      if (code_like) {
        add_plain(QRectF(block.rect.left() + 12, block.rect.top(), 120,
                         kCodeHeaderHeight),
                  block.source.language.toUpper(),
                  theme_.UiFont(10, QFont::Normal),
                  Qt::AlignLeft | Qt::AlignVCenter);
        const QString copy_text =
            !copied_block_.isEmpty() && copied_block_ == block.source.text
                ? QStringLiteral("Copied")
                : QStringLiteral("Copy");
        add_plain(QRectF(block.rect.right() - 64, block.rect.top(), 36,
                         kCodeHeaderHeight),
                  copy_text, theme_.UiFont(10, QFont::Normal),
                  Qt::AlignRight | Qt::AlignVCenter);
        if (block.document) {
          add_document(QRectF(block.rect.left() + 12,
                              block.rect.top() + kCodeHeaderHeight + 7,
                              block.document->size().width(),
                              block.document->size().height()),
                       block.document);
        }
      } else if (block.source.kind == BlockKind::kImage) {
        if (block.preview.isNull()) {
          add_plain(QRectF(block.rect.left() + 44, block.rect.top() + 8,
                           block.rect.width() - 56, 18),
                    block.source.text.isEmpty() ? QStringLiteral("Image")
                                                : block.source.text,
                    theme_.UiFont(11, QFont::DemiBold),
                    Qt::AlignLeft | Qt::AlignVCenter);
          add_plain(QRectF(block.rect.left() + 44, block.rect.top() + 27,
                           block.rect.width() - 56, 16),
                    block.source.detail, MonoFont(10, QFont::Normal),
                    Qt::AlignLeft | Qt::AlignVCenter);
        }
      } else if (block.source.kind == BlockKind::kArtifact) {
        const QString title = block.source.language.isEmpty()
                                  ? QFileInfo(block.source.detail).fileName()
                                  : block.source.language;
        add_plain(QRectF(block.rect.left() + 44, block.rect.top() + 7,
                         block.rect.width() - 58, 18),
                  title.isEmpty() ? QStringLiteral("Artifact") : title,
                  theme_.UiFont(11, QFont::DemiBold),
                  Qt::AlignLeft | Qt::AlignVCenter);
        QString detail = block.source.detail;
        if (!block.source.text.isEmpty() && block.source.text != title) {
          detail = block.source.text + QStringLiteral(" · ") + detail;
        }
        add_plain(QRectF(block.rect.left() + 44, block.rect.top() + 26,
                         block.rect.width() - 58, 18),
                  detail, MonoFont(10, QFont::Normal),
                  Qt::AlignLeft | Qt::AlignVCenter);
      } else if (block.source.kind == BlockKind::kTool) {
        if (block.tool_group) {
          add_plain(QRectF(block.rect.left() + 28, block.rect.top(),
                           block.rect.width() - 56, 28),
                    QStringLiteral("%1 actions: %2")
                        .arg(block.tool_count)
                        .arg(block.tool_names),
                    theme_.UiFont(11, QFont::DemiBold),
                    Qt::AlignLeft | Qt::AlignVCenter);
        } else {
          add_plain(QRectF(block.rect.left() + 28, block.rect.top(), 90, 28),
                    block.source.text, theme_.UiFont(11, QFont::DemiBold),
                    Qt::AlignLeft | Qt::AlignVCenter);
          if (block.rect.height() <= 28.5) {
            QString summary = block.source.detail;
            summary.replace(QLatin1Char('\n'), QLatin1Char(' '));
            add_plain(QRectF(block.rect.left() + 118, block.rect.top(),
                             block.rect.width() - 150, 28),
                      summary, theme_.UiFont(11, QFont::Normal),
                      Qt::AlignLeft | Qt::AlignVCenter);
          }
          if (block.document && block.rect.height() > 28.5) {
            add_document(QRectF(block.rect.left() + 12, block.rect.top() + 32,
                                block.document->size().width(),
                                block.document->size().height()),
                         block.document);
          }
        }
      } else if (block.document) {
        add_document(BlockTextRect(block), block.document);
      }
    }

    add_plain(message.reaction_footer, message.reaction_text,
              theme_.UiFont(10, QFont::Normal),
              Qt::AlignRight | Qt::AlignVCenter);
  }
  return runs;
}

QRectF TranscriptWidget::BlockTextRect(const LaidBlock& block) const {
  if (block.document == nullptr) {
    return block.rect;
  }
  if (block.source.kind == BlockKind::kQuote) {
    return QRectF(block.rect.left() + kQuoteTextInsetLeft,
                  block.rect.top() + kQuoteTextInsetTop,
                  block.document->size().width(),
                  block.document->size().height());
  }
  if (block.source.kind == BlockKind::kCode ||
      block.source.kind == BlockKind::kDiff ||
      block.source.kind == BlockKind::kMath ||
      block.source.kind == BlockKind::kMermaid) {
    return QRectF(
        block.rect.left() + 12, block.rect.top() + kCodeHeaderHeight + 7,
        block.document->size().width(), block.document->size().height());
  }
  return QRectF(block.rect.topLeft(), block.document->size());
}

int TranscriptWidget::DocumentOffsetAt(const QTextDocument& document,
                                       const QPointF& local) {
  if (document.characterCount() <= 1) {
    return 0;
  }
  const qreal width = std::max<qreal>(1, document.size().width());
  const qreal height = std::max<qreal>(1, document.size().height());
  const QPointF hit(std::clamp(local.x(), qreal(0), width - 1),
                    std::clamp(local.y(), qreal(0), height - 1));
  int offset = document.documentLayout()->hitTest(hit, Qt::FuzzyHit);
  if (offset < 0) {
    offset = local.y() < 0 ? 0 : document.characterCount() - 1;
  }
  return std::clamp(offset, 0, document.characterCount() - 1);
}

int TranscriptWidget::PlainOffsetAt(const TextRun& run,
                                    const QPointF& position) {
  if (run.plain.isEmpty()) {
    return 0;
  }
  const QFontMetricsF metrics(run.font);
  const qreal advance = metrics.horizontalAdvance(run.plain);
  qreal start = run.rect.left();
  if (run.alignment & Qt::AlignRight) {
    start = run.rect.right() - advance;
  } else if (run.alignment & Qt::AlignHCenter) {
    start = run.rect.center().x() - advance / 2.0;
  }
  const qreal local = position.x() - start;
  if (local <= 0) {
    return 0;
  }
  if (local >= advance) {
    return run.plain.size();
  }
  for (int index = 1; index <= run.plain.size(); ++index) {
    if (metrics.horizontalAdvance(run.plain.left(index)) >= local) {
      return index - 1;
    }
  }
  return run.plain.size();
}

TranscriptWidget::TextPosition TranscriptWidget::TextPositionAt(
    const QPointF& position) const {
  const std::vector<TextRun> runs = SelectableRuns();
  int best = -1;
  qreal best_score = std::numeric_limits<qreal>::max();
  for (int index = 0; index < static_cast<int>(runs.size()); ++index) {
    const QRectF rect = runs[index].rect;
    const qreal dx =
        position.x() < rect.left()
            ? rect.left() - position.x()
            : (position.x() > rect.right() ? position.x() - rect.right() : 0);
    const qreal dy =
        position.y() < rect.top()
            ? rect.top() - position.y()
            : (position.y() > rect.bottom() ? position.y() - rect.bottom() : 0);
    const qreal score = dy * 8 + dx;
    if (score < best_score) {
      best_score = score;
      best = index;
    }
  }
  if (best < 0) {
    return {};
  }
  const TextRun& run = runs[best];
  TextPosition result;
  result.run = best;
  if (run.document) {
    result.offset =
        DocumentOffsetAt(*run.document, position - run.rect.topLeft());
  } else {
    result.offset = PlainOffsetAt(run, position);
  }
  return result;
}

QString TranscriptWidget::DocumentRange(const QTextDocument& document,
                                        int start, int end) {
  const int limit = std::max(0, document.characterCount() - 1);
  start = std::clamp(start, 0, limit);
  end = std::clamp(end, 0, limit);
  if (end < start) {
    std::swap(start, end);
  }
  QTextCursor cursor(const_cast<QTextDocument*>(&document));
  cursor.setPosition(start);
  cursor.setPosition(end, QTextCursor::KeepAnchor);
  QString text = cursor.selectedText();
  text.replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
  text.replace(QChar::LineSeparator, QLatin1Char('\n'));
  return text;
}

bool TranscriptWidget::HasSelection() const {
  return selection_anchor_.run >= 0 && selection_cursor_.run >= 0 &&
         (selection_anchor_.run != selection_cursor_.run ||
          selection_anchor_.offset != selection_cursor_.offset);
}

void TranscriptWidget::ClearSelection() {
  if (selection_anchor_.run < 0 && selection_cursor_.run < 0) {
    return;
  }
  selection_anchor_ = {};
  selection_cursor_ = {};
  selecting_ = false;
  update();
}

void TranscriptWidget::SelectAllText() {
  const std::vector<TextRun> runs = SelectableRuns();
  if (runs.empty()) {
    ClearSelection();
    return;
  }
  selection_anchor_ = {0, 0};
  const TextRun& last = runs.back();
  const int offset = last.document
                         ? std::max(0, last.document->characterCount() - 1)
                         : last.plain.size();
  selection_cursor_ = {static_cast<int>(runs.size()) - 1, offset};
  selecting_ = false;
  setFocus(Qt::OtherFocusReason);
  update();
}

QString TranscriptWidget::SelectedText() const {
  if (!HasSelection()) {
    return {};
  }
  TextPosition start = selection_anchor_;
  TextPosition end = selection_cursor_;
  if (start.run > end.run ||
      (start.run == end.run && start.offset > end.offset)) {
    std::swap(start, end);
  }
  const std::vector<TextRun> runs = SelectableRuns();
  QString text;
  for (int index = start.run;
       index <= end.run && index < static_cast<int>(runs.size()); ++index) {
    const TextRun& run = runs[index];
    const int length = run.document
                           ? std::max(0, run.document->characterCount() - 1)
                           : run.plain.size();
    int from = index == start.run ? start.offset : 0;
    int to = index == end.run ? end.offset : length;
    from = std::clamp(from, 0, length);
    to = std::clamp(to, 0, length);
    if (to <= from) {
      continue;
    }
    const QString piece = run.document ? DocumentRange(*run.document, from, to)
                                       : run.plain.mid(from, to - from);
    if (piece.isEmpty()) {
      continue;
    }
    if (!text.isEmpty()) {
      text += QLatin1Char('\n');
    }
    text += piece;
  }
  return text;
}

QColor TranscriptWidget::FirstCodeForegroundForTest() const {
  for (const LaidMessage& message : messages_) {
    for (const LaidBlock& block : message.blocks) {
      if (block.source.kind != BlockKind::kCode || !block.document) {
        continue;
      }
      const QTextBlock first = block.document->begin();
      if (first.begin() == first.end()) {
        continue;
      }
      return first.begin().fragment().charFormat().foreground().color();
    }
  }
  return QColor();
}

std::vector<std::pair<QRectF, QRectF>> TranscriptWidget::BlockRectsForTest(
    BlockKind kind) const {
  std::vector<std::pair<QRectF, QRectF>> rects;
  for (const LaidMessage& message : messages_) {
    for (const LaidBlock& block : message.blocks) {
      if (block.source.kind != kind) {
        continue;
      }
      rects.emplace_back(block.rect, BlockTextRect(block));
    }
  }
  return rects;
}

void TranscriptWidget::PaintSelection(QPainter& painter) const {
  if (!HasSelection()) {
    return;
  }
  TextPosition start = selection_anchor_;
  TextPosition end = selection_cursor_;
  if (start.run > end.run ||
      (start.run == end.run && start.offset > end.offset)) {
    std::swap(start, end);
  }
  const std::vector<TextRun> runs = SelectableRuns();
  QColor wash = theme_.accent;
  wash.setAlpha(80);
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(wash);
  for (int index = start.run;
       index <= end.run && index < static_cast<int>(runs.size()); ++index) {
    const TextRun& run = runs[index];
    const int length = run.document
                           ? std::max(0, run.document->characterCount() - 1)
                           : run.plain.size();
    int from = index == start.run ? start.offset : 0;
    int to = index == end.run ? end.offset : length;
    from = std::clamp(from, 0, length);
    to = std::clamp(to, 0, length);
    if (to <= from) {
      continue;
    }
    if (run.document) {
      QTextBlock block = run.document->findBlock(from);
      while (block.isValid() && block.position() < to) {
        const int block_start = block.position();
        const int block_end = block_start + block.length() - 1;
        const int line_from = std::max(from, block_start);
        const int line_to = std::min(to, block_end);
        const QTextLayout* layout = block.layout();
        if (layout != nullptr && line_from < line_to) {
          const QRectF block_rect =
              run.document->documentLayout()->blockBoundingRect(block);
          for (int line_index = 0; line_index < layout->lineCount();
               ++line_index) {
            const QTextLine line = layout->lineAt(line_index);
            const int text_start = block_start + line.textStart();
            const int text_end = text_start + line.textLength();
            const int a = std::max(line_from, text_start);
            const int b = std::min(line_to, text_end);
            if (a >= b) {
              continue;
            }
            const qreal x1 = line.cursorToX(a - text_start);
            const qreal x2 = line.cursorToX(b - text_start);
            if (x2 <= x1) {
              continue;
            }
            painter.drawRect(
                QRectF(run.rect.left() + x1,
                       run.rect.top() + block_rect.top() + line.y(), x2 - x1,
                       line.height()));
          }
        }
        block = block.next();
      }
    } else {
      const QFontMetricsF metrics(run.font);
      const qreal advance = metrics.horizontalAdvance(run.plain);
      qreal origin = run.rect.left();
      if (run.alignment & Qt::AlignRight) {
        origin = run.rect.right() - advance;
      } else if (run.alignment & Qt::AlignHCenter) {
        origin = run.rect.center().x() - advance / 2.0;
      }
      const qreal x1 = origin + metrics.horizontalAdvance(run.plain.left(from));
      const qreal x2 = origin + metrics.horizontalAdvance(run.plain.left(to));
      painter.drawRect(QRectF(x1, run.rect.top(), std::max<qreal>(1, x2 - x1),
                              run.rect.height()));
    }
  }
  painter.restore();
}

void TranscriptWidget::mouseMoveEvent(QMouseEvent* event) {
  const QPointF position = event->position();
  if (selecting_ && (event->buttons() & Qt::LeftButton)) {
    selection_cursor_ = TextPositionAt(position);
    update();
    event->accept();
    return;
  }
  int hovered = -1;
  QString action;
  for (std::size_t index = 0; index < messages_.size(); ++index) {
    const LaidMessage& message = messages_[index];
    const bool over_message =
        message.bounds.contains(position) &&
        (message.is_user || message.source.role == MessageRole::kSystem
             ? (message.bubble.contains(position) ||
                message.action_row.contains(position))
             : true);
    if (!over_message) {
      continue;
    }
    hovered = static_cast<int>(index);
    if (message.branch_rect.contains(position)) {
      action = QStringLiteral("branch");
    } else if (message.copy_rect.contains(position)) {
      action = QStringLiteral("copy");
    } else if (message.copy_full_rect.contains(position)) {
      action = QStringLiteral("copy_full");
    } else if (message.speak_rect.contains(position)) {
      action = QStringLiteral("speak");
    } else if (message.action_rect.contains(position)) {
      action = message.action_label == QStringLiteral("Edit")
                   ? QStringLiteral("edit")
                   : QStringLiteral("regenerate");
    } else if (message.react_rect.contains(position)) {
      action = QStringLiteral("react");
    }
    break;
  }
  const QString tooltip = action == QStringLiteral("edit")
                              ? QStringLiteral("Edit and resend")
                              : ActionTooltip(action);
  if (tooltip != toolTip()) {
    setToolTip(tooltip);
  }
  if (hovered != hovered_message_ || action != hovered_action_) {
    hovered_message_ = hovered;
    hovered_action_ = action;
    update();
  }
  QWidget::mouseMoveEvent(event);
}

void TranscriptWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    const QPointF position = event->position();
    for (std::size_t index = 0; index < messages_.size(); ++index) {
      const LaidMessage& message = messages_[index];
      if (!message.branch_rect.isNull() &&
          message.branch_rect.contains(position)) {
        if (branch_handler_) {
          branch_handler_(message.source, BodyText(message.source));
        }
        event->accept();
        return;
      }
      if (!message.copy_full_rect.isNull() &&
          message.copy_full_rect.contains(position)) {
        const QString text = MessageText(message.source);
        if (!text.isEmpty()) {
          QGuiApplication::clipboard()->setText(text);
          update();
        }
        event->accept();
        return;
      }
      if (!message.react_rect.isNull() &&
          message.react_rect.contains(position)) {
        if (react_handler_) {
          QMenu menu(this);
          QHash<QAction*, QString> reactions;
          const QStringList reaction_choices = {
              QStringLiteral("👍"), QStringLiteral("❤️"),  QStringLiteral("🎉"),
              QStringLiteral("👀"), QStringLiteral("✅"),
          };
          for (const QString& emoji : reaction_choices) {
            reactions.insert(menu.addAction(emoji), emoji);
          }
          QAction* chosen = menu.exec(event->globalPosition().toPoint());
          if (reactions.contains(chosen)) {
            react_handler_(message.source, reactions.value(chosen));
          }
        }
        event->accept();
        return;
      }
      if (!message.action_rect.isNull() &&
          message.action_rect.contains(position)) {
        const QString text = MessageText(message.source);
        if (message.action_label == QStringLiteral("Edit") && edit_handler_) {
          edit_handler_(message.source, text);
        } else if (message.action_label == QStringLiteral("Retry") &&
                   regenerate_handler_) {
          regenerate_handler_(message.source, text);
        }
        event->accept();
        return;
      }
      if (!message.speak_rect.isNull() &&
          message.speak_rect.contains(position)) {
        if (read_aloud_handler_) {
          read_aloud_handler_(message.source, MessageText(message.source));
        }
        event->accept();
        return;
      }
      if (!message.copy_rect.isNull() && message.copy_rect.contains(position)) {
        const QString text = MessageText(message.source);
        if (!text.isEmpty()) {
          QGuiApplication::clipboard()->setText(text);
          update();
        }
        event->accept();
        return;
      }
    }
    for (const LaidMessage& message : messages_) {
      for (const LaidBlock& block : message.blocks) {
        if (block.source.kind == BlockKind::kTool &&
            block.rect.height() >= 28 &&
            QRectF(block.rect.left(), block.rect.top(), block.rect.width(), 28)
                .contains(position)) {
          if (expanded_tools_.count(block.text) > 0) {
            expanded_tools_.erase(block.text);
          } else {
            expanded_tools_.insert(block.text);
          }
          layout_cache_.clear();
          Rebuild();
          update();
          event->accept();
          return;
        }
        if (block.source.kind == BlockKind::kCode &&
            IsPreviewableLanguage(block.source.language) &&
            PreviewButtonRect(block).contains(position)) {
          OpenCodePreview(block.source.text, block.source.language);
          event->accept();
          return;
        }
        if (block.source.kind == BlockKind::kCode &&
            CopyButtonRect(block).contains(position)) {
          QGuiApplication::clipboard()->setText(block.source.text);
          copied_block_ = block.source.text;
          QTimer::singleShot(1200, this, [this]() {
            copied_block_.clear();
            update();
          });
          update();
          event->accept();
          return;
        }
      }
    }
    for (std::size_t index = 0; index < messages_.size(); ++index) {
      const LaidMessage& message = messages_[index];
      if (!message.reasoning_header.isNull() &&
          message.reasoning_header.contains(position)) {
        const QString view_key = QStringLiteral("%1#%2")
                                     .arg(profile_name_)
                                     .arg(static_cast<qulonglong>(index));
        reasoning_expanded_[view_key] = !message.reasoning_expanded;
        layout_cache_.clear();
        Rebuild();
        update();
        event->accept();
        return;
      }
    }

    const TextPosition selected = TextPositionAt(position);
    if (selected.run >= 0) {
      selection_anchor_ = selected;
      selection_cursor_ = selected;
      selecting_ = true;
      setFocus(Qt::MouseFocusReason);
      update();
      event->accept();
      return;
    }
    ClearSelection();
  }
  QWidget::mousePressEvent(event);
}

void TranscriptWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && selecting_) {
    selection_cursor_ = TextPositionAt(event->position());
    selecting_ = false;
    update();
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void TranscriptWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  const TextPosition hit = TextPositionAt(event->position());
  if (hit.run < 0) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  const std::vector<TextRun> runs = SelectableRuns();
  if (hit.run >= static_cast<int>(runs.size())) {
    return;
  }
  const TextRun& run = runs[hit.run];
  if (run.document) {
    QTextCursor cursor(const_cast<QTextDocument*>(run.document.get()));
    cursor.setPosition(hit.offset);
    cursor.select(QTextCursor::WordUnderCursor);
    if (cursor.hasSelection()) {
      selection_anchor_ = {hit.run, cursor.selectionStart()};
      selection_cursor_ = {hit.run, cursor.selectionEnd()};
    }
  } else {
    auto is_word = [](QChar ch) {
      return ch.isLetterOrNumber() || ch == QLatin1Char('_') ||
             ch == QLatin1Char('-');
    };
    int start = std::clamp(hit.offset, 0, static_cast<int>(run.plain.size()));
    int end = start;
    while (start > 0 && is_word(run.plain.at(start - 1))) {
      --start;
    }
    while (end < run.plain.size() && is_word(run.plain.at(end))) {
      ++end;
    }
    selection_anchor_ = {hit.run, start};
    selection_cursor_ = {hit.run, end};
  }
  selecting_ = false;
  setFocus(Qt::MouseFocusReason);
  update();
  event->accept();
}

void TranscriptWidget::keyPressEvent(QKeyEvent* event) {
  if (event->matches(QKeySequence::Copy)) {
    const QString text = SelectedText();
    if (!text.isEmpty()) {
      QGuiApplication::clipboard()->setText(text);
      event->accept();
      return;
    }
  }
  if (event->matches(QKeySequence::SelectAll)) {
    SelectAllText();
    event->accept();
    return;
  }
  if (event->key() == Qt::Key_Escape && HasSelection()) {
    ClearSelection();
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

void TranscriptWidget::contextMenuEvent(QContextMenuEvent* event) {
  const QPointF position = event->pos();
  const LaidMessage* hit = nullptr;
  for (const LaidMessage& message : messages_) {
    if (message.bounds.contains(position)) {
      hit = &message;
      break;
    }
  }
  if (hit == nullptr) {
    return;
  }
  QString body;
  for (const LaidBlock& block : hit->blocks) {
    if (block.source.kind == BlockKind::kTool) {
      continue;
    }
    if (!body.isEmpty()) {
      body += QStringLiteral("\n\n");
    }
    body += block.source.text;
  }
  QMenu menu(this);
  QAction* copy_selection = nullptr;
  if (HasSelection()) {
    copy_selection = menu.addAction(QStringLiteral("Copy selection"));
    menu.addSeparator();
  }
  QAction* copy = menu.addAction(QStringLiteral("Copy message"));
  copy->setEnabled(!body.isEmpty());
  QAction* copy_reasoning = nullptr;
  if (!hit->source.reasoning.trimmed().isEmpty()) {
    copy_reasoning = menu.addAction(QStringLiteral("Copy reasoning"));
  }
  QAction* edit = nullptr;
  QAction* regenerate = nullptr;
  QAction* read_aloud = nullptr;
  QAction* pin = nullptr;
  QAction* branch = nullptr;
  QAction* rewind = nullptr;
  QAction* delete_conversation = nullptr;
  QMenu* react_menu = nullptr;
  QHash<QAction*, QString> reactions;
  if (hit->source.role != MessageRole::kSystem) {
    branch = menu.addAction(QStringLiteral("Branch from here"));
    react_menu = menu.addMenu(QStringLiteral("React"));
    for (const QString& emoji :
         {QStringLiteral("👍"), QStringLiteral("❤️"), QStringLiteral("🎉"),
          QStringLiteral("👀"), QStringLiteral("✅")}) {
      QAction* reaction = react_menu->addAction(emoji);
      reactions.insert(reaction, emoji);
    }
    if (hit->source.role == MessageRole::kAssistant) {
      rewind = menu.addAction(QStringLiteral("Rewind last turn"));
    }
    delete_conversation = menu.addAction(QStringLiteral("Delete conversation"));
  }
  if (hit->source.role == MessageRole::kAssistant && !body.isEmpty()) {
    read_aloud = menu.addAction(QStringLiteral("Read aloud"));
    pin = menu.addAction(QStringLiteral("Pin to context"));
  }
  if (hit->action_label == QStringLiteral("Edit")) {
    edit = menu.addAction(QStringLiteral("Edit and resend"));
  } else if (hit->action_label == QStringLiteral("Retry")) {
    regenerate = menu.addAction(QStringLiteral("Regenerate reply"));
  }
  QAction* chosen = menu.exec(event->globalPos());
  if (copy_selection != nullptr && chosen == copy_selection) {
    const QString text = SelectedText();
    if (!text.isEmpty()) {
      QGuiApplication::clipboard()->setText(text);
    }
  } else if (chosen == copy) {
    QGuiApplication::clipboard()->setText(body);
  } else if (copy_reasoning != nullptr && chosen == copy_reasoning) {
    QGuiApplication::clipboard()->setText(hit->source.reasoning);
  } else if (edit != nullptr && chosen == edit && edit_handler_) {
    edit_handler_(hit->source, body);
  } else if (regenerate != nullptr && chosen == regenerate &&
             regenerate_handler_) {
    regenerate_handler_(hit->source, body);
  } else if (read_aloud != nullptr && chosen == read_aloud &&
             read_aloud_handler_) {
    read_aloud_handler_(hit->source, body);
  } else if (pin != nullptr && chosen == pin && pin_handler_) {
    pin_handler_(hit->source, body);
  } else if (branch != nullptr && chosen == branch && branch_handler_) {
    branch_handler_(hit->source, body);
  } else if (rewind != nullptr && chosen == rewind && rewind_handler_) {
    rewind_handler_(hit->source, body);
  } else if (reactions.contains(chosen) && react_handler_) {
    react_handler_(hit->source, reactions.value(chosen));
  } else if (delete_conversation != nullptr && chosen == delete_conversation &&
             delete_conversation_handler_) {
    delete_conversation_handler_();
  }
  event->accept();
}

void TranscriptWidget::SetSnapshot(
    std::shared_ptr<const ProfileSnapshot> snapshot) {
  ClearSelection();
  const int width = std::max<qreal>(320, this->width());
  if (!snapshot) {
    snapshot_ = nullptr;
    messages_.clear();
    content_height_ = 0;
    setFixedHeight(0);
    search_matches_.clear();
    search_current_ = -1;
    update();
    return;
  }

  // A profile switch is a cache lookup when the snapshot has not changed and
  // the transcript width is the same. Copying the laid-out vector only copies
  // shared_ptr/QString handles; the QTextDocuments stay shared.
  const auto cached = layout_cache_.find(snapshot->name);
  if (cached != layout_cache_.end() &&
      cached->second.revision == snapshot->revision &&
      cached->second.width == width) {
    snapshot_ = std::move(snapshot);
    messages_ = cached->second.messages;
    content_height_ = cached->second.content_height;
    setFixedHeight(content_height_);
    RecomputeSearch();
    update();
    return;
  }

  snapshot_ = std::move(snapshot);
  Rebuild();
  RecomputeSearch();
  update();
}

void TranscriptWidget::SetProfileName(const QString& name) {
  profile_name_ = name;
}

void TranscriptWidget::Rebuild() {
  ClearSelection();
  messages_.clear();
  if (!snapshot_) {
    content_height_ = 0;
    setFixedHeight(0);
    return;
  }

  const qreal width = std::max<qreal>(320, this->width());
  qreal y = 26;
  for (std::size_t message_index = 0;
       message_index < snapshot_->messages.size(); ++message_index) {
    const ChatMessage& message = snapshot_->messages[message_index];
    LaidMessage laid_out;
    laid_out.source = message;
    laid_out.index = message_index;
    laid_out.is_user = message.role == MessageRole::kUser;
    if (message.role == MessageRole::kSystem) {
      BuildSystemMessage(message, width, y, &laid_out);
    } else if (laid_out.is_user) {
      BuildUserMessage(message, width, y, &laid_out);
    } else {
      BuildAssistantMessage(message, width, y, message_index, &laid_out);
    }
    laid_out.bounds = QRectF(0, y, width, laid_out.bounds.height());
    if (message.role != MessageRole::kSystem) {
      bool has_user_before = false;
      for (std::size_t prior = 0; prior < message_index; ++prior) {
        const ChatMessage& earlier = snapshot_->messages[prior];
        has_user_before =
            has_user_before ||
            (earlier.role == MessageRole::kUser && earlier.row_id > 0);
      }
      if (laid_out.is_user) {
        if (message.row_id > 0) {
          laid_out.action_label = QStringLiteral("Edit");
        }
      } else if (message.row_id > 0 && !message.streaming && has_user_before) {
        laid_out.action_label = QStringLiteral("Retry");
      }
    }
    LayoutMessageActions(&laid_out, message);
    messages_.push_back(std::move(laid_out));
    y += messages_.back().bounds.height() + kMessageGap;
  }
  if (has_request_) {
    const qreal card_width =
        std::max<qreal>(280, width - kContentLeft - kRightPadding);
    request_bar_->setFixedWidth(static_cast<int>(card_width));
    int card_height =
        request_bar_->heightForWidth(static_cast<int>(card_width));
    if (card_height <= 0) {
      card_height = request_bar_->height();
    }
    card_height = std::max(76, card_height);
    request_bar_->setGeometry(static_cast<int>(kContentLeft),
                              static_cast<int>(y), static_cast<int>(card_width),
                              card_height);
    request_bar_->show();
    y += card_height + 14;
  } else {
    request_bar_->hide();
  }
  content_height_ = static_cast<int>(y + 26);
  setFixedHeight(content_height_);

  if (snapshot_) {
    LayoutCache cache;
    cache.revision = snapshot_->revision;
    cache.width = static_cast<int>(width);
    cache.content_height = content_height_;
    cache.messages = messages_;
    layout_cache_[snapshot_->name] = std::move(cache);
    if (layout_cache_.size() > 8) {
      // Bound the layout cache: eight profiles of 120 messages is already a
      // lot of QTextDocuments. The oldest key is dropped first.
      layout_cache_.erase(layout_cache_.begin());
    }
  }
}

void TranscriptWidget::BuildAssistantMessage(const ChatMessage& message,
                                             qreal width, qreal start_y,
                                             std::size_t message_index,
                                             LaidMessage* laid_out) {
  const qreal content_width =
      std::min(content_max_width_, width - kContentLeft - kRightPadding);
  bool only_tools = !message.blocks.empty();
  for (const MessageBlock& block : message.blocks) {
    if (block.kind != BlockKind::kTool) {
      only_tools = false;
      break;
    }
  }
  const bool has_reasoning = !message.reasoning.trimmed().isEmpty();
  if (only_tools && !has_reasoning) {
    laid_out->avatar = QRectF();
    laid_out->header = QRectF();
  } else {
    laid_out->avatar = QRectF(28, start_y + 1, kAvatarSize, kAvatarSize);
    laid_out->header = QRectF(kContentLeft, start_y, content_width, 18);
  }

  qreal y = start_y + (only_tools && !has_reasoning ? 0 : 24);

  if (has_reasoning) {
    const QString view_key = QStringLiteral("%1#%2")
                                 .arg(profile_name_)
                                 .arg(static_cast<qulonglong>(message_index));
    const auto stored = reasoning_expanded_.find(view_key);
    // Reasoning follows the Appearance preference until the user explicitly
    // opens or closes a particular trace.
    const bool expanded = stored != reasoning_expanded_.end()
                              ? stored->second
                              : !reasoning_collapsed_;
    laid_out->reasoning_expanded = expanded;
    laid_out->reasoning_header = QRectF(kContentLeft, y, content_width, 26);
    y += 30;
    if (expanded) {
      const QColor muted = theme_.text_secondary;
      const QString html =
          QStringLiteral(
              "<div style=\"color:%1;font-size:12px;line-height:17px;\">%2"
              "</div>")
              .arg(muted.name(),
                   InlineHtml(message.reasoning)
                       .replace(QStringLiteral("\n"), QStringLiteral("<br/>")));
      QFont font = theme_.UiFont(12, QFont::Normal);
      font.setItalic(true);
      laid_out->reasoning_document =
          MakeDocument(html, font, content_width - 24, 17);
      const qreal body_height = laid_out->reasoning_document->size().height();
      laid_out->reasoning_body =
          QRectF(kContentLeft + 12, y, content_width - 24, body_height);
      y += body_height + 10;
    }
    y += 4;
  }

  for (std::size_t block_index = 0; block_index < message.blocks.size();
       ++block_index) {
    const MessageBlock& block = message.blocks[block_index];
    LaidBlock laid_block;
    laid_block.source = block;
    if (block.kind == BlockKind::kHeading) {
      const int level = qBound(1, block.detail.toInt(), 6);
      const int size = level == 1 ? 20 : (level == 2 ? 16 : 14);
      const qreal line_height = level == 1 ? 27 : (level == 2 ? 24 : 21);
      if (block_index > 0) {
        y += level == 1 ? 8 : 5;
      }
      QFont font = theme_.UiFont(size, QFont::DemiBold);
      const QString html =
          QStringLiteral("<div style=\"margin:0;color:%1;\">%2</div>")
              .arg(theme_.foreground.name(), InlineHtml(block.text));
      laid_block.document =
          MakeDocument(html, font, content_width, line_height);
      laid_block.rect = QRectF(kContentLeft, y, content_width,
                               laid_block.document->size().height());
      y += laid_block.rect.height() + (level == 1 ? 8 : 5);
    } else if (block.kind == BlockKind::kParagraph) {
      QFont font = theme_.UiFont(13, QFont::Normal);
      laid_block.document =
          MakeDocument(ParagraphHtml(block.text, theme_), font, content_width,
                       kParagraphLineHeight);
      laid_block.rect = QRectF(kContentLeft, y, content_width,
                               laid_block.document->size().height());
      y += laid_block.rect.height() + 10;
    } else if (block.kind == BlockKind::kBullet) {
      QFont font = theme_.UiFont(13, QFont::Normal);
      const qreal indent = block.detail.startsWith(QStringLiteral("task:"))
                               ? 22
                               : (block.detail.isEmpty() ? 14 : 24);
      const qreal bullet_width = content_width - indent;
      laid_block.document =
          MakeDocument(ParagraphHtml(block.text, theme_), font, bullet_width,
                       kParagraphLineHeight);
      laid_block.rect = QRectF(kContentLeft + indent, y, bullet_width,
                               laid_block.document->size().height());
      y += laid_block.rect.height() + 5;
    } else if (block.kind == BlockKind::kQuote) {
      QFont font = theme_.UiFont(13, QFont::Normal);
      QString escaped = InlineHtml(block.text);
      escaped.replace(QStringLiteral("\n"), QStringLiteral("<br/>"));
      const QString html =
          QStringLiteral("<div style=\"margin:0;color:%1;\">%2</div>")
              .arg(theme_.text_secondary.name(), escaped);
      const qreal quote_width = std::max<qreal>(
          1, content_width - kQuoteTextInsetLeft - kQuoteTextInsetRight);
      laid_block.document =
          MakeDocument(html, font, quote_width, kParagraphLineHeight);
      laid_block.rect = QRectF(kContentLeft, y, content_width,
                               laid_block.document->size().height() +
                                   kQuoteTextInsetTop + kQuoteTextInsetBottom);
      y += laid_block.rect.height() + 8;
    } else if (block.kind == BlockKind::kRule) {
      laid_block.rect = QRectF(kContentLeft, y, content_width, 1);
      y += 13;
    } else if (block.kind == BlockKind::kTable) {
      QFont font = theme_.UiFont(12, QFont::Normal);
      const QString html =
          QStringLiteral("<div style=\"color:%1;font-size:12px;\">%2</div>")
              .arg(theme_.foreground.name(), block.detail);
      laid_block.document =
          MakeDocument(html, font, content_width, kParagraphLineHeight);
      laid_block.rect = QRectF(kContentLeft, y, content_width,
                               laid_block.document->size().height());
      y += laid_block.rect.height() + 12;
    } else if (block.kind == BlockKind::kCode ||
               block.kind == BlockKind::kDiff ||
               block.kind == BlockKind::kMath ||
               block.kind == BlockKind::kMermaid) {
      if (block.kind == BlockKind::kDiff) {
        laid_block.source.language = QStringLiteral("diff");
      } else if (block.kind == BlockKind::kMath) {
        laid_block.source.language = QStringLiteral("math");
      } else if (block.kind == BlockKind::kMermaid) {
        laid_block.source.language = QStringLiteral("mermaid");
      } else if (laid_block.source.language.trimmed().isEmpty()) {
        // Fences often omit the language. Recognise self-describing payloads
        // so their Preview button still appears.
        const QString head = block.text.trimmed().left(64).toLower();
        if (head.startsWith(QStringLiteral("<svg"))) {
          laid_block.source.language = QStringLiteral("svg");
        } else if (head.startsWith(QStringLiteral("{\\rtf"))) {
          laid_block.source.language = QStringLiteral("rtf");
        } else if (head.startsWith(QStringLiteral("<!doctype html")) ||
                   head.startsWith(QStringLiteral("<html"))) {
          laid_block.source.language = QStringLiteral("html");
        }
      }
      QFont font = MonoFont(12, QFont::Normal);
      laid_block.document = MakeDocument(
          HighlightCode(block.text, laid_block.source.language, theme_), font,
          content_width - 24, CodeLineHeight());
      // Wrapped source lines make the laid-out document taller than the raw
      // newline count. Size the card from the document, not the source, or the
      // last lines paint outside the card.
      const qreal body_height = laid_block.document->size().height();
      const qreal code_height = kCodeHeaderHeight + body_height + 14;
      laid_block.rect = QRectF(kContentLeft, y, content_width, code_height);
      y += code_height + 11;
    } else if (block.kind == BlockKind::kImage) {
      const QUrl url(block.detail);
      QString path;
      if (url.isLocalFile()) {
        path = url.toLocalFile();
      } else if (url.scheme().isEmpty()) {
        path = block.detail;
      }
      QSize image_size;
      if (!path.isEmpty() && QFileInfo::exists(path)) {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        image_size = reader.size();
        if (image_size.isValid() && !image_size.isEmpty()) {
          const qreal scale = std::min<qreal>(
              1.0, std::min<qreal>(420.0 / image_size.width(),
                                   240.0 / image_size.height()));
          image_size = QSize(static_cast<int>(image_size.width() * scale),
                             static_cast<int>(image_size.height() * scale));
          QImage image = reader.read();
          if (!image.isNull()) {
            laid_block.preview = QPixmap::fromImage(image.scaled(
                image_size, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            image_size = laid_block.preview.size();
          }
        }
      }
      const qreal image_height =
          laid_block.preview.isNull() ? 52 : laid_block.preview.height();
      const qreal image_width =
          laid_block.preview.isNull()
              ? content_width
              : std::min<qreal>(content_width, laid_block.preview.width() + 20);
      laid_block.rect = QRectF(kContentLeft, y, image_width, image_height);
      y += image_height + 11;
    } else if (block.kind == BlockKind::kArtifact) {
      laid_block.rect = QRectF(kContentLeft, y, content_width, 52);
      y += 52 + 9;
    } else if (block.kind == BlockKind::kFootnote) {
      QFont font = theme_.UiFont(11, QFont::Normal);
      const QString html = ParagraphHtml(
          QStringLiteral("[%1] %2").arg(block.detail, block.text), theme_);
      laid_block.document = MakeDocument(html, font, content_width - 18, 16);
      laid_block.rect = QRectF(kContentLeft + 18, y, content_width - 18,
                               laid_block.document->size().height());
      y += laid_block.rect.height() + 5;
    } else if (block.kind == BlockKind::kTool) {
      // Consecutive tool calls collapse into one summary row by default. A
      // long agent turn can run dozens of tools; rendering each as its own
      // 28px row buried the answer and the next prompt.
      std::size_t run_end = block_index;
      while (run_end + 1 < message.blocks.size() &&
             message.blocks[run_end + 1].kind == BlockKind::kTool) {
        ++run_end;
      }
      const int run_size = static_cast<int>(run_end - block_index) + 1;
      if (run_size > 1) {
        const QString group_key =
            QStringLiteral("g%1:%2").arg(message_index).arg(block_index);
        QStringList names;
        for (std::size_t j = block_index; j <= run_end; ++j) {
          const QString name = message.blocks[j].text;
          if (!name.isEmpty() && !names.contains(name)) {
            names.append(name);
          }
        }
        LaidBlock summary;
        summary.source = block;
        summary.tool_group = true;
        summary.tool_count = run_size;
        summary.tool_names = names.join(QStringLiteral(", "));
        summary.text = group_key;
        summary.rect = QRectF(kContentLeft, y, content_width, 28);
        laid_out->blocks.push_back(std::move(summary));
        y += 28 + 7;

        if (expanded_tools_.count(group_key) == 0) {
          block_index = run_end;
          continue;
        }
        for (std::size_t j = block_index; j <= run_end; ++j) {
          const MessageBlock& tool = message.blocks[j];
          LaidBlock row;
          row.source = tool;
          const QString tool_key =
              QStringLiteral("%1:%2").arg(message_index).arg(j);
          const bool expanded = expanded_tools_.count(tool_key) > 0;
          qreal tool_height = 28;
          if (expanded && !tool.detail.isEmpty()) {
            QFont font = MonoFont(10, QFont::Normal);
            row.document = MakeDocument(
                HighlightCode(tool.detail, QStringLiteral("text"), theme_),
                font, content_width - 24, 15);
            tool_height += row.document->size().height() + 10;
          }
          row.rect = QRectF(kContentLeft, y, content_width, tool_height);
          row.text = tool_key;
          y += tool_height + 7;
          laid_out->blocks.push_back(std::move(row));
        }
        block_index = run_end;
        continue;
      }
      const QString tool_key =
          QStringLiteral("%1:%2").arg(message_index).arg(block_index);
      const bool expanded = expanded_tools_.count(tool_key) > 0;
      qreal tool_height = 28;
      if (expanded && !block.detail.isEmpty()) {
        QFont font = MonoFont(10, QFont::Normal);
        laid_block.document = MakeDocument(
            HighlightCode(block.detail, QStringLiteral("text"), theme_), font,
            content_width - 24, 15);
        tool_height += laid_block.document->size().height() + 10;
      }
      laid_block.rect = QRectF(kContentLeft, y, content_width, tool_height);
      laid_block.text = tool_key;
      y += tool_height + 7;
    }
    laid_out->blocks.push_back(std::move(laid_block));
  }
  // Reserve the desktop hover-action row even while it is invisible. Every
  // message item mounts the row so revealing it on hover never shifts the
  // transcript. Tool-only replies get the same copy/copy-full actions.
  laid_out->action_row =
      QRectF(kContentLeft, y + 2, content_width, kActionRowHeight);
  y += kActionRowHeight + 2;
  laid_out->bounds = QRectF(0, start_y, width, y - start_y);
  laid_out->reaction_text = ReactionFooterText(message);
  if (!laid_out->reaction_text.isEmpty()) {
    laid_out->reaction_footer = QRectF(kContentLeft, y + 2, content_width, 18);
    laid_out->bounds.setHeight(y + 22 - start_y);
  }
}

QString TranscriptWidget::MessageText(const ChatMessage& message) {
  QString text;
  for (const MessageBlock& block : message.blocks) {
    if (block.kind == BlockKind::kTool) {
      // A tool row's copyable text is its output, not its name.
      if (!block.detail.isEmpty()) {
        if (!text.isEmpty()) {
          text += QStringLiteral("\n\n");
        }
        text += block.detail;
      }
      continue;
    }
    if (!text.isEmpty()) {
      text += QStringLiteral("\n\n");
    }
    text += block.text;
  }
  return text;
}

QString TranscriptWidget::BodyText(const ChatMessage& message) {
  QString text;
  for (const MessageBlock& block : message.blocks) {
    if (block.kind == BlockKind::kTool) {
      continue;
    }
    if (!text.isEmpty()) {
      text += QStringLiteral("\n\n");
    }
    text += block.text;
  }
  return text;
}

QString TranscriptWidget::ActionTooltip(const QString& action) {
  if (action == QStringLiteral("branch")) {
    return QStringLiteral("Branch in new chat");
  }
  if (action == QStringLiteral("copy")) {
    return QStringLiteral("Copy");
  }
  if (action == QStringLiteral("copy_full")) {
    return QStringLiteral("Copy full response");
  }
  if (action == QStringLiteral("speak")) {
    return QStringLiteral("Read aloud");
  }
  if (action == QStringLiteral("regenerate")) {
    return QStringLiteral("Refresh");
  }
  if (action == QStringLiteral("react")) {
    return QStringLiteral("React");
  }
  return QString();
}

void TranscriptWidget::LayoutMessageActions(LaidMessage* message,
                                            const ChatMessage& source) {
  if (message == nullptr || message->action_row.isNull()) {
    return;
  }
  const QString body = BodyText(source);
  const bool system = source.role == MessageRole::kSystem;
  QStringList actions;
  if (!system) {
    actions.push_back(QStringLiteral("branch"));
  }
  if (!body.isEmpty()) {
    actions.push_back(QStringLiteral("copy"));
    actions.push_back(QStringLiteral("copy_full"));
    if (!system) {
      actions.push_back(QStringLiteral("speak"));
    }
  }
  if (message->action_label == QStringLiteral("Edit")) {
    actions.push_back(QStringLiteral("edit"));
  } else if (message->action_label == QStringLiteral("Retry")) {
    actions.push_back(QStringLiteral("regenerate"));
  }
  if (!system) {
    actions.push_back(QStringLiteral("react"));
  }

  qreal x = message->action_row.right() - actions.size() * kActionRowButton -
            (actions.size() - 1) * kActionRowGap;
  const qreal top = message->action_row.top() +
                    (message->action_row.height() - kActionRowButton) / 2.0;
  for (const QString& action : actions) {
    const QRectF button(x, top, kActionRowButton, kActionRowButton);
    if (action == QStringLiteral("branch")) {
      message->branch_rect = button;
    } else if (action == QStringLiteral("copy")) {
      message->copy_rect = button;
    } else if (action == QStringLiteral("copy_full")) {
      message->copy_full_rect = button;
    } else if (action == QStringLiteral("speak")) {
      message->speak_rect = button;
    } else if (action == QStringLiteral("edit") ||
               action == QStringLiteral("regenerate")) {
      message->action_rect = button;
    } else if (action == QStringLiteral("react")) {
      message->react_rect = button;
    }
    x += kActionRowButton + kActionRowGap;
  }
}

void TranscriptWidget::BuildSystemMessage(const ChatMessage& message,
                                          qreal width, qreal start_y,
                                          LaidMessage* laid_out) {
  // Slash-command feedback: a quiet centred pill so it reads as a note, not
  // as something the model said.
  const QString text = MessageText(message);
  QFont font = MonoFont(10, QFont::Normal);
  const qreal max_width = std::min<qreal>(560, width - 120);
  auto document =
      MakeDocument(ParagraphHtml(text, theme_), font, max_width - 24, 15);
  const qreal pill_width =
      std::min<qreal>(max_width, document->idealWidth() + 26);
  document->setTextWidth(pill_width - 26);
  const qreal pill_height = document->size().height() + 12;
  const qreal pill_x = (width - pill_width) / 2.0;
  laid_out->bubble = QRectF(pill_x, start_y + 2, pill_width, pill_height);
  laid_out->bubble_document = std::move(document);
  laid_out->action_row =
      QRectF(kContentLeft, start_y + pill_height + 6,
             width - kContentLeft - kRightPadding, kActionRowHeight);
  laid_out->bounds =
      QRectF(0, start_y, width, pill_height + 4 + kActionRowHeight + 4);
}

void TranscriptWidget::BuildUserMessage(const ChatMessage& message, qreal width,
                                        qreal start_y, LaidMessage* laid_out) {
  const qreal max_bubble_width = std::min<qreal>(640, width * 0.68);
  QString text;
  for (const MessageBlock& block : message.blocks) {
    if (!text.isEmpty()) {
      text += QStringLiteral("\n");
    }
    text += block.text;
  }
  QFont font = theme_.UiFont(13, QFont::Normal);
  const auto document =
      MakeDocument(ParagraphHtml(text, theme_), font, max_bubble_width - 24,
                   kParagraphLineHeight);
  const qreal bubble_width = std::min<qreal>(
      max_bubble_width, std::max<qreal>(140, document->idealWidth() + 24));
  const qreal bubble_height = document->size().height() + 20;
  const qreal bubble_x = width - kRightPadding - bubble_width;
  laid_out->bubble = QRectF(bubble_x, start_y, bubble_width, bubble_height);
  laid_out->bubble_document = document;
  laid_out->action_row =
      QRectF(kContentLeft, start_y + bubble_height + 4,
             width - kContentLeft - kRightPadding, kActionRowHeight);
  laid_out->reaction_text = ReactionFooterText(message);
  qreal bounds_height = bubble_height + kActionRowHeight + 6;
  if (!laid_out->reaction_text.isEmpty()) {
    laid_out->reaction_footer =
        QRectF(bubble_x, start_y + bubble_height + kActionRowHeight + 7,
               bubble_width, 17);
    bounds_height += 20;
  }
  laid_out->bounds = QRectF(0, start_y, width, bounds_height);
}

void TranscriptWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  // The transcript widget is much taller than its scroll viewport. Painting
  // every laid-out message made a profile switch cost scale with transcript
  // length; paint only the exposed band.
  const QRectF visible = event->rect();
  painter.fillRect(visible, theme_.background);

  if (!snapshot_) {
    return;
  }
  for (const LaidMessage& message : messages_) {
    if (!message.bounds.intersects(visible)) {
      continue;
    }
    PaintMessage(painter, message);
  }
  PaintSelection(painter);
}

void TranscriptWidget::PaintMessageActions(QPainter& painter,
                                           const LaidMessage& message) const {
  if (message.action_row.isNull()) {
    return;
  }
  const int index = static_cast<int>(&message - messages_.data());
  if (hovered_message_ != index) {
    return;
  }
  if (message.source.duration_secs >= 0) {
    painter.setFont(theme_.UiFont(10, QFont::Normal));
    painter.setPen(theme_.text_tertiary);
    painter.drawText(QRectF(message.action_row.left(), message.action_row.top(),
                            72, message.action_row.height()),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QStringLiteral("⏱ %1").arg(
                         FormatTurnDuration(message.source.duration_secs)));
  }

  const auto draw_icon = [&](const QRectF& rect, ushort codepoint,
                             const QString& action) {
    if (rect.isNull()) {
      return;
    }
    const bool active = hovered_action_ == action;
    if (active) {
      FillRounded(painter, rect, 5, theme_.hover);
    }
    DrawCodicon(painter, rect, codepoint,
                active ? theme_.accent : theme_.text_secondary, 13);
  };

  draw_icon(message.branch_rect, 0xEA63, QStringLiteral("branch"));
  draw_icon(message.copy_rect, 0xEBCC, QStringLiteral("copy"));
  draw_icon(message.copy_full_rect, 0xEBCC, QStringLiteral("copy_full"));
  draw_icon(message.speak_rect, 0xEB75, QStringLiteral("speak"));
  if (message.action_label == QStringLiteral("Edit")) {
    draw_icon(message.action_rect, 0xEA73, QStringLiteral("edit"));
  } else if (message.action_label == QStringLiteral("Retry")) {
    draw_icon(message.action_rect, 0xEB37, QStringLiteral("regenerate"));
  }
  if (!message.react_rect.isNull()) {
    const bool active = hovered_action_ == QStringLiteral("react");
    if (active) {
      FillRounded(painter, message.react_rect, 5, theme_.hover);
    }
    QString emoji;
    for (const ChatReaction& reaction : message.source.reactions) {
      if (!reaction.emoji.isEmpty()) {
        emoji = reaction.emoji;
        break;
      }
    }
    if (emoji.isEmpty()) {
      DrawCodicon(painter, message.react_rect, 0xEB54,
                  active ? theme_.accent : theme_.text_secondary, 13);
    } else {
      painter.setFont(theme_.UiFont(13, QFont::Normal));
      painter.setPen(theme_.foreground);
      painter.drawText(message.react_rect, Qt::AlignCenter, emoji);
    }
  }
}

void TranscriptWidget::PaintMessage(QPainter& painter,
                                    const LaidMessage& message) const {
  if (!search_matches_.empty()) {
    int match = -1;
    for (int i = 0; i < static_cast<int>(search_matches_.size()); ++i) {
      if (search_matches_[i] == message.index) {
        match = i;
        break;
      }
    }
    if (match >= 0) {
      painter.save();
      const bool current = match == search_current_;
      QColor wash = theme_.accent;
      wash.setAlpha(current ? 46 : 22);
      painter.setPen(Qt::NoPen);
      painter.setBrush(wash);
      painter.drawRoundedRect(message.bounds.adjusted(-6, -4, 6, 4), 8, 8);
      if (current) {
        QPen pen(theme_.accent);
        pen.setWidthF(1.5);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(message.bounds.adjusted(-6, -4, 6, 4), 8, 8);
      }
      painter.restore();
    }
  }
  if (message.source.role == MessageRole::kSystem) {
    FillRounded(painter, message.bubble, message.bubble.height() / 2.0,
                Mix(theme_.background, theme_.surface, 0.55));
    StrokeRounded(painter, message.bubble, message.bubble.height() / 2.0,
                  theme_.stroke);
    if (message.bubble_document) {
      painter.save();
      painter.translate(message.bubble.left() + 13, message.bubble.top() + 6);
      message.bubble_document->drawContents(&painter);
      painter.restore();
    }
    PaintMessageActions(painter, message);
    return;
  }
  if (message.is_user) {
    DrawSoftShadow(painter, message.bubble, theme_.radius_large, 10, 6);
    FillRounded(painter, message.bubble, theme_.radius_large,
                theme_.user_bubble);
    StrokeRounded(painter, message.bubble, theme_.radius_large, theme_.stroke);
    if (message.bubble_document) {
      painter.save();
      painter.translate(message.bubble.left() + 12, message.bubble.top() + 10);
      message.bubble_document->drawContents(&painter);
      painter.restore();
    }
    PaintMessageActions(painter, message);
    PaintReactionFooter(painter, message);
    return;
  }

  if (!message.header.isEmpty()) {
    FillRounded(painter, message.avatar, 6, theme_.accent_soft);
    DrawCodicon(painter, message.avatar, 0xEC10, theme_.accent, 12);

    QFont author_font = theme_.UiFont(12, QFont::DemiBold);
    painter.setFont(author_font);
    painter.setPen(theme_.foreground);
    painter.drawText(message.header, Qt::AlignVCenter | Qt::AlignLeft,
                     message.source.author);
    if (show_timestamps_) {
      const QFontMetrics author_metrics(author_font);
      const qreal time_x =
          message.header.left() +
          author_metrics.horizontalAdvance(message.source.author) + 8;
      painter.setFont(theme_.UiFont(10, QFont::Normal));
      painter.setPen(theme_.text_tertiary);
      painter.drawText(
          QRectF(time_x, message.header.top(), message.header.right() - time_x,
                 message.header.height()),
          Qt::AlignVCenter | Qt::AlignLeft,
          message.source.timestamp.toString("h:mm AP"));
    }
  }

  if (!message.reasoning_header.isNull()) {
    PaintReasoning(painter, message);
  }

  for (const LaidBlock& block : message.blocks) {
    if (block.source.kind == BlockKind::kCode ||
        block.source.kind == BlockKind::kDiff ||
        block.source.kind == BlockKind::kMath ||
        block.source.kind == BlockKind::kMermaid) {
      PaintCodeBlock(painter, block);
    } else if (block.source.kind == BlockKind::kImage) {
      PaintImageBlock(painter, block);
    } else if (block.source.kind == BlockKind::kArtifact) {
      PaintArtifactBlock(painter, block);
    } else if (block.source.kind == BlockKind::kTool) {
      PaintToolRow(painter, block);
    } else if (block.source.kind == BlockKind::kBullet) {
      if (block.source.detail.startsWith(QStringLiteral("task:"))) {
        const QRectF box(block.rect.left() - 16, block.rect.top() + 3, 10, 10);
        if (block.source.detail == QStringLiteral("task:done")) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(theme_.accent);
          painter.drawRoundedRect(box, 2, 2);
          painter.setPen(QPen(theme_.background, 1.4));
          painter.drawLine(box.left() + 2, box.center().y(), box.center().x(),
                           box.bottom() - 2);
          painter.drawLine(box.center().x(), box.bottom() - 2, box.right() - 2,
                           box.top() + 2);
        } else {
          painter.setPen(QPen(theme_.text_tertiary, 1));
          painter.setBrush(Qt::NoBrush);
          painter.drawRoundedRect(box, 2, 2);
        }
      } else if (block.source.detail.isEmpty()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme_.accent);
        painter.drawEllipse(
            QPointF(block.rect.left() - 9, block.rect.top() + 9), 2, 2);
      } else {
        painter.setPen(theme_.text_secondary);
        painter.setFont(theme_.UiFont(12, QFont::Normal));
        painter.drawText(QRectF(block.rect.left() - 22, block.rect.top(), 16,
                                block.rect.height()),
                         Qt::AlignRight | Qt::AlignTop, block.source.detail);
      }
      if (block.document) {
        painter.save();
        painter.translate(block.rect.topLeft());
        block.document->drawContents(&painter);
        painter.restore();
      }
    } else if (block.source.kind == BlockKind::kQuote) {
      FillRounded(painter, block.rect, kQuoteRadius,
                  Mix(theme_.background, theme_.surface, 0.72));
      StrokeRounded(painter, block.rect, kQuoteRadius, theme_.stroke);
      painter.setPen(Qt::NoPen);
      painter.setBrush(theme_.text_tertiary);
      painter.drawRoundedRect(
          QRectF(block.rect.left() + kQuoteBarInset,
                 block.rect.top() + kQuoteTextInsetTop, kQuoteBarWidth,
                 std::max<qreal>(8, block.rect.height() - kQuoteTextInsetTop -
                                        kQuoteTextInsetBottom)),
          kQuoteBarWidth / 2.0, kQuoteBarWidth / 2.0);
      if (block.document) {
        painter.save();
        painter.translate(BlockTextRect(block).topLeft());
        block.document->drawContents(&painter);
        painter.restore();
      }
    } else if (block.source.kind == BlockKind::kRule) {
      painter.setPen(QPen(theme_.stroke, 1));
      painter.drawLine(QPointF(block.rect.left(), block.rect.top()),
                       QPointF(block.rect.right(), block.rect.top()));
    } else if (block.document) {
      painter.save();
      painter.translate(block.rect.topLeft());
      block.document->drawContents(&painter);
      painter.restore();
    }
  }

  if (message.source.streaming && caret_visible_ && !message.blocks.empty()) {
    const LaidBlock& last = message.blocks.back();
    if (last.document) {
      DrawStreamingCaret(painter, *last.document, BlockTextRect(last),
                         theme_.accent);
    }
  }
  PaintMessageActions(painter, message);
  PaintReactionFooter(painter, message);
}

void TranscriptWidget::PaintReactionFooter(QPainter& painter,
                                           const LaidMessage& message) const {
  if (message.reaction_footer.isNull() || message.reaction_text.isEmpty()) {
    return;
  }
  painter.setFont(theme_.UiFont(10, QFont::Normal));
  painter.setPen(theme_.text_tertiary);
  painter.drawText(message.reaction_footer, Qt::AlignVCenter | Qt::AlignRight,
                   message.reaction_text);
}

void TranscriptWidget::PaintReasoning(QPainter& painter,
                                      const LaidMessage& message) const {
  const QRectF header = message.reasoning_header;
  QRectF panel = header;
  if (message.reasoning_expanded && !message.reasoning_body.isNull()) {
    panel.setBottom(message.reasoning_body.bottom() + 10);
  }
  FillRounded(painter, panel, 8, Mix(theme_.background, theme_.surface, 0.55));
  StrokeRounded(painter, panel, 8, theme_.stroke);

  DrawCodicon(painter, QRectF(header.left() + 8, header.top(), 14, 26),
              message.reasoning_expanded ? 0xEAB4 : 0xEAB6,
              theme_.text_tertiary, 12);
  painter.setFont(theme_.UiFont(11, QFont::DemiBold));
  painter.setPen(theme_.text_tertiary);
  painter.drawText(QRectF(header.left() + 26, header.top(), header.width() - 34,
                          header.height()),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   message.source.reasoning_streaming
                       ? QStringLiteral("Thinking...")
                       : QStringLiteral("Thought"));
  painter.setFont(theme_.UiFont(10, QFont::Normal));
  painter.setPen(theme_.text_tertiary);
  painter.drawText(
      QRectF(header.right() - 90, header.top(), 82, header.height()),
      Qt::AlignVCenter | Qt::AlignRight,
      message.reasoning_expanded ? QStringLiteral("hide")
                                 : QStringLiteral("show"));

  if (message.reasoning_expanded && message.reasoning_document) {
    painter.fillRect(QRectF(header.left() + 3, header.top() + 8, 2,
                            panel.bottom() - header.top() - 16),
                     theme_.accent);
    painter.save();
    painter.translate(message.reasoning_body.topLeft());
    message.reasoning_document->drawContents(&painter);
    painter.restore();
  }
}

void TranscriptWidget::PaintCodeBlock(QPainter& painter,
                                      const LaidBlock& block) const {
  DrawSoftShadow(painter, block.rect, 8, 8, 5);
  FillRounded(painter, block.rect, 8, theme_.code_background);
  StrokeRounded(painter, block.rect, 8, theme_.stroke);

  painter.save();
  QPainterPath clip;
  clip.addRoundedRect(block.rect, 8, 8);
  painter.setClipPath(clip);
  painter.fillRect(QRectF(block.rect.left(), block.rect.top(),
                          block.rect.width(), kCodeHeaderHeight),
                   theme_.code_header);
  painter.restore();

  painter.setFont(theme_.UiFont(10, QFont::DemiBold));
  painter.setPen(theme_.code_comment);
  painter.drawText(
      QRectF(block.rect.left() + 12, block.rect.top(), 120, kCodeHeaderHeight),
      Qt::AlignVCenter | Qt::AlignLeft, block.source.language.toUpper());

  const bool copied =
      !copied_block_.isEmpty() && copied_block_ == block.source.text;
  painter.setFont(theme_.UiFont(10, QFont::Normal));
  painter.setPen(theme_.code_comment);
  if (IsPreviewableLanguage(block.source.language)) {
    painter.drawText(QRectF(block.rect.right() - 116, block.rect.top(), 56,
                            kCodeHeaderHeight),
                     Qt::AlignVCenter | Qt::AlignRight,
                     QStringLiteral("Preview"));
  }
  painter.setPen(copied ? theme_.accent : theme_.code_comment);
  painter.drawText(
      QRectF(block.rect.right() - 64, block.rect.top(), 36, kCodeHeaderHeight),
      Qt::AlignVCenter | Qt::AlignRight,
      copied ? QStringLiteral("Copied") : QStringLiteral("Copy"));

  if (block.document) {
    painter.save();
    QPainterPath body_clip;
    body_clip.addRoundedRect(block.rect, 8, 8);
    painter.setClipPath(body_clip);
    painter.translate(block.rect.left() + 12,
                      block.rect.top() + kCodeHeaderHeight + 7);
    block.document->drawContents(&painter);
    painter.restore();
  }
}

void TranscriptWidget::OpenCodePreview(const QString& text,
                                       const QString& language) {
  const QString lang = language.trimmed().toLower();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("codePreviewDialog"));
  dialog.setWindowTitle(QStringLiteral("Preview · %1").arg(language.toUpper()));
  dialog.resize(760, 580);
  dialog.setStyleSheet(QStringLiteral("QDialog { background: %1; }")
                           .arg(theme_.background.name()));
  auto* layout = new QVBoxLayout(&dialog);

  const auto add_browser = [&](const QString& html) {
    auto* browser = new QTextBrowser(&dialog);
    browser->setObjectName(QStringLiteral("codePreviewBrowser"));
    browser->setOpenExternalLinks(true);
    browser->setStyleSheet(
        QStringLiteral("QTextBrowser { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 6px; "
                       "padding: 10px; }")
            .arg(theme_.surface.name(), theme_.foreground.name(),
                 theme_.stroke.name()));
    browser->setHtml(html);
    layout->addWidget(browser, 1);
  };

  bool rendered = false;
  if (lang == QStringLiteral("svg")) {
    QPixmap pixmap;
    if (!pixmap.loadFromData(text.toUtf8(), "SVG") &&
        !pixmap.loadFromData(text.toUtf8(), "svg")) {
      pixmap = QPixmap();
    }
    if (!pixmap.isNull()) {
      if (pixmap.width() > 680 || pixmap.height() > 470) {
        pixmap = pixmap.scaled(680, 470, Qt::KeepAspectRatio,
                               Qt::SmoothTransformation);
      }
      auto* label = new QLabel(&dialog);
      label->setObjectName(QStringLiteral("codePreviewImage"));
      label->setAlignment(Qt::AlignCenter);
      label->setPixmap(pixmap);
      layout->addWidget(label, 1);
      rendered = true;
    }
  } else if (lang == QStringLiteral("rtf")) {
    QTemporaryFile source(QDir::tempPath() +
                          QStringLiteral("/hermes-preview-XXXXXX.rtf"));
    if (source.open()) {
      source.write(text.toUtf8());
      source.flush();
      QProcess converter;
      converter.start(QStringLiteral("/usr/bin/textutil"),
                      {QStringLiteral("-convert"), QStringLiteral("html"),
                       QStringLiteral("-stdout"), source.fileName()});
      if (converter.waitForFinished(4000) &&
          converter.exitStatus() == QProcess::NormalExit &&
          converter.exitCode() == 0) {
        add_browser(QString::fromUtf8(converter.readAllStandardOutput()));
        rendered = true;
      }
    }
  } else if (lang == QStringLiteral("html") || lang == QStringLiteral("htm")) {
    add_browser(text);
    rendered = true;
  } else if (lang == QStringLiteral("markdown") ||
             lang == QStringLiteral("md")) {
    auto* browser = new QTextBrowser(&dialog);
    browser->setObjectName(QStringLiteral("codePreviewBrowser"));
    browser->setStyleSheet(
        QStringLiteral("QTextBrowser { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 6px; "
                       "padding: 10px; }")
            .arg(theme_.surface.name(), theme_.foreground.name(),
                 theme_.stroke.name()));
    browser->setMarkdown(text);
    layout->addWidget(browser, 1);
    rendered = true;
  } else if (lang == QStringLiteral("csv")) {
    const QStringList lines = text.split('\n');
    QString table = QStringLiteral(
        "<table cellspacing='0' cellpadding='5' border='1' "
        "style='border-collapse:collapse;'>");
    const int rows = std::min(static_cast<int>(lines.size()), 500);
    for (int row = 0; row < rows; ++row) {
      const QStringList cells = lines[row].split(QLatin1Char(','));
      table += row == 0 ? QStringLiteral("<tr style='font-weight:600;'>")
                        : QStringLiteral("<tr>");
      for (const QString& raw : cells) {
        QString cell = raw;
        cell.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
        cell.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
        cell.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
        table += QStringLiteral("<td>%1</td>").arg(cell);
      }
      table += QStringLiteral("</tr>");
    }
    table += QStringLiteral("</table>");
    add_browser(table);
    rendered = true;
  }

  if (!rendered) {
    auto* fallback = new QPlainTextEdit(&dialog);
    fallback->setObjectName(QStringLiteral("codePreviewText"));
    fallback->setReadOnly(true);
    fallback->setPlainText(text);
    layout->addWidget(fallback, 1);
  }

  auto* buttons = new QHBoxLayout();
  buttons->addStretch(1);
  auto* close = new QPushButton(QStringLiteral("Close"), &dialog);
  close->setObjectName(QStringLiteral("codePreviewClose"));
  QObject::connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
  buttons->addWidget(close);
  layout->addLayout(buttons);
  dialog.exec();
}

void TranscriptWidget::PaintImageBlock(QPainter& painter,
                                       const LaidBlock& block) const {
  DrawSoftShadow(painter, block.rect, 8, 8, 5);
  FillRounded(painter, block.rect, 8, theme_.surface);
  StrokeRounded(painter, block.rect, 8, theme_.stroke);
  if (!block.preview.isNull()) {
    painter.save();
    QPainterPath clip;
    clip.addRoundedRect(block.rect, 8, 8);
    painter.setClipPath(clip);
    const QPixmap scaled =
        block.preview.scaled(block.rect.size().toSize(), Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    const QPointF origin(
        block.rect.left() + (block.rect.width() - scaled.width()) / 2.0,
        block.rect.top() + (block.rect.height() - scaled.height()) / 2.0);
    painter.drawPixmap(origin, scaled);
    painter.restore();
    return;
  }
  DrawCodicon(painter,
              QRectF(block.rect.left() + 12, block.rect.top() + 14, 22, 22),
              0xEB9F, theme_.text_tertiary, 14);
  painter.setFont(theme_.UiFont(11, QFont::DemiBold));
  painter.setPen(theme_.text_secondary);
  painter.drawText(QRectF(block.rect.left() + 44, block.rect.top() + 8,
                          block.rect.width() - 56, 18),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   block.source.text.isEmpty() ? QStringLiteral("Image")
                                               : block.source.text);
  painter.setFont(MonoFont(10, QFont::Normal));
  painter.setPen(theme_.text_tertiary);
  painter.drawText(QRectF(block.rect.left() + 44, block.rect.top() + 27,
                          block.rect.width() - 56, 16),
                   Qt::AlignVCenter | Qt::AlignLeft, block.source.detail);
}

void TranscriptWidget::PaintArtifactBlock(QPainter& painter,
                                          const LaidBlock& block) const {
  FillRounded(painter, block.rect, 8,
              Mix(theme_.background, theme_.accent_soft, 0.45));
  StrokeRounded(painter, block.rect, 8, theme_.stroke);
  DrawCodicon(painter,
              QRectF(block.rect.left() + 12, block.rect.top() + 14, 22, 22),
              0xEA7B, theme_.accent, 14);
  const QString title = block.source.language.isEmpty()
                            ? QFileInfo(block.source.detail).fileName()
                            : block.source.language;
  painter.setFont(theme_.UiFont(11, QFont::DemiBold));
  painter.setPen(theme_.foreground);
  painter.drawText(QRectF(block.rect.left() + 44, block.rect.top() + 7,
                          block.rect.width() - 58, 18),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   title.isEmpty() ? QStringLiteral("Artifact") : title);
  painter.setFont(MonoFont(10, QFont::Normal));
  painter.setPen(theme_.text_tertiary);
  QString detail = block.source.detail;
  if (!block.source.text.isEmpty() && block.source.text != title) {
    detail = block.source.text + QStringLiteral(" · ") + detail;
  }
  painter.drawText(QRectF(block.rect.left() + 44, block.rect.top() + 26,
                          block.rect.width() - 58, 18),
                   Qt::AlignVCenter | Qt::AlignLeft, detail);
}

namespace {

// Distinct codicons per tool family, so a transcript scan separates a shell
// command from a file edit or an image inspection instead of showing the same
// generic file glyph for every action.
ushort ToolIconFor(const QString& name) {
  const QString tool = name.toLower();
  const auto has = [&tool](const char* needle) {
    return tool.contains(QLatin1String(needle));
  };
  if (has("terminal") || has("shell") || has("bash") || has("exec")) {
    return 0xEA85;  // terminal
  }
  if (has("patch") || has("edit") || has("write") || has("apply")) {
    return 0xEA73;  // edit
  }
  if (has("vision") || has("image") || has("screenshot") || has("view")) {
    return 0xEA70;  // eye
  }
  if (has("search") || has("grep") || has("find") || has("query")) {
    return 0xEA6D;  // search
  }
  if (has("git")) {
    return 0xEC6F;  // git-branch
  }
  if (has("web") || has("browser") || has("fetch") || has("http")) {
    return 0xEB7F;  // browser
  }
  if (has("file") || has("read") || has("list") || has("dir")) {
    return 0xEAF0;  // files
  }
  if (has("tool") || has("delegate") || has("task") || has("agent")) {
    return 0xEB6D;  // tools
  }
  return 0xEA7B;  // generic file/document
}

// Languages whose fenced block can be rendered rather than only copied.
bool IsPreviewableLanguage(const QString& language) {
  const QString lang = language.trimmed().toLower();
  return lang == QStringLiteral("svg") || lang == QStringLiteral("rtf") ||
         lang == QStringLiteral("html") || lang == QStringLiteral("htm") ||
         lang == QStringLiteral("markdown") || lang == QStringLiteral("md") ||
         lang == QStringLiteral("csv");
}

}  // namespace

void TranscriptWidget::PaintToolRow(QPainter& painter,
                                    const LaidBlock& block) const {
  FillRounded(painter, block.rect, 6,
              Mix(theme_.background, theme_.surface, 0.72));
  StrokeRounded(painter, block.rect, 6, theme_.stroke);
  if (block.tool_group) {
    DrawCodicon(painter,
                QRectF(block.rect.left() + 8, block.rect.top(), 18, 28), 0xEB6D,
                theme_.text_secondary, 12);
    painter.setFont(theme_.UiFont(11, QFont::DemiBold));
    painter.setPen(theme_.text_secondary);
    const QString label =
        block.tool_count == 1
            ? QStringLiteral("1 action")
            : QStringLiteral("%1 actions").arg(block.tool_count);
    const qreal label_width = QFontMetricsF(theme_.UiFont(11, QFont::DemiBold))
                                  .horizontalAdvance(label) +
                              8;
    painter.drawText(
        QRectF(block.rect.left() + 28, block.rect.top(), label_width, 28),
        Qt::AlignVCenter | Qt::AlignLeft, label);
    DrawElidedText(
        painter,
        QRectF(block.rect.left() + 30 + label_width, block.rect.top(),
               block.rect.width() - label_width - 62, 28),
        Qt::AlignVCenter | Qt::AlignLeft, block.tool_names,
        theme_.text_tertiary, theme_.UiFont(11, QFont::Normal));
    const bool expanded = expanded_tools_.count(block.text) > 0;
    DrawCodicon(painter,
                QRectF(block.rect.right() - 24, block.rect.top(), 16, 28),
                expanded ? 0xEAB4 : 0xEAB6, theme_.text_tertiary, 11);
    return;
  }
  DrawCodicon(painter, QRectF(block.rect.left() + 8, block.rect.top(), 18, 28),
              ToolIconFor(block.source.text), theme_.text_tertiary, 11);
  QFont name_font = theme_.UiFont(11, QFont::DemiBold);
  painter.setFont(name_font);
  painter.setPen(theme_.text_secondary);
  painter.drawText(QRectF(block.rect.left() + 28, block.rect.top(), 90, 28),
                   Qt::AlignVCenter | Qt::AlignLeft, block.source.text);
  const bool expanded = block.rect.height() > 28.5;
  if (!expanded) {
    QString summary = block.source.detail;
    summary.replace(QLatin1Char('\n'), QLatin1Char(' '));
    DrawElidedText(painter,
                   QRectF(block.rect.left() + 118, block.rect.top(),
                          block.rect.width() - 150, 28),
                   Qt::AlignVCenter | Qt::AlignLeft, summary,
                   theme_.text_tertiary, theme_.UiFont(11, QFont::Normal));
  }
  DrawCodicon(painter,
              QRectF(block.rect.right() - 24, block.rect.top(), 16, 28),
              expanded ? 0xEAB4 : 0xEAB6, theme_.text_tertiary, 11);
  if (expanded && block.document) {
    painter.save();
    painter.translate(block.rect.left() + 12, block.rect.top() + 32);
    block.document->drawContents(&painter);
    painter.restore();
  }
}

ChatView::ChatView(const Theme& theme, QWidget* parent)
    : QScrollArea(parent), theme_(theme) {
  setFrameShape(QFrame::NoFrame);
  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  setWidgetResizable(true);
  transcript_ = new TranscriptWidget(theme_, this);
  setWidget(transcript_);
  SetTheme(theme_);
}

std::vector<ClickTarget> ChatView::ClickTargets() const {
  return transcript_ == nullptr ? std::vector<ClickTarget>()
                                : transcript_->ClickTargets();
}

std::vector<std::pair<QRectF, QRectF>> ChatView::BlockRectsForTest(
    BlockKind kind) const {
  return transcript_->BlockRectsForTest(kind);
}

QColor ChatView::FirstCodeForegroundForTest() const {
  return transcript_ == nullptr ? QColor()
                                : transcript_->FirstCodeForegroundForTest();
}

void ChatView::SelectAllTextForTest() {
  if (transcript_ != nullptr) {
    transcript_->SelectAllText();
  }
}

QString ChatView::SelectedTextForTest() const {
  return transcript_ == nullptr ? QString() : transcript_->SelectedText();
}

bool ChatView::ReasoningExpanded(int message_index) const {
  if (transcript_ == nullptr || message_index < 0) {
    return false;
  }
  return transcript_->reasoning_expanded(
      static_cast<std::size_t>(message_index));
}

void ChatView::SetTheme(const Theme& theme) {
  theme_ = theme;
  QPalette palette = this->palette();
  palette.setColor(QPalette::Window, theme_.background);
  palette.setColor(QPalette::Base, theme_.background);
  setPalette(palette);
  setAutoFillBackground(true);
  viewport()->setAutoFillBackground(false);
  setStyleSheet(
      QStringLiteral("QScrollArea { border: none; background: %1; }"
                     "QScrollBar:vertical { background: transparent; width: "
                     "10px; margin: 4px 2px; }"
                     "QScrollBar::handle:vertical { background: "
                     "rgba(127,127,127,0.30); border-radius: 3px; "
                     "min-height: 24px; }"
                     "QScrollBar::add-line:vertical, "
                     "QScrollBar::sub-line:vertical { height: 0; }"
                     "QScrollBar::add-page:vertical, "
                     "QScrollBar::sub-page:vertical { background: transparent; "
                     "}")
          .arg(theme_.background.name()));
  transcript_->SetTheme(theme_);
  transcript_->SetChatFontScale(chat_font_scale_);
}

void ChatView::SetContentMaxWidth(int pixels) {
  if (transcript_ != nullptr) {
    transcript_->SetContentMaxWidth(pixels);
  }
}

void ChatView::SetShowTimestamps(bool show) {
  if (transcript_ != nullptr) {
    transcript_->SetShowTimestamps(show);
  }
}

void ChatView::SetReasoningCollapsedByDefault(bool collapsed) {
  if (transcript_ != nullptr) {
    transcript_->SetReasoningCollapsedByDefault(collapsed);
  }
}

void ChatView::SetAnimationsEnabled(bool enabled) {
  if (transcript_ != nullptr) {
    transcript_->SetAnimationsEnabled(enabled);
  }
}

void ChatView::SetChatFontScale(double scale) {
  chat_font_scale_ = std::clamp(scale, 0.8, 1.4);
  if (transcript_ != nullptr) {
    transcript_->SetChatFontScale(chat_font_scale_);
  }
}

void ChatView::SetCodeFontScale(double scale) {
  if (transcript_ != nullptr) {
    transcript_->SetCodeFontScale(scale);
  }
}

bool ChatView::IsCaretAnimatingForTest() const {
  return transcript_ != nullptr && transcript_->IsCaretAnimatingForTest();
}

void ChatView::SetSnapshot(std::shared_ptr<const ProfileSnapshot> snapshot) {
  QScrollBar* bar = verticalScrollBar();
  const bool at_bottom = follow_bottom_ && bar->value() >= bar->maximum() - 6;
  transcript_->SetSnapshot(std::move(snapshot));
  if (at_bottom) {
    // The range grows when the transcript's fixed height lands, which can be
    // after this call. Follow the bottom again on the next loop turn.
    bar->setValue(bar->maximum());
    QTimer::singleShot(0, this, [this]() {
      QScrollBar* scroll_bar = verticalScrollBar();
      scroll_bar->setValue(scroll_bar->maximum());
    });
  }
}

void ChatView::SetFollowBottom(bool follow_bottom) {
  follow_bottom_ = follow_bottom;
}

void ChatView::EnsureSearchVisible() {
  if (transcript_ == nullptr) {
    return;
  }
  const QRectF rect = transcript_->CurrentMatchRect();
  if (!rect.isNull()) {
    ensureVisible(0, static_cast<int>(rect.center().y()), 0, 120);
  }
}

void ChatView::ScrollBy(int delta) {
  QScrollBar* bar = verticalScrollBar();
  if (bar == nullptr) {
    return;
  }
  follow_bottom_ = false;
  bar->setValue(bar->value() + delta);
}

void ChatView::ScrollToMessageRow(qint64 row_id) {
  if (transcript_ == nullptr || row_id <= 0) {
    return;
  }
  const QRectF rect = transcript_->MessageRectForRow(row_id);
  if (rect.isNull()) {
    transcript_->update();
    return;
  }
  follow_bottom_ = false;
  ensureVisible(0, static_cast<int>(rect.center().y()), 0, 120);
}

void ChatView::SetSearchQuery(const QString& query) {
  if (transcript_ == nullptr) {
    return;
  }
  transcript_->SetSearchQuery(query);
  EnsureSearchVisible();
}

void ChatView::FindNext() {
  if (transcript_ == nullptr) {
    return;
  }
  transcript_->FindNext();
  EnsureSearchVisible();
}

void ChatView::FindPrevious() {
  if (transcript_ == nullptr) {
    return;
  }
  transcript_->FindPrevious();
  EnsureSearchVisible();
}

void ChatView::ClearSearch() {
  if (transcript_ != nullptr) {
    transcript_->ClearSearch();
  }
}

int ChatView::SearchMatchCount() const {
  return transcript_ == nullptr ? 0 : transcript_->SearchMatchCount();
}

int ChatView::CurrentSearchMatch() const {
  return transcript_ == nullptr ? -1 : transcript_->CurrentSearchMatch();
}

void ChatView::ScrollToTop() { verticalScrollBar()->setValue(0); }

void ChatView::ScrollToBottom() {
  QScrollBar* bar = verticalScrollBar();
  bar->setValue(bar->maximum());
  QTimer::singleShot(0, this, [this]() {
    QScrollBar* scroll_bar = verticalScrollBar();
    scroll_bar->setValue(scroll_bar->maximum());
  });
}

void ChatView::SetProfileName(const QString& name) {
  transcript_->SetProfileName(name);
}

void ChatView::SetRequest(const PendingRequest& request) {
  transcript_->SetRequest(request);
}

void ChatView::ClearRequest() { transcript_->ClearRequest(); }

bool ChatView::HasConnectionRequest() const {
  return transcript_ != nullptr && transcript_->HasConnectionRequest();
}

bool ChatView::AnswerConnectionDisplay(const QString& text) {
  return transcript_ != nullptr && transcript_->AnswerConnectionDisplay(text);
}

bool ChatView::ContinueConnection() {
  return transcript_ != nullptr && transcript_->ContinueConnection();
}

void ChatView::SetRequestRespondHandler(
    std::function<void(const QString&, const QJsonObject&)> handler) {
  transcript_->SetRequestRespondHandler(std::move(handler));
}

void ChatView::SetEditMessageHandler(MessageActionHandler handler) {
  transcript_->SetEditMessageHandler(std::move(handler));
}

void ChatView::SetRegenerateMessageHandler(MessageActionHandler handler) {
  transcript_->SetRegenerateMessageHandler(std::move(handler));
}

void ChatView::SetReadAloudHandler(MessageActionHandler handler) {
  transcript_->SetReadAloudHandler(std::move(handler));
}

void ChatView::SetPinMessageHandler(MessageActionHandler handler) {
  transcript_->SetPinMessageHandler(std::move(handler));
}

void ChatView::SetBranchMessageHandler(MessageActionHandler handler) {
  transcript_->SetBranchMessageHandler(std::move(handler));
}

void ChatView::SetRewindMessageHandler(MessageActionHandler handler) {
  transcript_->SetRewindMessageHandler(std::move(handler));
}

void ChatView::SetReactMessageHandler(ReactionHandler handler) {
  transcript_->SetReactMessageHandler(std::move(handler));
}

void ChatView::SetDeleteConversationHandler(std::function<void()> handler) {
  transcript_->SetDeleteConversationHandler(std::move(handler));
}

void ChatView::TriggerMessageActionForTest(const QString& action,
                                           int message_index,
                                           const QString& value) {
  transcript_->TriggerMessageActionForTest(action, message_index, value);
}

void ChatView::resizeEvent(QResizeEvent* event) {
  QScrollArea::resizeEvent(event);
  transcript_->Rebuild();
}

}  // namespace hermes::ui
