// Copyright 2026 The Hermes Qt Spike Authors.
//
// Transcript contract for reasoning blocks: the gateway streams reasoning as
// its own events, so the answer keeps it beside the message, collapsed to a
// header until the user expands it.

#include "src/chat_view.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialog>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QtTest>
#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "src/detached_chat.h"
#include "src/markdown.h"
#include "src/model.h"
#include "src/prompt_submit.h"
#include "src/request_bar.h"
#include "src/theme.h"

namespace {

std::shared_ptr<hermes::ui::ProfileSnapshot> SnapshotWithReasoning(
    bool streaming) {
  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  message.streaming = streaming;
  message.reasoning =
      QStringLiteral("Step one.\nStep two.\nStep three.\nStep four.");
  message.reasoning_streaming = streaming;
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kParagraph;
  block.text = QStringLiteral("The answer.");
  message.blocks.push_back(block);
  snapshot->messages.push_back(message);
  return snapshot;
}

std::shared_ptr<hermes::ui::ProfileSnapshot> SnapshotWithDisplayBlocks() {
  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime(QDate(2026, 1, 2), QTime(10, 30));
  message.reasoning = QStringLiteral("A short trace.");
  hermes::ui::MessageBlock paragraph;
  paragraph.kind = hermes::ui::BlockKind::kParagraph;
  paragraph.text = QStringLiteral(
      "A long paragraph that wraps across several lines so "
      "changing the transcript width or text scale moves the "
      "laid-out height instead of only repainting glyphs. ");
  for (int i = 0; i < 8; ++i) {
    paragraph.text += QStringLiteral("More words in the same paragraph. ");
  }
  message.blocks.push_back(paragraph);
  hermes::ui::MessageBlock code;
  code.kind = hermes::ui::BlockKind::kCode;
  code.language = QStringLiteral("cpp");
  code.text = QStringLiteral("int main() {\n  return 0;\n}");
  message.blocks.push_back(code);
  snapshot->messages.push_back(message);
  return snapshot;
}

std::shared_ptr<hermes::ui::ProfileSnapshot> SnapshotWithLongCodeLine() {
  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime(QDate(2026, 1, 2), QTime(10, 30));
  hermes::ui::MessageBlock code;
  code.kind = hermes::ui::BlockKind::kCode;
  code.language = QStringLiteral("cpp");
  code.text = QStringLiteral("wrapped_%1\nint done = 1;")
                  .arg(QString(420, QLatin1Char('x')));
  message.blocks.push_back(code);
  snapshot->messages.push_back(message);
  return snapshot;
}

}  // namespace

class ChatViewTest : public QObject {
  Q_OBJECT

 private slots:
  void reasoningBlockCollapsesAndExpandsOnClick();
  void reasoningBlockStartsCollapsedAfterTurn();
  void consecutiveToolRowsCollapseIntoOneGroup();
  void toolOutputWrapsLongLines();
  void quoteCardWrapsAndContainsText();
  void appearancePreferencesReshapeTranscript();
  void markdownParsesRichBlocksAndInlineSpans();
  void markdownBlocksRenderInTranscript();
  void codePreviewRendersSvg();
  void codePreviewRendersRtf();
  void codeCardContainsWrappedLines();
  void messageActionsEditAndRegenerate();
  void promptSubmitTruncationIsExplicit();
  void transcriptSearchFindsAndWrapsMatches();
  void approvalRendersAsInlineTranscriptCard();
  void clarifyOptionsStackAndWrapInsideCard();
  void connectionCardAnswersTargetsAndContinues();
  void connectionCardYieldsToTypedText();
  void connectionDisplayCardUsesComposerText();
  void reactionFooterAddsTranscriptHeight();
  void hoverActionRowMatchesDesktop();
  void everyMessageItemUsesHoverActionRow();
  void transcriptTextSelectionCopiesToClipboard();
  void detachedChatWindowSharesSnapshotAndComposer();
};

void ChatViewTest::reasoningBlockCollapsesAndExpandsOnClick() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  view.SetSnapshot(SnapshotWithReasoning(true));
  QTest::qWait(30);
  QWidget* transcript = view.widget();
  QVERIFY(transcript != nullptr);
  // Reasoning starts collapsed even while it streams.
  QVERIFY(!view.ReasoningExpanded(0));
  const int collapsed_height = transcript->height();
  QVERIFY(collapsed_height > 0);

  // Header sits under the author row: y = 26 + 24, 26px tall.
  QTest::mouseClick(transcript, Qt::LeftButton, Qt::NoModifier,
                    QPoint(120, 62));
  QTest::qWait(30);
  QVERIFY(view.ReasoningExpanded(0));
  const int expanded_height = transcript->height();
  QVERIFY2(expanded_height > collapsed_height,
           "clicking the reasoning header must expand the block");

  QTest::mouseClick(transcript, Qt::LeftButton, Qt::NoModifier,
                    QPoint(120, 62));
  QTest::qWait(30);
  QVERIFY(!view.ReasoningExpanded(0));
  QCOMPARE(transcript->height(), collapsed_height);
}

void ChatViewTest::reasoningBlockStartsCollapsedAfterTurn() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  view.SetSnapshot(SnapshotWithReasoning(true));
  QTest::qWait(30);
  const int streaming_height = view.widget()->height();
  QVERIFY(!view.ReasoningExpanded(0));

  // A running and a finished turn both show just the "Thought" header until
  // the reader opens it.
  auto finished = SnapshotWithReasoning(false);
  finished->revision = 2;
  view.SetSnapshot(finished);
  QTest::qWait(30);
  QVERIFY(!view.ReasoningExpanded(0));
  QCOMPARE(view.widget()->height(), streaming_height);
}

void ChatViewTest::consecutiveToolRowsCollapseIntoOneGroup() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  const QStringList tools = {QStringLiteral("terminal"),
                             QStringLiteral("patch"),
                             QStringLiteral("vision_analyze")};
  for (const QString& tool : tools) {
    hermes::ui::MessageBlock block;
    block.kind = hermes::ui::BlockKind::kTool;
    block.text = tool;
    block.detail = QStringLiteral("output of ") + tool;
    message.blocks.push_back(block);
  }
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  const int collapsed_height = view.widget()->height();
  QRectF group_rect;
  for (const auto& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:tool:g"))) {
      group_rect = target.rect;
      break;
    }
  }
  QVERIFY2(!group_rect.isNull(),
           "three consecutive tools must render as one group row");
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    group_rect.center().toPoint());
  QTest::qWait(30);
  QVERIFY2(view.widget()->height() > collapsed_height,
           "clicking the group row must reveal the individual tool rows");
}

void ChatViewTest::toolOutputWrapsLongLines() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kTool;
  block.text = QStringLiteral("terminal");
  block.detail =
      QString(600, QLatin1Char('x')) + QStringLiteral("\nsecond line");
  message.blocks.push_back(block);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  const int collapsed_height = view.widget()->height();
  QRectF tool_rect;
  for (const auto& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:tool:"))) {
      tool_rect = target.rect;
      break;
    }
  }
  QVERIFY2(!tool_rect.isNull(), "tool row must expose a click target");
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    tool_rect.center().toPoint());
  QTest::qWait(30);
  // A 600-character unbroken token must occupy several wrapped lines instead
  // of one clipped row.
  QVERIFY2(view.widget()->height() > collapsed_height + 40,
           "long tool output must wrap onto multiple lines");
}

void ChatViewTest::quoteCardWrapsAndContainsText() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(520, 640);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kQuote;
  block.text = QString(180, QLatin1Char('w')) + QStringLiteral(" tail");
  message.blocks.push_back(block);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  auto rects = view.BlockRectsForTest(hermes::ui::BlockKind::kQuote);
  QCOMPARE(rects.size(), std::size_t(1));
  const QRectF card = rects.front().first;
  const QRectF text = rects.front().second;
  QVERIFY(card.right() <= view.widget()->width() - 34 + 0.5);
  QVERIFY(text.left() >= card.left());
  QVERIFY(text.right() <= card.right());
  QVERIFY(text.top() >= card.top());
  QVERIFY(text.bottom() <= card.bottom());
  QVERIFY2(text.height() > 20, "a long quote must wrap onto several lines");
  const qreal wide_height = text.height();

  view.resize(380, 640);
  QTest::qWait(40);
  rects = view.BlockRectsForTest(hermes::ui::BlockKind::kQuote);
  QCOMPARE(rects.size(), std::size_t(1));
  const QRectF narrow_card = rects.front().first;
  const QRectF narrow_text = rects.front().second;
  QVERIFY(narrow_card.right() <= view.widget()->width() - 34 + 0.5);
  QVERIFY(narrow_text.left() >= narrow_card.left());
  QVERIFY(narrow_text.right() <= narrow_card.right());
  QVERIFY(narrow_text.top() >= narrow_card.top());
  QVERIFY(narrow_text.bottom() <= narrow_card.bottom());
  QVERIFY2(narrow_text.height() > wide_height,
           "a narrower transcript must wrap the quote further");
}

void ChatViewTest::appearancePreferencesReshapeTranscript() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 620);
  view.show();

  view.SetContentMaxWidth(720);
  view.SetReasoningCollapsedByDefault(true);
  view.SetShowTimestamps(true);
  view.SetAnimationsEnabled(true);
  view.SetChatFontScale(1.0);
  view.SetCodeFontScale(1.0);
  view.SetSnapshot(SnapshotWithDisplayBlocks());
  QTest::qWait(20);

  auto paragraph_rect = [&view]() {
    const auto rects =
        view.BlockRectsForTest(hermes::ui::BlockKind::kParagraph);
    return rects.empty() ? QRectF() : rects.front().first;
  };
  auto code_rect = [&view]() {
    const auto rects = view.BlockRectsForTest(hermes::ui::BlockKind::kCode);
    return rects.empty() ? QRectF() : rects.front().first;
  };

  QCOMPARE(qRound(paragraph_rect().width()), 720);
  const qreal narrow_before = paragraph_rect().width();
  view.SetContentMaxWidth(600);
  QCOMPARE(qRound(paragraph_rect().width()), 600);
  QVERIFY(paragraph_rect().width() < narrow_before);

  const qreal paragraph_height = paragraph_rect().height();
  view.SetChatFontScale(1.25);
  QVERIFY2(paragraph_rect().height() > paragraph_height,
           "chat text scale must reflow the paragraph");
  view.SetChatFontScale(1.0);

  const qreal code_height = code_rect().height();
  view.SetCodeFontScale(1.35);
  QVERIFY2(code_rect().height() > code_height,
           "code scale must grow fenced code rows");
  view.SetCodeFontScale(1.0);

  view.SelectAllTextForTest();
  QVERIFY(view.SelectedTextForTest().contains(QStringLiteral("10:30 AM")));
  view.SetShowTimestamps(false);
  view.SelectAllTextForTest();
  QVERIFY(!view.SelectedTextForTest().contains(QStringLiteral("10:30 AM")));
  view.SetShowTimestamps(true);

  QVERIFY(!view.ReasoningExpanded(0));
  view.SetReasoningCollapsedByDefault(false);
  QVERIFY(view.ReasoningExpanded(0));
  view.SetReasoningCollapsedByDefault(true);
  QVERIFY(!view.ReasoningExpanded(0));

  QVERIFY(view.IsCaretAnimatingForTest());
  view.SetAnimationsEnabled(false);
  QVERIFY(!view.IsCaretAnimatingForTest());
  view.SetAnimationsEnabled(true);
  QVERIFY(view.IsCaretAnimatingForTest());
}

void ChatViewTest::markdownParsesRichBlocksAndInlineSpans() {
  const std::vector<hermes::ui::MessageBlock> blocks =
      hermes::ui::ParseMarkdown(QStringLiteral(
          "# Title\n"
          "### Small\n"
          "> quoted line\n"
          "- bullet\n"
          "1. first\n"
          "- [x] done\n"
          "---\n"
          "| Left | Right |\n"
          "|:-----|------:|\n"
          "| a | b |\n"
          "![chart](artifacts/chart.png \"Chart\")\n"
          "```diff\n"
          "-old\n"
          "+new\n"
          "```\n"
          "```mermaid\n"
          "graph TD; A-->B;\n"
          "```\n"
          "$$\n"
          "x = y + 1\n"
          "$$\n"
          ":::artifact{src=\"artifacts/report.md\" title=\"Report\"}\n"
          "Quarterly summary\n"
          ":::\n"
          "[^1]: footnote body\n"
          "A [link](https://example.com) with **bold**, *italic*, ~~old~~ and "
          "[@doe]."));

  bool saw_h1 = false;
  bool saw_h3 = false;
  bool saw_quote = false;
  bool saw_bullet = false;
  bool saw_numbered = false;
  bool saw_task = false;
  bool saw_rule = false;
  bool saw_table = false;
  bool saw_link = false;
  bool saw_image = false;
  bool saw_diff = false;
  bool saw_mermaid = false;
  bool saw_math = false;
  bool saw_artifact = false;
  bool saw_footnote = false;
  for (const hermes::ui::MessageBlock& block : blocks) {
    if (block.kind == hermes::ui::BlockKind::kHeading) {
      saw_h1 = saw_h1 || (block.detail == QStringLiteral("1") &&
                          block.text == QStringLiteral("Title"));
      saw_h3 = saw_h3 || (block.detail == QStringLiteral("3") &&
                          block.text == QStringLiteral("Small"));
    } else if (block.kind == hermes::ui::BlockKind::kQuote) {
      saw_quote = block.text == QStringLiteral("quoted line");
    } else if (block.kind == hermes::ui::BlockKind::kBullet) {
      if (block.detail == QStringLiteral("task:done")) {
        saw_task = block.text == QStringLiteral("done");
      } else if (block.detail == QStringLiteral("1.")) {
        saw_numbered = block.text == QStringLiteral("first");
      } else {
        saw_bullet = block.text == QStringLiteral("bullet");
      }
    } else if (block.kind == hermes::ui::BlockKind::kRule) {
      saw_rule = true;
    } else if (block.kind == hermes::ui::BlockKind::kTable) {
      saw_table =
          block.detail.contains(QStringLiteral("<table")) &&
          block.detail.contains(QStringLiteral("Left")) &&
          block.detail.contains(QStringLiteral("align=\"right\"")) &&
          block.detail.contains(
              QStringLiteral("border-bottom:1px solid rgba(127,127,127,")) &&
          block.detail.contains(
              QStringLiteral("background-color:rgba(127,127,127,0.10)"));
    } else if (block.kind == hermes::ui::BlockKind::kImage) {
      saw_image = block.text == QStringLiteral("chart") &&
                  block.detail == QStringLiteral("artifacts/chart.png") &&
                  block.language == QStringLiteral("Chart");
    } else if (block.kind == hermes::ui::BlockKind::kDiff) {
      saw_diff = block.text.contains(QStringLiteral("-old")) &&
                 block.text.contains(QStringLiteral("+new"));
    } else if (block.kind == hermes::ui::BlockKind::kMermaid) {
      saw_mermaid = block.text.contains(QStringLiteral("A-->B"));
    } else if (block.kind == hermes::ui::BlockKind::kMath) {
      saw_math = block.text == QStringLiteral("x = y + 1");
    } else if (block.kind == hermes::ui::BlockKind::kArtifact) {
      saw_artifact = block.detail == QStringLiteral("artifacts/report.md") &&
                     block.language == QStringLiteral("Report") &&
                     block.text == QStringLiteral("Quarterly summary");
    } else if (block.kind == hermes::ui::BlockKind::kFootnote) {
      saw_footnote = block.detail == QStringLiteral("1") &&
                     block.text == QStringLiteral("footnote body");
    } else if (block.kind == hermes::ui::BlockKind::kParagraph) {
      const QString html = hermes::ui::InlineHtml(block.text);
      saw_link =
          saw_link ||
          (html.contains(QStringLiteral("href=\"https://example.com\"")) &&
           html.contains(QStringLiteral("<b>bold</b>")) &&
           html.contains(QStringLiteral("<i>italic</i>")) &&
           html.contains(QStringLiteral("<s>old</s>")) &&
           html.contains(QStringLiteral("<sup>")) &&
           html.contains(QStringLiteral("citation:@doe")));
    }
  }
  QVERIFY(saw_h1);
  QVERIFY(saw_h3);
  QVERIFY(saw_quote);
  QVERIFY(saw_bullet);
  QVERIFY(saw_numbered);
  QVERIFY(saw_task);
  QVERIFY(saw_rule);
  QVERIFY(saw_table);
  QVERIFY(saw_link);
  QVERIFY(saw_image);
  QVERIFY(saw_diff);
  QVERIFY(saw_mermaid);
  QVERIFY(saw_math);
  QVERIFY(saw_artifact);
  QVERIFY(saw_footnote);
}

void ChatViewTest::markdownBlocksRenderInTranscript() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  message.blocks = hermes::ui::ParseMarkdown(
      QStringLiteral("## Result\n"
                     "> keep this in mind\n"
                     "- [x] shipped\n"
                     "1. verify\n"
                     "---\n"
                     "| Name | Count |\n"
                     "|:-----|------:|\n"
                     "| rows | 2 |\n"
                     "```diff\n"
                     "-old\n"
                     "+new\n"
                     "```\n"
                     "```svg\n"
                     "<svg xmlns='http://www.w3.org/2000/svg' width='10' "
                     "height='10'><rect width='10' height='10'/></svg>\n"
                     "```\n"
                     "$$x + 1$$\n"
                     ":::artifact artifacts/chart.png\n"
                     "Chart caption\n"
                     ":::\n"
                     "[^1]: note body\n"
                     "See [docs](https://example.com)."));
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);
  QVERIFY(view.widget() != nullptr);
  QVERIFY2(view.widget()->height() > 300,
           "rich markdown blocks must occupy transcript height");
  bool preview_target = false;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:preview:"))) {
      preview_target = true;
    }
  }
  QVERIFY2(preview_target,
           "previewable code blocks must expose a Preview target");
}

void ChatViewTest::codeCardContainsWrappedLines() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 620);
  view.show();
  view.SetContentMaxWidth(600);
  view.SetCodeFontScale(1.0);
  view.SetSnapshot(SnapshotWithLongCodeLine());
  QTest::qWait(20);

  const auto rects = view.BlockRectsForTest(hermes::ui::BlockKind::kCode);
  QCOMPARE(rects.size(), std::size_t(1));
  // Plain code text must inherit the theme's code foreground. A malformed
  // style attribute silently falls back to black, which is unreadable on a
  // dark code card even when the surrounding transcript is legible.
  QCOMPARE(view.FirstCodeForegroundForTest().name(), theme.code_text.name());
  const QRectF card = rects.front().first;
  const QRectF text = rects.front().second;
  // A wrapped source line grows the document; the card must grow with it and
  // the text rect must start below the language/header strip.
  QVERIFY(text.top() >= card.top() + 28);
  QVERIFY(text.left() > card.left());
  QVERIFY(text.right() <= card.right());
  QVERIFY(text.bottom() <= card.bottom());
  QVERIFY2(text.height() > 4 * 19,
           "the long token must wrap across several code lines");
  QVERIFY2(card.height() >= text.height() + 28 + 14 - 0.5,
           "the card must contain the whole wrapped code document");

  // Select from the first wrapped line through the final logical line. Every
  // visual line in between must paint its own highlight; using line-relative
  // offsets with QTextLine::cursorToX used to collapse them all to x=0.
  QWidget* transcript = view.widget();
  const QPointF from(text.left() + 5, text.top() + 5);
  const QPointF to(text.left() + 200, text.bottom() - 5);
  QTest::mousePress(transcript, Qt::LeftButton, Qt::NoModifier, from.toPoint());
  QMouseEvent move(QEvent::MouseMove, to, to, Qt::NoButton, Qt::LeftButton,
                   Qt::NoModifier);
  QApplication::sendEvent(transcript, &move);
  QTest::mouseRelease(transcript, Qt::LeftButton, Qt::NoModifier, to.toPoint());
  const QString selected = view.SelectedTextForTest();
  QVERIFY2(selected.contains(QStringLiteral("int done = 1;")),
           "drag selection must reach the final logical line");
  const std::vector<QRectF> highlights = view.SelectionRectsForTest();
  QVERIFY2(highlights.size() >= 5,
           "wrapped code selection must paint one rect per visual line");
  for (const QRectF& highlight : highlights) {
    QVERIFY(highlight.width() > 0);
    QVERIFY(highlight.height() > 0);
    QVERIFY(highlight.left() >= text.left() - 0.5);
    QVERIFY(highlight.right() <= text.right() + 0.5);
  }
  QVERIFY2(highlights.back().top() > highlights.front().top(),
           "highlights must descend through the wrapped code lines");
}

void ChatViewTest::codePreviewRendersSvg() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(820, 620);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kCode;
  block.language = QStringLiteral("svg");
  block.text = QStringLiteral(
      "<svg xmlns='http://www.w3.org/2000/svg' width='96' height='96' "
      "viewBox='0 0 96 96'><rect x='3' y='3' width='90' height='90' "
      "rx='20' fill='none' stroke='black' stroke-width='6'/></svg>");
  message.blocks.push_back(block);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  hermes::ui::ClickTarget preview;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:preview:"))) {
      preview = target;
      break;
    }
  }
  QVERIFY2(!preview.rect.isNull(), "SVG block must expose Preview");
  QVERIFY(preview.modal);

  bool image_rendered = false;
  QTimer::singleShot(150, &view, [&image_rendered]() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    auto* image =
        dialog->findChild<QLabel*>(QStringLiteral("codePreviewImage"));
    image_rendered = image != nullptr && !image->pixmap().isNull();
    dialog->accept();
  });
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    preview.rect.center().toPoint());
  QVERIFY2(image_rendered, "SVG preview dialog must render the drawing");
}

void ChatViewTest::codePreviewRendersRtf() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(820, 620);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kCode;
  block.language = QStringLiteral("rtf");
  block.text = QStringLiteral(
      "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Helvetica;}}\\f0\\fs24 "
      "Hello RTF\\par}");
  message.blocks.push_back(block);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  hermes::ui::ClickTarget preview;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:preview:"))) {
      preview = target;
      break;
    }
  }
  QVERIFY2(!preview.rect.isNull(), "RTF block must expose Preview");

  bool browser_rendered = false;
  QTimer::singleShot(150, &view, [&browser_rendered]() {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    if (dialog == nullptr) {
      return;
    }
    browser_rendered = dialog->findChild<QWidget*>(
                           QStringLiteral("codePreviewBrowser")) != nullptr;
    dialog->accept();
  });
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    preview.rect.center().toPoint());
  QVERIFY2(browser_rendered, "RTF preview must convert and render to HTML");
}

void ChatViewTest::messageActionsEditAndRegenerate() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage user;
  user.role = hermes::ui::MessageRole::kUser;
  user.row_id = 41;
  user.timestamp = QDateTime::currentDateTime();
  user.raw_text = QStringLiteral("original question");
  hermes::ui::MessageBlock question;
  question.kind = hermes::ui::BlockKind::kParagraph;
  question.text = user.raw_text;
  user.blocks.push_back(question);
  snapshot->messages.push_back(user);
  hermes::ui::ChatMessage answer;
  answer.role = hermes::ui::MessageRole::kAssistant;
  answer.author = QStringLiteral("Hermes");
  answer.row_id = 42;
  answer.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock reply;
  reply.kind = hermes::ui::BlockKind::kParagraph;
  reply.text = QStringLiteral("first answer");
  answer.blocks.push_back(reply);
  snapshot->messages.push_back(answer);

  qint64 edited_row = 0;
  QString edited_text;
  qint64 regenerated_row = 0;
  int edits = 0;
  int regenerates = 0;
  int speaks = 0;
  int branches = 0;
  int rewinds = 0;
  int reactions = 0;
  int deletes = 0;
  int pins = 0;
  QString pinned_text;
  QString react_emoji;
  QString branch_text;
  QString spoken_text;
  view.SetEditMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString& text) {
        ++edits;
        edited_row = message.row_id;
        edited_text = text;
      });
  view.SetRegenerateMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString&) {
        ++regenerates;
        regenerated_row = message.row_id;
      });
  view.SetReadAloudHandler(
      [&](const hermes::ui::ChatMessage& message, const QString& text) {
        ++speaks;
        spoken_text = text;
        QCOMPARE(message.row_id, qint64{42});
      });
  view.SetBranchMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString& text) {
        ++branches;
        branch_text = text;
        QCOMPARE(message.row_id, qint64{42});
      });
  view.SetRewindMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString&) {
        ++rewinds;
        QCOMPARE(message.row_id, qint64{42});
      });
  view.SetReactMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString& emoji) {
        ++reactions;
        react_emoji = emoji;
        QCOMPARE(message.row_id, qint64{42});
      });
  view.SetPinMessageHandler(
      [&](const hermes::ui::ChatMessage& message, const QString& text) {
        ++pins;
        pinned_text = text;
        QCOMPARE(message.row_id, qint64{42});
      });
  view.SetDeleteConversationHandler([&]() { ++deletes; });
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  const auto targets = view.ClickTargets();
  QRectF edit_rect;
  QRectF regenerate_rect;
  QRectF speak_rect;
  for (const auto& target : targets) {
    if (target.name == QStringLiteral("chat:editmsg:0")) {
      edit_rect = target.rect;
    } else if (target.name == QStringLiteral("chat:regenmsg:1")) {
      regenerate_rect = target.rect;
    } else if (target.name == QStringLiteral("chat:speakmsg:1")) {
      speak_rect = target.rect;
    }
  }
  QVERIFY2(!edit_rect.isNull(), "user message needs an Edit action");
  QVERIFY2(!regenerate_rect.isNull(),
           "completed assistant message needs a Retry action");
  QVERIFY2(!speak_rect.isNull(),
           "completed assistant message needs a read-aloud action");
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    edit_rect.center().toPoint());
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    regenerate_rect.center().toPoint());
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    speak_rect.center().toPoint());
  QCOMPARE(edits, 1);
  QCOMPARE(edited_row, qint64{41});
  QCOMPARE(edited_text, QStringLiteral("original question"));
  QCOMPARE(regenerates, 1);
  QCOMPARE(regenerated_row, qint64{42});
  QCOMPARE(speaks, 1);
  QCOMPARE(spoken_text, QStringLiteral("first answer"));

  view.TriggerMessageActionForTest(QStringLiteral("branch"), 1);
  view.TriggerMessageActionForTest(QStringLiteral("rewind"), 1);
  view.TriggerMessageActionForTest(QStringLiteral("react"), 1,
                                   QStringLiteral("🎉"));
  view.TriggerMessageActionForTest(QStringLiteral("pin"), 1);
  view.TriggerMessageActionForTest(QStringLiteral("delete"), 1);
  QCOMPARE(branches, 1);
  QCOMPARE(branch_text, QStringLiteral("first answer"));
  QCOMPARE(rewinds, 1);
  QCOMPARE(reactions, 1);
  QCOMPARE(react_emoji, QStringLiteral("🎉"));
  QCOMPARE(pins, 1);
  QCOMPARE(pinned_text, QStringLiteral("first answer"));
  QCOMPARE(deletes, 1);
}

void ChatViewTest::hoverActionRowMatchesDesktop() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage user;
  user.role = hermes::ui::MessageRole::kUser;
  user.row_id = 7;
  user.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock prompt;
  prompt.kind = hermes::ui::BlockKind::kParagraph;
  prompt.text = QStringLiteral("ship it");
  user.blocks.push_back(prompt);
  snapshot->messages.push_back(user);
  hermes::ui::ChatMessage answer;
  answer.role = hermes::ui::MessageRole::kAssistant;
  answer.author = QStringLiteral("Hermes");
  answer.row_id = 8;
  answer.timestamp = QDateTime::currentDateTime();
  answer.duration_secs = 42;
  hermes::ui::MessageBlock reply;
  reply.kind = hermes::ui::BlockKind::kParagraph;
  reply.text = QStringLiteral("shipped and verified");
  answer.blocks.push_back(reply);
  snapshot->messages.push_back(answer);

  int branches = 0;
  int regenerates = 0;
  int speaks = 0;
  int reactions = 0;
  QString branch_text;
  view.SetBranchMessageHandler(
      [&](const hermes::ui::ChatMessage&, const QString& text) {
        ++branches;
        branch_text = text;
      });
  view.SetRegenerateMessageHandler(
      [&](const hermes::ui::ChatMessage&, const QString&) { ++regenerates; });
  view.SetReadAloudHandler(
      [&](const hermes::ui::ChatMessage&, const QString&) { ++speaks; });
  view.SetReactMessageHandler(
      [&](const hermes::ui::ChatMessage&, const QString&) { ++reactions; });
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QMap<QString, QRectF> actions;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:branchmsg:")) ||
        target.name.startsWith(QStringLiteral("chat:copymsg:")) ||
        target.name.startsWith(QStringLiteral("chat:copyfullmsg:")) ||
        target.name.startsWith(QStringLiteral("chat:speakmsg:")) ||
        target.name.startsWith(QStringLiteral("chat:regenmsg:")) ||
        target.name.startsWith(QStringLiteral("chat:reactmsg:"))) {
      if (target.name.endsWith(QStringLiteral(":1"))) {
        actions.insert(target.name, target.rect);
      }
    }
  }
  const QStringList expected = {
      QStringLiteral("chat:branchmsg:1"),   QStringLiteral("chat:copymsg:1"),
      QStringLiteral("chat:copyfullmsg:1"), QStringLiteral("chat:speakmsg:1"),
      QStringLiteral("chat:regenmsg:1"),    QStringLiteral("chat:reactmsg:1")};
  for (const QString& name : expected) {
    QVERIFY2(actions.contains(name), qPrintable(name + " missing"));
  }
  QCOMPARE(actions.size(), expected.size());

  QStringList sorted = expected;
  std::sort(sorted.begin(), sorted.end(),
            [&](const QString& left, const QString& right) {
              return actions.value(left).left() < actions.value(right).left();
            });
  const QStringList visual_order = {
      QStringLiteral("chat:branchmsg:1"),   QStringLiteral("chat:copymsg:1"),
      QStringLiteral("chat:copyfullmsg:1"), QStringLiteral("chat:speakmsg:1"),
      QStringLiteral("chat:regenmsg:1"),    QStringLiteral("chat:reactmsg:1")};
  QCOMPARE(sorted, visual_order);

  QGuiApplication::clipboard()->clear();
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      actions.value(QStringLiteral("chat:copymsg:1")).center().toPoint());
  QCOMPARE(QGuiApplication::clipboard()->text(),
           QStringLiteral("shipped and verified"));
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      actions.value(QStringLiteral("chat:copyfullmsg:1")).center().toPoint());
  QCOMPARE(QGuiApplication::clipboard()->text(),
           QStringLiteral("shipped and verified"));
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      actions.value(QStringLiteral("chat:branchmsg:1")).center().toPoint());
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      actions.value(QStringLiteral("chat:speakmsg:1")).center().toPoint());
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      actions.value(QStringLiteral("chat:regenmsg:1")).center().toPoint());
  QCOMPARE(branches, 1);
  QCOMPARE(branch_text, QStringLiteral("shipped and verified"));
  QCOMPARE(speaks, 1);
  QCOMPARE(regenerates, 1);

  view.TriggerMessageActionForTest(QStringLiteral("react"), 1,
                                   QStringLiteral("👍"));
  QCOMPARE(reactions, 1);
}

void ChatViewTest::everyMessageItemUsesHoverActionRow() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage user;
  user.role = hermes::ui::MessageRole::kUser;
  user.row_id = 7;
  user.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock prompt;
  prompt.kind = hermes::ui::BlockKind::kParagraph;
  prompt.text = QStringLiteral("ship it");
  user.blocks.push_back(prompt);
  snapshot->messages.push_back(user);
  hermes::ui::ChatMessage answer;
  answer.role = hermes::ui::MessageRole::kAssistant;
  answer.author = QStringLiteral("Hermes");
  answer.row_id = 8;
  answer.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock reply;
  reply.kind = hermes::ui::BlockKind::kParagraph;
  reply.text = QStringLiteral("shipped");
  answer.blocks.push_back(reply);
  snapshot->messages.push_back(answer);
  hermes::ui::ChatMessage notice;
  notice.role = hermes::ui::MessageRole::kSystem;
  notice.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock status;
  status.kind = hermes::ui::BlockKind::kParagraph;
  status.text = QStringLiteral("Steer queued.");
  notice.blocks.push_back(status);
  snapshot->messages.push_back(notice);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QMap<QString, QRectF> targets;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    targets.insert(target.name, target.rect);
  }

  // User prompts get the same hover row, including Edit where the message is
  // persisted and the reply-only actions that also make sense for a prompt.
  for (const QString& name :
       {QStringLiteral("chat:branchmsg:0"), QStringLiteral("chat:copymsg:0"),
        QStringLiteral("chat:copyfullmsg:0"), QStringLiteral("chat:speakmsg:0"),
        QStringLiteral("chat:editmsg:0"), QStringLiteral("chat:reactmsg:0")}) {
    QVERIFY2(targets.contains(name), qPrintable(name + " missing"));
  }

  // System notices are items too, but keep only Copy / Copy full.
  QVERIFY(targets.contains(QStringLiteral("chat:copymsg:2")));
  QVERIFY(targets.contains(QStringLiteral("chat:copyfullmsg:2")));
  QVERIFY(!targets.contains(QStringLiteral("chat:branchmsg:2")));
  QVERIFY(!targets.contains(QStringLiteral("chat:speakmsg:2")));
  QVERIFY(!targets.contains(QStringLiteral("chat:editmsg:2")));
  QVERIFY(!targets.contains(QStringLiteral("chat:reactmsg:2")));

  QGuiApplication::clipboard()->clear();
  QTest::mouseClick(
      view.widget(), Qt::LeftButton, Qt::NoModifier,
      targets.value(QStringLiteral("chat:copymsg:0")).center().toPoint());
  QTest::qWait(20);
  QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("ship it"));
}

void ChatViewTest::detachedChatWindowSharesSnapshotAndComposer() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::DetachedChatWindow window(theme);
  window.resize(900, 700);
  window.show();
  QVERIFY(window.chat_view() != nullptr);
  QVERIFY(window.composer() != nullptr);

  window.SetDraft(QStringLiteral("detached draft"));
  QCOMPARE(window.Draft(), QStringLiteral("detached draft"));

  auto snapshot = SnapshotWithReasoning(false);
  snapshot->status_text = QStringLiteral("Gateway reconnecting");
  window.SetSnapshot(snapshot);
  QTest::qWait(20);
  QVERIFY(window.windowTitle().contains(QStringLiteral("default")));
  QVERIFY(!window.chat_view()->ClickTargets().empty());
  auto* status =
      window.findChild<QLabel*>(QStringLiteral("detachedChatStatus"));
  QVERIFY(status != nullptr);
  QVERIFY(status->isVisible());
  QCOMPARE(status->text(), QStringLiteral("Gateway reconnecting"));

  snapshot->connected = true;
  window.SetSnapshot(snapshot);
  QVERIFY(!status->isVisible());
  QCOMPARE(window.Draft(), QStringLiteral("detached draft"));

  window.SetTheme(hermes::ui::Theme::Dark());
  window.SetProfileName(QStringLiteral("work"));
  QVERIFY(window.windowTitle().contains(QStringLiteral("work")));
}

void ChatViewTest::transcriptTextSelectionCopiesToClipboard() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage user;
  user.role = hermes::ui::MessageRole::kUser;
  user.author = QStringLiteral("You");
  user.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock user_block;
  user_block.kind = hermes::ui::BlockKind::kParagraph;
  user_block.text = QStringLiteral("Hello selectable world.");
  user.blocks.push_back(user_block);
  snapshot->messages.push_back(user);

  hermes::ui::ChatMessage assistant;
  assistant.role = hermes::ui::MessageRole::kAssistant;
  assistant.author = QStringLiteral("Hermes");
  assistant.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock answer;
  answer.kind = hermes::ui::BlockKind::kParagraph;
  answer.text = QStringLiteral("The answer is selectable too.");
  assistant.blocks.push_back(answer);
  snapshot->messages.push_back(assistant);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QWidget* transcript = view.widget();
  QVERIFY(transcript != nullptr);
  view.SelectAllTextForTest();
  const QString selected = view.SelectedTextForTest();
  QVERIFY2(selected.contains(QStringLiteral("Hello selectable world.")),
           qPrintable(selected));
  QVERIFY2(selected.contains(QStringLiteral("The answer is selectable too.")),
           qPrintable(selected));

  QGuiApplication::clipboard()->clear();
  QTest::keySequence(transcript, QKeySequence::Copy);
  QCOMPARE(QGuiApplication::clipboard()->text(), selected);

  // Drag selection starts on the painted author/body and extends into the
  // answer. The transcript is custom-painted, so this exercises the hit-test
  // path rather than a native text widget.
  QTest::mousePress(transcript, Qt::LeftButton, Qt::NoModifier, QPoint(20, 8));
  QMouseEvent move(QEvent::MouseMove, QPointF(320, 150), QPointF(320, 150),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(transcript, &move);
  QTest::mouseRelease(transcript, Qt::LeftButton, Qt::NoModifier,
                      QPoint(320, 150));
  const QString dragged = view.SelectedTextForTest();
  QVERIFY2(dragged.contains(QStringLiteral("The answer is selectable too.")),
           qPrintable(dragged));

  QGuiApplication::clipboard()->clear();
  QTest::keySequence(transcript, QKeySequence::Copy);
  QCOMPARE(QGuiApplication::clipboard()->text(), dragged);
}

void ChatViewTest::promptSubmitTruncationIsExplicit() {
  const QJsonObject ordinary = hermes::ui::BuildPromptSubmitParams(
      QStringLiteral("s1"), QStringLiteral("hello"), QStringLiteral("p"),
      QStringLiteral("sub1"));
  QVERIFY(!ordinary.contains(QStringLiteral("confirm_truncate")));
  QVERIFY(!ordinary.contains(QStringLiteral("truncate_before_row_id")));

  const QJsonObject edit = hermes::ui::BuildPromptSubmitParams(
      QStringLiteral("s1"), QStringLiteral("fixed"), QStringLiteral("p"),
      QStringLiteral("sub2"), 77, false, true);
  QCOMPARE(edit.value(QStringLiteral("confirm_truncate")).toBool(), true);
  QCOMPARE(edit.value(QStringLiteral("truncate_before_row_id")).toInt(), 77);
  QVERIFY(!edit.contains(QStringLiteral("confirm_empty_truncate")));
  QCOMPARE(edit.value(QStringLiteral("confirm_deep_truncate")).toBool(), true);

  const QJsonObject first = hermes::ui::BuildPromptSubmitParams(
      QStringLiteral("s1"), QStringLiteral("first"), QString(), QString(), 12,
      true, false);
  QCOMPARE(first.value(QStringLiteral("confirm_empty_truncate")).toBool(),
           true);
  QVERIFY(!first.contains(QStringLiteral("profile")));

  const std::vector<qint64> rows = {10, 20, 30};
  const auto first_turn = hermes::ui::ComputePromptTruncation(rows, 10);
  QVERIFY(first_turn.confirm_empty_truncate);
  QVERIFY(first_turn.confirm_deep_truncate);
  const auto middle = hermes::ui::ComputePromptTruncation(rows, 20);
  QVERIFY(!middle.confirm_empty_truncate);
  QVERIFY(middle.confirm_deep_truncate);
  const auto last = hermes::ui::ComputePromptTruncation(rows, 30);
  QVERIFY(!last.confirm_empty_truncate);
  QVERIFY(!last.confirm_deep_truncate);
  const auto missing = hermes::ui::ComputePromptTruncation(rows, 99);
  QVERIFY(!missing.confirm_empty_truncate);
  QVERIFY(!missing.confirm_deep_truncate);

  QCOMPARE(hermes::ui::ComposePinnedPrompt({}, QStringLiteral("plain")),
           QStringLiteral("plain"));
  const QString pinned = hermes::ui::ComposePinnedPrompt(
      {QStringLiteral("first answer"), QStringLiteral("second answer")},
      QStringLiteral("next question"));
  QVERIFY(pinned.startsWith(
      QStringLiteral("Pinned context from earlier replies:")));
  QVERIFY(pinned.contains(QStringLiteral("first answer")));
  QVERIFY(pinned.contains(QStringLiteral("second answer")));
  QVERIFY(pinned.endsWith(QStringLiteral("next question")));
}

void ChatViewTest::transcriptSearchFindsAndWrapsMatches() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  hermes::ui::ChatMessage second;
  second.role = hermes::ui::MessageRole::kAssistant;
  second.author = QStringLiteral("Hermes");
  second.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kParagraph;
  block.text = QStringLiteral("Pricing details are attached.");
  second.blocks.push_back(block);
  snapshot->messages.push_back(second);
  hermes::ui::ChatMessage third;
  third.role = hermes::ui::MessageRole::kUser;
  third.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock prompt;
  prompt.kind = hermes::ui::BlockKind::kParagraph;
  prompt.text = QStringLiteral("Check the pricing again.");
  third.blocks.push_back(prompt);
  snapshot->messages.push_back(third);
  snapshot->revision = 2;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);

  view.SetSearchQuery(QStringLiteral("PRICING"));
  QCOMPARE(view.SearchMatchCount(), 2);
  QCOMPARE(view.CurrentSearchMatch(), 0);
  view.FindNext();
  QCOMPARE(view.CurrentSearchMatch(), 1);
  view.FindNext();
  QCOMPARE(view.CurrentSearchMatch(), 0);
  view.FindPrevious();
  QCOMPARE(view.CurrentSearchMatch(), 1);

  view.SetSearchQuery(QStringLiteral("no such text"));
  QCOMPARE(view.SearchMatchCount(), 0);
  QCOMPARE(view.CurrentSearchMatch(), -1);
  view.FindNext();
  QCOMPARE(view.CurrentSearchMatch(), -1);
  view.ClearSearch();
  QCOMPARE(view.SearchMatchCount(), 0);

  // A snapshot refresh keeps the query live and re-counts matches.
  view.SetSearchQuery(QStringLiteral("attached"));
  QCOMPARE(view.SearchMatchCount(), 1);
  auto refreshed = std::make_shared<hermes::ui::ProfileSnapshot>(*snapshot);
  refreshed->revision = 3;
  view.SetSnapshot(refreshed);
  QCOMPARE(view.SearchMatchCount(), 1);
}

void ChatViewTest::approvalRendersAsInlineTranscriptCard() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  snapshot->revision = 2;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);
  QWidget* transcript = view.widget();
  QVERIFY(transcript != nullptr);
  const int without_card = transcript->height();

  hermes::ui::PendingRequest request;
  request.id = QStringLiteral("approval-1");
  request.method = QStringLiteral("approval");
  request.session_id = QStringLiteral("s1");
  request.title = QStringLiteral("Run a shell command");
  request.detail = QStringLiteral("rm -rf build");
  request.choices = {QStringLiteral("once"), QStringLiteral("deny")};
  QString answered_id;
  QJsonObject answer;
  view.SetRequestRespondHandler(
      [&answered_id, &answer](const QString& id, const QJsonObject& result) {
        answered_id = id;
        answer = result;
      });
  view.SetRequest(request);
  QTest::qWait(30);

  auto* card = dynamic_cast<hermes::ui::RequestBar*>(
      transcript->findChild<QWidget*>(QStringLiteral("requestBar")));
  QVERIFY(card != nullptr);
  QVERIFY(card->isVisible());
  QVERIFY(transcript->height() > without_card);
  QVERIFY(card->geometry().top() > 0);
  QVERIFY(card->geometry().bottom() < transcript->height());
  QPushButton* allow = nullptr;
  for (QPushButton* button : card->findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Allow once")) {
      allow = button;
      break;
    }
  }
  QVERIFY(allow != nullptr);
  allow->click();
  QCOMPARE(answered_id, QStringLiteral("approval-1"));
  QCOMPARE(answer.value(QStringLiteral("choice")).toString(),
           QStringLiteral("once"));

  view.ClearRequest();
  QTest::qWait(20);
  QVERIFY(!card->isVisible());
  QCOMPARE(transcript->height(), without_card);
}

void ChatViewTest::clarifyOptionsStackAndWrapInsideCard() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(620, 800);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  snapshot->revision = 2;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);

  hermes::ui::PendingRequest request;
  request.id = QStringLiteral("clarify-1");
  request.method = QStringLiteral("clarify");
  request.session_id = QStringLiteral("s1");
  request.title = QStringLiteral("Clarification needed");
  request.detail = QStringLiteral("The agent is waiting for an answer.");
  hermes::ui::ClarifyQuestion question;
  question.qid = QStringLiteral("q1");
  question.question = QStringLiteral("Which deployment target should I use?");
  const QString short_choice = QStringLiteral("Staging only (Recommended)");
  const QString long_choice = QStringLiteral(
      "A much longer option that explains the staging rollout in enough "
      "detail that it has to wrap onto another line instead of widening "
      "the card or being clipped at the right edge");
  question.choices = {short_choice, QStringLiteral("Staging and production"),
                      long_choice};
  request.questions.push_back(question);
  view.SetRequest(request);
  QTest::qWait(40);

  auto* card = dynamic_cast<hermes::ui::RequestBar*>(
      view.widget()->findChild<QWidget*>(QStringLiteral("requestBar")));
  QVERIFY(card != nullptr);
  QVERIFY(card->isVisible());

  QPushButton* short_button = nullptr;
  QPushButton* long_button = nullptr;
  QPushButton* send = nullptr;
  for (QPushButton* button : card->findChildren<QPushButton*>()) {
    if (button->text() == short_choice) {
      short_button = button;
    } else if (button->text() == long_choice) {
      long_button = button;
    } else if (button->text() == QStringLiteral("Send")) {
      send = button;
    }
  }
  QVERIFY(short_button != nullptr);
  QVERIFY(long_button != nullptr);
  QVERIFY(send != nullptr);

  // Choices are full-width rows, stacked vertically, not grid cells.
  QCOMPARE(short_button->width(), long_button->width());
  QVERIFY(long_button->height() > short_button->height());
  QVERIFY(long_button->geometry().top() > short_button->geometry().bottom());

  constexpr int kCardInset = 24;
  constexpr int kCardPadding = 12;
  const int content_left = kCardInset + kCardPadding;
  const int content_right = card->width() - kCardInset - kCardPadding;
  const int long_left = long_button->mapTo(card, QPoint(0, 0)).x();
  const int long_right =
      long_button->mapTo(card, QPoint(long_button->width(), 0)).x();
  QVERIFY(qAbs(long_left - content_left) <= 1);
  QVERIFY(qAbs(long_right - content_right) <= 1);

  // The footer belongs inside the painted card, not past its right edge.
  const int send_right = send->mapTo(card, QPoint(send->width(), 0)).x();
  const int send_bottom = send->mapTo(card, QPoint(0, send->height())).y();
  QVERIFY(send_right <= content_right + 1);
  QVERIFY(send_bottom <= card->height() - 4);

  // A narrower composer must grow the wrapped rows, update the card height,
  // and keep the footer inside the repainted card.
  const int wide_card_height = card->height();
  const int wide_choice_height = long_button->height();
  view.resize(430, 800);
  QTest::qWait(80);
  QVERIFY(card->height() > wide_card_height);
  QVERIFY(long_button->height() > wide_choice_height);
  const int narrow_content_right = card->width() - kCardInset - kCardPadding;
  const int narrow_send_right = send->mapTo(card, QPoint(send->width(), 0)).x();
  const int narrow_send_bottom =
      send->mapTo(card, QPoint(0, send->height())).y();
  QVERIFY(narrow_send_right <= narrow_content_right + 1);
  QVERIFY(narrow_send_bottom <= card->height() - 4);

  view.ClearRequest();
}

void ChatViewTest::connectionCardAnswersTargetsAndContinues() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  snapshot->revision = 5;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);

  hermes::ui::PendingRequest request;
  request.id = QStringLiteral("op-1");
  request.method = QStringLiteral("connection");
  request.session_id = QStringLiteral("s1");
  request.title = QStringLiteral("Connection needed");
  request.detail = QStringLiteral("2 targets · backend-owned state");
  request.revision = 3;
  request.connection.op_id = QStringLiteral("op-1");
  request.connection.seq = 3;
  request.connection.suggested = {QStringLiteral("github")};
  hermes::ui::ConnectionTarget github;
  github.name = QStringLiteral("github");
  github.kind = QStringLiteral("mcp");
  github.action = QStringLiteral("install");
  github.state = QStringLiteral("pending");
  github.detail = QStringLiteral("GitHub MCP");
  github.connect_url = QStringLiteral("https://example.test/oauth");
  github.required_env.push_back(
      hermes::ui::ConnectionEnvField{.name = QStringLiteral("GITHUB_TOKEN"),
                                     .prompt = QStringLiteral("Token"),
                                     .required = true,
                                     .secret = true});
  request.connection.targets.push_back(github);
  hermes::ui::ConnectionTarget slack;
  slack.name = QStringLiteral("slack");
  slack.kind = QStringLiteral("connector");
  slack.action = QStringLiteral("connect");
  slack.state = QStringLiteral("connected");
  request.connection.targets.push_back(slack);

  QString answered_id;
  QJsonObject answer;
  view.SetRequestRespondHandler(
      [&answered_id, &answer](const QString& id, const QJsonObject& result) {
        answered_id = id;
        answer = result;
      });
  view.SetRequest(request);
  QTest::qWait(30);

  auto* card = dynamic_cast<hermes::ui::RequestBar*>(
      view.widget()->findChild<QWidget*>(QStringLiteral("requestBar")));
  QVERIFY(card != nullptr);
  QVERIFY(card->isVisible());
  QStringList labels;
  for (QLabel* label : card->findChildren<QLabel*>()) {
    labels.push_back(label->text());
  }
  QVERIFY(labels.contains(QStringLiteral("github · mcp · pending")));
  QVERIFY(labels.contains(QStringLiteral("slack · connector · connected")));
  QVERIFY(labels.contains(QStringLiteral("Suggested connectors: github")));
  QVERIFY(labels.contains(QStringLiteral("GitHub MCP")) ||
          labels.contains(QStringLiteral("Install the connector")));

  QLineEdit* token = card->findChild<QLineEdit*>();
  QVERIFY(token != nullptr);
  token->setText(QStringLiteral("ghp_x"));

  QPushButton* approve = nullptr;
  QPushButton* cont = nullptr;
  for (QPushButton* button : card->findChildren<QPushButton*>()) {
    if (button->text() == QStringLiteral("Approve")) {
      approve = button;
    } else if (button->text() == QStringLiteral("Continue")) {
      cont = button;
    }
  }
  QVERIFY(approve != nullptr);
  QVERIFY(cont != nullptr);
  approve->click();
  QCOMPARE(answered_id, QStringLiteral("op-1"));
  const QJsonArray targets = answer.value(QStringLiteral("targets")).toArray();
  QCOMPARE(targets.size(), 1);
  QCOMPARE(targets[0].toObject().value(QStringLiteral("name")).toString(),
           QStringLiteral("github"));
  QCOMPARE(targets[0].toObject().value(QStringLiteral("status")).toString(),
           QStringLiteral("approved"));
  QCOMPARE(targets[0]
               .toObject()
               .value(QStringLiteral("env"))
               .toObject()
               .value(QStringLiteral("GITHUB_TOKEN"))
               .toString(),
           QStringLiteral("ghp_x"));

  cont->click();
  QCOMPARE(answer.value(QStringLiteral("settled_by")).toString(),
           QStringLiteral("continue"));
}

void ChatViewTest::connectionCardYieldsToTypedText() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  snapshot->revision = 7;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);

  hermes::ui::PendingRequest request;
  request.id = QStringLiteral("op-target");
  request.method = QStringLiteral("connection");
  request.session_id = QStringLiteral("s1");
  request.title = QStringLiteral("Connection needed");
  request.revision = 2;
  request.connection.op_id = QStringLiteral("op-target");
  request.connection.seq = 2;
  hermes::ui::ConnectionTarget github;
  github.name = QStringLiteral("github");
  github.kind = QStringLiteral("connector");
  github.action = QStringLiteral("connect");
  github.state = QStringLiteral("pending");
  request.connection.targets.push_back(github);

  QString answered_id;
  QJsonObject answer;
  view.SetRequestRespondHandler(
      [&answered_id, &answer](const QString& id, const QJsonObject& result) {
        answered_id = id;
        answer = result;
      });
  view.SetRequest(request);
  QTest::qWait(20);
  QVERIFY(view.HasConnectionRequest());
  QVERIFY(!view.AnswerConnectionDisplay(QStringLiteral("hello")));
  QVERIFY(view.ContinueConnection());
  QCOMPARE(answered_id, QStringLiteral("op-target"));
  QCOMPARE(answer.value(QStringLiteral("settled_by")).toString(),
           QStringLiteral("continue"));
  QVERIFY(!answer.contains(QStringLiteral("said")));
}

void ChatViewTest::connectionDisplayCardUsesComposerText() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(900, 700);
  view.show();
  auto snapshot = SnapshotWithReasoning(false);
  snapshot->revision = 6;
  view.SetSnapshot(snapshot);
  QTest::qWait(20);

  hermes::ui::PendingRequest request;
  request.id = QStringLiteral("op-display");
  request.method = QStringLiteral("connection");
  request.session_id = QStringLiteral("s1");
  request.title = QStringLiteral("Connector directory");
  request.revision = 1;
  request.connection.op_id = QStringLiteral("op-display");
  request.connection.seq = 1;
  request.connection.suggested = {QStringLiteral("github"),
                                  QStringLiteral("notion")};
  QString answered_id;
  QJsonObject answer;
  view.SetRequestRespondHandler(
      [&answered_id, &answer](const QString& id, const QJsonObject& result) {
        answered_id = id;
        answer = result;
      });
  view.SetRequest(request);
  QTest::qWait(20);
  QVERIFY(view.AnswerConnectionDisplay(QStringLiteral("  use github  ")));
  QCOMPARE(answered_id, QStringLiteral("op-display"));
  QCOMPARE(answer.value(QStringLiteral("said")).toString(),
           QStringLiteral("use github"));
  QCOMPARE(answer.value(QStringLiteral("settled_by")).toString(),
           QStringLiteral("continue"));
  QVERIFY(!view.AnswerConnectionDisplay(QString()));
}

void ChatViewTest::reactionFooterAddsTranscriptHeight() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();

  auto snapshot = SnapshotWithReasoning(false);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);
  const int without_reactions = view.widget()->height();

  snapshot->revision = 2;
  snapshot->messages.front().reactions.push_back(hermes::ui::ChatReaction{
      .emoji = QStringLiteral("👍"), .author = QStringLiteral("you")});
  view.SetSnapshot(snapshot);
  QTest::qWait(30);
  QVERIFY(view.widget()->height() > without_reactions);
}

QTEST_MAIN(ChatViewTest)

#include "chat_view_test.moc"
