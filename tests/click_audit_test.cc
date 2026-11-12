// Copyright 2026 The Hermes Qt Spike Authors.
//
// Click audit: the shell's controls are custom-painted, so nothing guarantees
// a painted region is wired to anything. This walks every ClickTarget each
// surface exposes, clicks it, and fails when the region is dead. Modal targets
// (native file dialog / menu) are recorded but not clicked.

#include <QClipboard>
#include <QGuiApplication>
#include <QListWidget>
#include <QMap>
#include <QShortcut>
#include <QSignalSpy>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtTest>
#include <memory>
#include <vector>

#include "src/chat_view.h"
#include "src/click_target.h"
#include "src/command_palette.h"
#include "src/composer.h"
#include "src/sidebar.h"
#include "src/slash_commands.h"
#include "src/theme.h"
#include "src/title_bar.h"

namespace {

QPoint Center(const QRectF& rect) {
  return QPoint(static_cast<int>(rect.center().x()),
                static_cast<int>(rect.center().y()));
}

hermes::ui::ProfileSnapshot StreamingSnapshot() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");
  snapshot.revision = 1;
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  message.reasoning =
      QStringLiteral("First I read the file.\nThen I ran the tests.");
  message.reasoning_streaming = false;
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kParagraph;
  block.text = QStringLiteral("Done.");
  message.blocks.push_back(block);
  snapshot.messages.push_back(message);
  return snapshot;
}

}  // namespace

class ClickAuditTest : public QObject {
  Q_OBJECT

 private slots:
  void titleBarTargetsFire();
  void commandPaletteRunsActionOnSingleClick();
  void sidebarTargetsFire();
  void composerTargetsFire();
  void composerAttachmentChipsAttachAndRemove();
  void reasoningHeaderTogglesBothWays();
  void codeCopyTargetCopies();
  void toolRowTargetToggles();
  void slashMenuFiltersAndAccepts();
  void shortcutFiresWhileComposerFocused();
  void gatewayCatalogDrivesCompletion();
  void copyChipsCopyEveryBubble();
  void systemNoticesShareHoverActionRow();
};

void ClickAuditTest::titleBarTargetsFire() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::TitleBar bar(theme);
  bar.resize(900, theme.title_bar_height);
  bar.show();

  int searches = 0;
  int settings = 0;
  bar.on_search = [&searches]() { ++searches; };
  bar.on_settings = [&settings]() { ++settings; };

  for (const hermes::ui::ClickTarget& target : bar.ClickTargets()) {
    if (target.modal) {
      continue;
    }
    QTest::mouseClick(&bar, Qt::LeftButton, Qt::NoModifier,
                      Center(target.rect));
  }
  QCOMPARE(searches, 1);
  QCOMPARE(settings, 1);
}

void ClickAuditTest::commandPaletteRunsActionOnSingleClick() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::CommandPalette palette(theme);
  palette.resize(800, 600);
  int runs = 0;
  palette.SetCommands(
      {{QStringLiteral("New chat"), QStringLiteral("Start a fresh session"),
        QStringLiteral("Actions"), [&runs]() { ++runs; }}});
  palette.Open();
  auto* list = palette.findChild<QListWidget*>(QStringLiteral("paletteList"));
  QVERIFY(list != nullptr);
  QTRY_COMPARE(list->count(), 1);
  const QRect row = list->visualItemRect(list->item(0));
  QVERIFY(row.isValid());
  QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier,
                    row.center());
  QCOMPARE(runs, 1);
  QVERIFY(!palette.IsOpen());
}

void ClickAuditTest::sidebarTargetsFire() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Session one");
  snapshot.sessions.push_back(session);
  sidebar.SetSnapshot(snapshot);
  std::vector<hermes::ui::ProfileChip> chips;
  for (const char* label : {"default", "work"}) {
    hermes::ui::ProfileChip chip;
    chip.label = QLatin1String(label);
    chips.push_back(chip);
  }
  sidebar.SetProfileChips(chips, 0);

  QStringList nav;
  int new_chat = 0;
  int profiles = 0;
  int added = 0;
  QString selected;
  sidebar.on_nav = [&nav](const QString& id) { nav.append(id); };
  sidebar.on_new_chat = [&new_chat]() { ++new_chat; };
  sidebar.on_profile_selected = [&profiles](int) { ++profiles; };
  sidebar.on_add_profile = [&added]() { ++added; };
  sidebar.on_session_selected = [&selected](const QString& id) {
    selected = id;
  };

  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.modal) {
      continue;
    }
    QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                      Center(target.rect));
  }

  QCOMPARE(new_chat, 1);
  QCOMPARE(nav.size(), 6);
  QVERIFY(nav.contains(QStringLiteral("capabilities")));
  QVERIFY(nav.contains(QStringLiteral("webhooks")));
  QVERIFY(nav.contains(QStringLiteral("profiles")));
  QCOMPARE(profiles, 2);
  QCOMPARE(added, 1);
  QCOMPARE(selected, QStringLiteral("s1"));
}

void ClickAuditTest::composerTargetsFire() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(700, 120);
  composer.show();

  QSignalSpy submitted(&composer, &hermes::ui::Composer::Submitted);
  QSignalSpy attach(&composer, &hermes::ui::Composer::AttachRequested);
  composer.SetText(QStringLiteral("hello"));
  for (const hermes::ui::ClickTarget& target : composer.ClickTargets()) {
    if (target.modal) {
      continue;
    }
    QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier,
                      Center(target.rect));
  }
  QCOMPARE(submitted.count(), 1);
  QCOMPARE(submitted.at(0).at(0).toString(), QStringLiteral("hello"));
  QCOMPARE(attach.count(), 0);
}

void ClickAuditTest::composerAttachmentChipsAttachAndRemove() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(700, 120);
  composer.show();
  composer.AddAttachment(QStringLiteral("notes.txt"),
                         QStringLiteral("/tmp/uploads/a-notes.txt"));
  composer.AddAttachment(QStringLiteral("image.png"),
                         QStringLiteral("/tmp/uploads/b-image.png"));
  QCOMPARE(composer.AttachmentPaths().size(), 2);

  // Each staged file paints a chip the user can click to detach it.
  QRectF first_chip;
  int chips = 0;
  for (const hermes::ui::ClickTarget& target : composer.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("composer:attachment:"))) {
      ++chips;
      if (first_chip.isNull()) {
        first_chip = target.rect;
      }
    }
  }
  QCOMPARE(chips, 2);

  QSignalSpy submitted(&composer, &hermes::ui::Composer::Submitted);
  composer.SetText(QStringLiteral("what is in this file?"));
  for (const hermes::ui::ClickTarget& target : composer.ClickTargets()) {
    if (target.name == QStringLiteral("composer:send")) {
      QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier,
                        Center(target.rect));
    }
  }
  QCOMPARE(submitted.count(), 1);
  const QString payload = submitted.at(0).at(0).toString();
  QVERIFY(payload.contains(QStringLiteral("Attached files:")));
  QVERIFY(payload.contains(QStringLiteral("/tmp/uploads/a-notes.txt")));
  QVERIFY(payload.contains(QStringLiteral("/tmp/uploads/b-image.png")));

  // Re-stage and remove one by clicking its chip.
  composer.AddAttachment(QStringLiteral("notes.txt"),
                         QStringLiteral("/tmp/uploads/a-notes.txt"));
  QCOMPARE(composer.AttachmentPaths().size(), 1);
  QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier,
                    Center(first_chip));
  QCOMPARE(composer.AttachmentPaths().size(), 0);
}

void ClickAuditTest::reasoningHeaderTogglesBothWays() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 600);
  view.show();
  auto snapshot =
      std::make_shared<hermes::ui::ProfileSnapshot>(StreamingSnapshot());
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  std::vector<hermes::ui::ClickTarget> targets;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:reasoning:"))) {
      targets.push_back(target);
    }
  }
  QCOMPARE(static_cast<int>(targets.size()), 1);
  QCOMPARE(targets.front().name, QStringLiteral("chat:reasoning:0"));

  // A finished turn starts collapsed; the first click must expand it.
  QVERIFY(!view.ReasoningExpanded(0));
  const int collapsed_height = view.widget()->height();
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(targets.front().rect));
  QTest::qWait(20);
  QVERIFY(view.ReasoningExpanded(0));
  QVERIFY(view.widget()->height() > collapsed_height);

  // And the second click collapses it again.
  QRectF header_rect;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name == QStringLiteral("chat:reasoning:0")) {
      header_rect = target.rect;
    }
  }
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(header_rect));
  QTest::qWait(20);
  QVERIFY(!view.ReasoningExpanded(0));
  QCOMPARE(view.widget()->height(), collapsed_height);
}

void ClickAuditTest::codeCopyTargetCopies() {
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
  hermes::ui::MessageBlock code;
  code.kind = hermes::ui::BlockKind::kCode;
  code.language = QStringLiteral("bash");
  code.text = QStringLiteral("echo native-copy");
  message.blocks.push_back(code);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QGuiApplication::clipboard()->clear();
  QString copy_target;
  QRectF copy_rect;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:copy:"))) {
      copy_target = target.name;
      copy_rect = target.rect;
    }
  }
  QCOMPARE(copy_target, QStringLiteral("chat:copy:0:0"));
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(copy_rect));
  QTest::qWait(20);
  QCOMPARE(QGuiApplication::clipboard()->text(),
           QStringLiteral("echo native-copy"));
}

void ClickAuditTest::toolRowTargetToggles() {
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
  hermes::ui::MessageBlock tool;
  tool.kind = hermes::ui::BlockKind::kTool;
  tool.text = QStringLiteral("terminal");
  tool.detail = QStringLiteral(
      "$ cmake --build build\n[ 33%] Building CXX object\n"
      "ld: undefined symbol: hermes_ui_main\n"
      "clang: error: linker command failed with exit code 1");
  message.blocks.push_back(tool);
  snapshot->messages.push_back(message);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QRectF tool_rect;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name == QStringLiteral("chat:tool:0:0")) {
      tool_rect = target.rect;
    }
  }
  QVERIFY(!tool_rect.isNull());
  const int collapsed = view.widget()->height();
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(tool_rect));
  QTest::qWait(20);
  QVERIFY2(view.widget()->height() > collapsed,
           "clicking a tool row must expand its output");

  QRectF expanded_rect;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name == QStringLiteral("chat:tool:0:0")) {
      expanded_rect = target.rect;
    }
  }
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(expanded_rect));
  QTest::qWait(20);
  QCOMPARE(view.widget()->height(), collapsed);
}

void ClickAuditTest::slashMenuFiltersAndAccepts() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(700, 120);
  composer.show();
  QTest::qWait(20);

  composer.SetText(QStringLiteral("/"));
  QTest::qWait(20);
  QVERIFY(composer.SlashMenuVisible());
  QCOMPARE(composer.SlashMenuEntries().size(),
           static_cast<int>(hermes::ui::SlashCommandCatalog().size()));

  composer.SetText(QStringLiteral("/reas"));
  QTest::qWait(20);
  QVERIFY(composer.SlashMenuVisible());
  QCOMPARE(composer.SlashMenuEntries().size(), 1);
  QVERIFY(composer.SlashMenuEntries().front().startsWith(
      QStringLiteral("/reasoning")));

  auto* editor = composer.findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  QTest::keyClick(editor, Qt::Key_Tab);
  QTest::qWait(20);
  QCOMPARE(composer.Text(), QStringLiteral("/reasoning "));
  QVERIFY(!composer.SlashMenuVisible());

  // A complete command submits instead of completing (so "/new<Enter>" runs).
  composer.SetText(QStringLiteral("/new"));
  QTest::qWait(20);
  QSignalSpy submitted(&composer, &hermes::ui::Composer::Submitted);
  QTest::keyClick(editor, Qt::Key_Return);
  QTest::qWait(20);
  QCOMPARE(submitted.count(), 1);
  QCOMPARE(submitted.at(0).at(0).toString(), QStringLiteral("/new"));
}

void ClickAuditTest::shortcutFiresWhileComposerFocused() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  QWidget window;
  auto* layout = new QVBoxLayout(&window);
  auto* composer = new hermes::ui::Composer(theme, &window);
  layout->addWidget(composer);
  window.resize(700, 200);
  window.show();
  QTest::qWait(20);

  int fired = 0;
  auto* shortcut =
      new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), &window);
  shortcut->setContext(Qt::WindowShortcut);
  QObject::connect(shortcut, &QShortcut::activated, [&fired]() { ++fired; });

  auto* editor = composer->findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  editor->setFocus();
  QTest::qWait(20);
  QTest::keyClick(editor, Qt::Key_K, Qt::ControlModifier);
  QTest::qWait(30);
  QCOMPARE(fired, 1);
}

void ClickAuditTest::gatewayCatalogDrivesCompletion() {
  // The gateway catalog (commands.catalog) is the source of truth: commands
  // the native fast path does not know, like /steer, must still complete.
  std::vector<hermes::ui::SlashCommand> catalog;
  hermes::ui::SlashCommand steer;
  steer.name = QStringLiteral("/steer");
  steer.description = QStringLiteral(
      "Inject a message after the next tool call without interrupting");
  steer.argument_hint = QStringLiteral("<prompt>");
  steer.takes_args = true;
  catalog.push_back(steer);
  hermes::ui::SlashCommand status;
  status.name = QStringLiteral("/status");
  status.description = QStringLiteral("Show session, model, token info");
  catalog.push_back(status);

  const auto matches =
      hermes::ui::MatchSlashCommands(catalog, QStringLiteral("/ste"));
  QCOMPARE(static_cast<int>(matches.size()), 1);
  QCOMPARE(matches.front()->name, QStringLiteral("/steer"));

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(700, 120);
  composer.show();
  composer.SetSlashCommands(catalog);
  composer.SetText(QStringLiteral("/ste"));
  QTest::qWait(20);
  QVERIFY(composer.SlashMenuVisible());
  QVERIFY(
      composer.SlashMenuEntries().front().startsWith(QStringLiteral("/steer")));

  auto* editor = composer.findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  QTest::keyClick(editor, Qt::Key_Tab);
  QTest::qWait(20);
  QCOMPARE(composer.Text(), QStringLiteral("/steer "));
}

void ClickAuditTest::copyChipsCopyEveryBubble() {
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
  hermes::ui::MessageBlock question;
  question.kind = hermes::ui::BlockKind::kParagraph;
  question.text = QStringLiteral("What broke the build?");
  user.blocks.push_back(question);
  snapshot->messages.push_back(user);

  hermes::ui::ChatMessage answer;
  answer.role = hermes::ui::MessageRole::kAssistant;
  answer.author = QStringLiteral("Hermes");
  answer.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock paragraph;
  paragraph.kind = hermes::ui::BlockKind::kParagraph;
  paragraph.text = QStringLiteral("The linker dropped main.cc.");
  answer.blocks.push_back(paragraph);
  hermes::ui::MessageBlock tool;
  tool.kind = hermes::ui::BlockKind::kTool;
  tool.text = QStringLiteral("terminal");
  tool.detail = QStringLiteral("ld: undefined symbol: hermes_ui_main");
  answer.blocks.push_back(tool);
  snapshot->messages.push_back(answer);

  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  QMap<QString, QRectF> chips;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("chat:copymsg:"))) {
      chips.insert(target.name, target.rect);
    }
  }
  QVERIFY(chips.contains(QStringLiteral("chat:copymsg:0")));
  QVERIFY(chips.contains(QStringLiteral("chat:copymsg:1")));

  QGuiApplication::clipboard()->clear();
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(chips.value(QStringLiteral("chat:copymsg:0"))));
  QTest::qWait(20);
  QCOMPARE(QGuiApplication::clipboard()->text(),
           QStringLiteral("What broke the build?"));

  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(chips.value(QStringLiteral("chat:copymsg:1"))));
  QTest::qWait(20);
  const QString answer_text = QGuiApplication::clipboard()->text();
  QVERIFY(answer_text.contains(QStringLiteral("The linker dropped main.cc.")));
  // A tool row's copyable text is its output, not its name.
  QVERIFY(answer_text.contains(QStringLiteral("ld: undefined symbol")));
}

void ClickAuditTest::systemNoticesShareHoverActionRow() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ChatView view(theme);
  view.resize(800, 400);
  view.show();

  auto snapshot = std::make_shared<hermes::ui::ProfileSnapshot>();
  snapshot->name = QStringLiteral("default");
  snapshot->label = QStringLiteral("default");
  snapshot->revision = 1;
  hermes::ui::ChatMessage notice;
  notice.role = hermes::ui::MessageRole::kSystem;
  notice.timestamp = QDateTime::currentDateTime();
  hermes::ui::MessageBlock text;
  text.kind = hermes::ui::BlockKind::kParagraph;
  text.text = QStringLiteral("Steer queued — the model reads it next.");
  notice.blocks.push_back(text);
  snapshot->messages.push_back(notice);
  view.SetSnapshot(snapshot);
  QTest::qWait(30);

  // System notices are items too: they mount the same reserved hover row,
  // with Copy / Copy full and none of the reply-only actions.
  QRectF copy_rect;
  for (const hermes::ui::ClickTarget& target : view.ClickTargets()) {
    if (target.name == QStringLiteral("chat:copymsg:0")) {
      copy_rect = target.rect;
    }
    QVERIFY(target.name != QStringLiteral("chat:speakmsg:0"));
    QVERIFY(target.name != QStringLiteral("chat:branchmsg:0"));
    QVERIFY(target.name != QStringLiteral("chat:reactmsg:0"));
  }
  QVERIFY(!copy_rect.isNull());

  QGuiApplication::clipboard()->clear();
  QTest::mouseClick(view.widget(), Qt::LeftButton, Qt::NoModifier,
                    Center(copy_rect));
  QTest::qWait(20);
  QCOMPARE(QGuiApplication::clipboard()->text(),
           QStringLiteral("Steer queued — the model reads it next."));
  QVERIFY(view.widget()->height() > 0);
}

QTEST_MAIN(ClickAuditTest)

#include "click_audit_test.moc"
