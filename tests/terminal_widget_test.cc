// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/terminal_widget.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QScopedPointer>
#include <QTemporaryDir>
#include <QtTest>

#include "src/theme.h"

namespace hermes::ui {
namespace {

class TerminalWidgetTest : public QObject {
  Q_OBJECT

 private slots:
  void parsesPlainTextAndCarriageReturn();
  void handlesCursorAddressingAndErase();
  void appliesSgrColors();
  void switchesToAlternateScreen();
  void updatesTitleFromOsc();
  void selectsWithMouseAndCopies();
  void singleClickDoesNotSelectOneCell();
  void selectAllAndKeyboardCopy();
  void contextMenuProvidesClipboardActions();
  void pastesClipboardThroughPty();
  void runsShellCommandThroughPty();
};

void TerminalWidgetTest::parsesPlainTextAndCarriageReturn() {
  TerminalWidget terminal;
  terminal.FeedForTest("hello\r\nworld");
  const QStringList lines = terminal.ScreenLinesForTest();
  QCOMPARE(lines.value(0), QStringLiteral("hello"));
  QCOMPARE(lines.value(1), QStringLiteral("world"));
  QCOMPARE(terminal.CursorRowForTest(), 1);
  QCOMPARE(terminal.CursorColumnForTest(), 5);
}

void TerminalWidgetTest::handlesCursorAddressingAndErase() {
  TerminalWidget terminal;
  terminal.FeedForTest("abcdef");
  terminal.FeedForTest("\x1b[3D");
  terminal.FeedForTest("\x1b[K");
  QCOMPARE(terminal.ScreenLinesForTest().value(0), QStringLiteral("abc"));

  terminal.FeedForTest("\x1b[2;3H");
  terminal.FeedForTest("X");
  QCOMPARE(terminal.CursorRowForTest(), 1);
  QCOMPARE(terminal.CursorColumnForTest(), 3);
  QCOMPARE(terminal.ScreenLinesForTest().value(1).left(3),
           QStringLiteral("  X"));
}

void TerminalWidgetTest::appliesSgrColors() {
  TerminalWidget terminal;
  terminal.SetTheme(Theme::Dark());
  terminal.FeedForTest("\x1b[31mred\x1b[0m plain");
  QCOMPARE(terminal.CellForegroundForTest(0, 0), Theme::Dark().danger);
  QCOMPARE(terminal.CellForegroundForTest(0, 4), Theme::Dark().code_text);
  QCOMPARE(terminal.CellBackgroundForTest(0, 0), Theme::Dark().code_background);
}

void TerminalWidgetTest::switchesToAlternateScreen() {
  TerminalWidget terminal;
  terminal.FeedForTest("primary");
  terminal.FeedForTest("\x1b[?1049h");
  terminal.FeedForTest("alternate");
  QCOMPARE(terminal.ScreenLinesForTest().value(0), QStringLiteral("alternate"));
  terminal.FeedForTest("\x1b[?1049l");
  QCOMPARE(terminal.ScreenLinesForTest().value(0), QStringLiteral("primary"));
}

void TerminalWidgetTest::updatesTitleFromOsc() {
  TerminalWidget terminal;
  QSignalSpy spy(&terminal, &TerminalWidget::TitleChanged);
  terminal.FeedForTest("\x1b]0;hermes terminal\x07");
  QCOMPARE(terminal.Title(), QStringLiteral("hermes terminal"));
  QCOMPARE(spy.count(), 1);
}

void TerminalWidgetTest::selectsWithMouseAndCopies() {
  TerminalWidget terminal;
  terminal.resize(640, 240);
  terminal.show();
  terminal.FeedForTest("hello world\r\nsecond line");
  QCoreApplication::processEvents();

  const QSize cell = terminal.CellSizeForTest();
  const int padding = terminal.PaddingForTest();
  const QPoint start(padding + cell.width() / 2, padding + cell.height() / 2);
  const QPoint end(padding + cell.width() * 4 + cell.width() / 2,
                   padding + cell.height() / 2);
  QTest::mousePress(&terminal, Qt::LeftButton, Qt::NoModifier, start);
  QMouseEvent move_event(QEvent::MouseMove, QPointF(end),
                         QPointF(terminal.mapToGlobal(end)), Qt::NoButton,
                         Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&terminal, &move_event);
  QTest::mouseRelease(&terminal, Qt::LeftButton, Qt::NoModifier, end);

  QVERIFY(terminal.HasSelection());
  QCOMPARE(terminal.SelectedText(), QStringLiteral("hello"));

  QGuiApplication::clipboard()->clear();
  terminal.CopySelection();
  QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello"));

  terminal.ClearSelection();
  QVERIFY(!terminal.HasSelection());
}

void TerminalWidgetTest::singleClickDoesNotSelectOneCell() {
  TerminalWidget terminal;
  terminal.resize(640, 240);
  terminal.show();
  terminal.FeedForTest("hello world");
  QCoreApplication::processEvents();

  terminal.SelectAll();
  QVERIFY(terminal.HasSelection());
  const int padding = terminal.PaddingForTest();
  const QSize cell = terminal.CellSizeForTest();
  const QPoint point(padding + cell.width() / 2, padding + cell.height() / 2);
  QTest::mousePress(&terminal, Qt::LeftButton, Qt::NoModifier, point);
  QTest::mouseRelease(&terminal, Qt::LeftButton, Qt::NoModifier, point);
  QVERIFY(!terminal.HasSelection());
}

void TerminalWidgetTest::selectAllAndKeyboardCopy() {
  TerminalWidget terminal;
  terminal.resize(640, 240);
  terminal.show();
  terminal.FeedForTest("hello world\r\nsecond line");
  QCoreApplication::processEvents();

  terminal.SelectAll();
  QVERIFY(terminal.HasSelection());
  const QString all = terminal.SelectedText();
  QVERIFY(all.startsWith(QStringLiteral("hello world")));
  QVERIFY(all.contains(QStringLiteral("second line")));

  QGuiApplication::clipboard()->clear();
  QTest::keyClick(&terminal, Qt::Key_C,
                  Qt::ControlModifier | Qt::ShiftModifier);
  QCOMPARE(QGuiApplication::clipboard()->text(), all);
}

void TerminalWidgetTest::contextMenuProvidesClipboardActions() {
  TerminalWidget terminal;
  terminal.FeedForTest("hello world\r\nsecond line");
  terminal.SelectRangeForTest(0, 0, 0, 4);

  QScopedPointer<QMenu> menu(terminal.CreateContextMenuForTest(&terminal));
  auto* copy = menu->findChild<QAction*>(QStringLiteral("terminalContextCopy"));
  auto* paste =
      menu->findChild<QAction*>(QStringLiteral("terminalContextPaste"));
  auto* select_all =
      menu->findChild<QAction*>(QStringLiteral("terminalContextSelectAll"));
  QVERIFY(copy != nullptr);
  QVERIFY(paste != nullptr);
  QVERIFY(select_all != nullptr);
  QVERIFY(copy->isEnabled());
  QVERIFY(select_all->isEnabled());

  QGuiApplication::clipboard()->clear();
  copy->trigger();
  QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("hello"));

  terminal.ClearSelection();
  select_all->trigger();
  QVERIFY(terminal.HasSelection());
  terminal.ClearSelection();
  QScopedPointer<QMenu> empty_menu(
      terminal.CreateContextMenuForTest(&terminal));
  auto* disabled_copy =
      empty_menu->findChild<QAction*>(QStringLiteral("terminalContextCopy"));
  QVERIFY(disabled_copy != nullptr);
  QVERIFY(!disabled_copy->isEnabled());
}

void TerminalWidgetTest::pastesClipboardThroughPty() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TerminalWidget terminal;
  terminal.resize(720, 420);
  terminal.SetShellForTest(QStringLiteral("/bin/sh"));
  terminal.SetWorkingDirectory(directory.path());
  QVERIFY(terminal.Start());
  QTRY_VERIFY_WITH_TIMEOUT(terminal.IsRunning(), 3000);

  QGuiApplication::clipboard()->setText(
      QStringLiteral("printf '__hermes_paste_ok__\\n'\r"));
  QScopedPointer<QMenu> menu(terminal.CreateContextMenuForTest(&terminal));
  auto* paste =
      menu->findChild<QAction*>(QStringLiteral("terminalContextPaste"));
  QVERIFY(paste != nullptr);
  QVERIFY(paste->isEnabled());
  paste->trigger();
  QTRY_VERIFY_WITH_TIMEOUT(terminal.PlainTextForTest().contains(
                               QStringLiteral("__hermes_paste_ok__")),
                           5000);
  terminal.Stop();
  QVERIFY(!terminal.IsRunning());
}

void TerminalWidgetTest::runsShellCommandThroughPty() {
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  TerminalWidget terminal;
  terminal.resize(720, 420);
  terminal.SetShellForTest(QStringLiteral("/bin/sh"));
  terminal.SetWorkingDirectory(directory.path());
  QVERIFY(terminal.Start());
  QTRY_VERIFY_WITH_TIMEOUT(terminal.IsRunning(), 3000);
  terminal.SendText(QStringLiteral("printf '__hermes_pty_ok__\\n'\r"));
  QTRY_VERIFY_WITH_TIMEOUT(
      terminal.PlainTextForTest().contains(QStringLiteral("__hermes_pty_ok__")),
      5000);
  terminal.Stop();
  QVERIFY(!terminal.IsRunning());
}

}  // namespace
}  // namespace hermes::ui

QTEST_MAIN(hermes::ui::TerminalWidgetTest)

#include "terminal_widget_test.moc"
