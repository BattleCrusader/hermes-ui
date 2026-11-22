// Copyright 2026 The Hermes Qt Spike Authors.

#include <QJsonArray>
#include <QJsonObject>
#include <QScrollBar>
#include <QtTest>

#include "src/kanban_model.h"
#include "src/kanban_view.h"
#include "src/pages_view.h"

using namespace hermes::kanban;
using hermes::ui::KanbanView;
using hermes::ui::PagesView;
using hermes::ui::Theme;

namespace {

QJsonObject Task(const QString& id, const QString& status,
                 const QString& title = QString()) {
  return QJsonObject{{"id", id},
                     {"title", title.isEmpty() ? id : title},
                     {"status", status},
                     {"assignee", "ceo"},
                     {"priority", 3},
                     {"comment_count", 2},
                     {"link_counts", QJsonObject{{"parents", 1}, {"children", 4}}},
                     {"progress", QJsonObject{{"done", 3}, {"total", 20}}}};
}

}  // namespace

class KanbanModelTest : public QObject {
  Q_OBJECT

 private slots:
  void parseObjectColumnsReadsLaneTasks() {
    const QJsonObject payload{
        {"board", "delivery"},
        {"latest_event_id", 42},
        {"tenants", QJsonArray{"default"}},
        {"assignees", QJsonArray{"ceo", "trader"}},
        {"columns",
         QJsonObject{
             {"triage", QJsonArray{Task("t1", "triage")}},
             {"ready", QJsonArray{Task("r1", "ready"), Task("r2", "ready")}},
             {"done", QJsonArray{Task("d1", "done")}},
         }},
    };
    const KanbanBoard board = ParseBoard(payload);
    QCOMPARE(board.board, QStringLiteral("delivery"));
    QCOMPARE(board.latest_event_id, 42LL);
    QCOMPARE(board.tasks.size(), 4);
    QCOMPARE(board.assignees, QStringList({"ceo", "trader"}));
    QVERIFY(board.columns.contains("triage"));
    QVERIFY(board.columns.contains("ready"));
    QCOMPARE(board.tasks.front().progress_done, 3);
    QCOMPARE(board.tasks.front().progress_total, 20);
    QCOMPARE(board.tasks.front().link_count, 5);
    QCOMPARE(board.tasks.front().comment_count, 2);
  }

  void parseArrayColumnsReadsLaneTasks() {
    const QJsonObject payload{
        {"columns",
         QJsonArray{
             QJsonObject{{"name", "todo"}, {"tasks", QJsonArray{Task("a", "todo")}}},
             QJsonObject{{"key", "running"}, {"tasks", QJsonArray{Task("b", "running")}}},
         }},
        {"tasks", QJsonArray{Task("a", "todo")}},
    };
    const KanbanBoard board = ParseBoard(payload);
    QCOMPARE(board.tasks.size(), 2);
    QCOMPARE(board.columns, QStringList({"todo", "running"}));
  }

  void parseLiveBoardShapeFromPlugin() {
    // Captured from ``GET /api/plugins/kanban/board?include_archived=1``:
    // lanes are an array, every task lives inside its lane, and there is no
    // top-level ``board``/``tasks`` object.
    const QJsonObject payload{
        {"latest_event_id", 648},
        {"now", 1785000000},
        {"assignees", QJsonArray{"ceo", "trader"}},
        {"tenants", QJsonArray{"default"}},
        {"columns",
         QJsonArray{
             QJsonObject{{"name", "triage"}, {"tasks", QJsonArray{}}},
             QJsonObject{{"name", "ready"},
                         {"tasks", QJsonArray{Task("r1", "ready")}}},
             QJsonObject{{"name", "done"},
                         {"tasks", QJsonArray{Task("d1", "done")}}},
             QJsonObject{{"name", "archived"},
                         {"tasks", QJsonArray{Task("a1", "archived")}}},
         }},
    };
    const KanbanBoard board = ParseBoard(payload);
    QCOMPARE(board.tasks.size(), 3);
    QCOMPARE(board.latest_event_id, 648LL);
    QCOMPARE(board.columns,
             QStringList({QStringLiteral("triage"), QStringLiteral("ready"),
                          QStringLiteral("done"), QStringLiteral("archived")}));
    QVERIFY(board.tasks.back().archived);
  }

  void parseFlatTasksWithoutColumns() {
    const QJsonObject payload{{"tasks", QJsonArray{Task("x", "done")}}};
    const KanbanBoard board = ParseBoard(payload);
    QCOMPARE(board.tasks.size(), 1);
    QVERIFY(board.columns.contains("done"));
  }

  void lockedColumnsCannotBeDropTargets() {
    QVERIFY(LockedTarget(QStringLiteral("running")));
    QVERIFY(LockedTarget(QStringLiteral("review")));
    QVERIFY(LockedTarget(QStringLiteral("scheduled")));
    QVERIFY(!LockedTarget(QStringLiteral("ready")));
    QVERIFY(!CanMove(QStringLiteral("ready"), QStringLiteral("running")));
    QVERIFY(!CanMove(QStringLiteral("ready"), QStringLiteral("ready")));
    QVERIFY(CanMove(QStringLiteral("ready"), QStringLiteral("done")));
    QVERIFY(!CanMove(QStringLiteral("running"), QStringLiteral("review")));
    QVERIFY(CanMove(QStringLiteral("running"), QStringLiteral("done")));
    // The manual workflow is stricter than "any unlocked lane".
    QVERIFY(!CanMove(QStringLiteral("todo"), QStringLiteral("done")));
    QVERIFY(!CanMove(QStringLiteral("triage"), QStringLiteral("blocked")));
    QVERIFY(!CanMove(QStringLiteral("triage"), QStringLiteral("scheduled")));
    QVERIFY(CanMove(QStringLiteral("blocked"), QStringLiteral("ready")));
    QVERIFY(!CanMove(QStringLiteral("todo"), QStringLiteral("blocked")));
    QVERIFY(CanMove(QStringLiteral("done"), QStringLiteral("ready")));
    QCOMPARE(OrderedColumns({}),
             QStringList({QStringLiteral("triage"), QStringLiteral("todo"),
                          QStringLiteral("scheduled"), QStringLiteral("ready"),
                          QStringLiteral("running"), QStringLiteral("blocked"),
                          QStringLiteral("review"), QStringLiteral("done")}));
  }

  void createPayloadKeepsRequestedLaneAndParents() {
    KanbanNewTask task;
    task.title = QStringLiteral("Ship the queue");
    task.body = QStringLiteral("Details");
    task.status = QStringLiteral("todo");
    task.assignee = QStringLiteral("ceo");
    task.priority = 4;
    task.skills = QStringLiteral("python, qt");
    task.parent_id = QStringLiteral("parent-1");
    task.model_override = QStringLiteral("gpt-5");
    task.goal_mode = true;
    const QJsonObject payload = BuildCreatePayload(task);
    QCOMPARE(payload.value("title").toString(), QStringLiteral("Ship the queue"));
    QCOMPARE(payload.value("assignee").toString(), QStringLiteral("ceo"));
    QCOMPARE(payload.value("priority").toInt(), 4);
    QCOMPARE(payload.value("parents").toArray().size(), 1);
    QCOMPARE(payload.value("skills").toArray().size(), 2);
    QCOMPARE(payload.value("model_override").toString(), QStringLiteral("gpt-5"));
    QVERIFY(payload.value("goal_mode").toBool());
    QCOMPARE(payload.value("triage").toBool(), false);
  }

  void viewPaintsCardsFromParsedBoard() {
    KanbanView view(hermes::ui::Theme{});
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns({QStringLiteral("triage"), QStringLiteral("ready")});
    board.tasks.push_back(ParseTask(Task("one", "triage")));
    board.tasks.push_back(ParseTask(Task("two", "ready")));
    board.tasks.push_back(ParseTask(Task("three", "ready")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");
    view.SetBoardForTest(board, meta);
    QCoreApplication::processEvents();
    QCOMPARE(view.VisibleTaskCountForTest(), 3);
    QVERIFY(view.TaskRectsForTest().size() >= 3);
    view.SelectForTest(QStringLiteral("two"));
    QVERIFY(view.TaskRectsForTest().size() >= 3);
  }

  void pagesViewThemeSwitchRepaintsKanbanBoard() {
    PagesView pages(Theme::Light());
    pages.resize(900, 600);
    pages.show();
    pages.OpenPage(QStringLiteral("kanban"));
    KanbanView* view = pages.Kanban();
    QVERIFY(view != nullptr);
    QCOMPARE(view->CanvasBackgroundForTest(), Theme::Light().background);

    // Regression: PagesView owns the Kanban widget but used to leave its
    // palette untouched when the app switched between light and dark themes.
    pages.SetTheme(Theme::Dark());
    QCOMPARE(view->CanvasBackgroundForTest(), Theme::Dark().background);
  }

  void viewKeepsHorizontalRangeAtRightEdge() {
    KanbanView view(hermes::ui::Theme{});
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns(
        {QStringLiteral("triage"), QStringLiteral("todo"),
         QStringLiteral("scheduled"), QStringLiteral("ready"),
         QStringLiteral("running"), QStringLiteral("blocked"),
         QStringLiteral("review"), QStringLiteral("done")});
    board.tasks.push_back(ParseTask(Task("done-1", "done")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");
    view.SetBoardForTest(board, meta);

    auto* scroll = view.findChild<QScrollBar*>(QStringLiteral("kanbanHScroll"));
    QVERIFY(scroll != nullptr);
    const int maximum = scroll->maximum();
    QVERIFY(maximum > 0);

    scroll->setValue(maximum);
    QCOMPARE(scroll->value(), maximum);
    // A refresh while scrolled right must not shrink the range back under the
    // thumb. This used to make the done lane unreachable.
    view.SetBoardForTest(board, meta);
    QCOMPARE(scroll->maximum(), maximum);
    QCOMPARE(scroll->value(), maximum);
  }

  void boardRefreshDoesNotCancelDrag() {
    KanbanView view(Theme::Light());
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns(
        {QStringLiteral("triage"), QStringLiteral("ready")});
    board.tasks.push_back(ParseTask(Task("one", "triage")));
    board.tasks.push_back(ParseTask(Task("two", "ready")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");
    view.SetBoardForTest(board, meta);
    view.resize(1200, 640);
    view.show();
    QCoreApplication::processEvents();

    QWidget* canvas = view.CanvasForTest();
    QVERIFY(canvas != nullptr);
    const QPoint from = view.TaskCenterForTest(QStringLiteral("one")).toPoint();
    const QPoint to = view.ColumnCenterForTest(QStringLiteral("ready")).toPoint();
    QVERIFY(!from.isNull());
    QVERIFY(!to.isNull());

    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(canvas, to, 20);
    QVERIFY(view.DragInProgressForTest());
    QCOMPARE(view.DragTaskForTest(), QStringLiteral("one"));

    // Regression: the four-second board poll used to call SetBoard and clear
    // the drag state, so release never moved the card.
    view.SetBoardForTest(board, meta);
    QCoreApplication::processEvents();
    QVERIFY(view.DragInProgressForTest());
    QCOMPARE(view.DragTaskForTest(), QStringLiteral("one"));

    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, to);
    QVERIFY(!view.DragInProgressForTest());
  }

  void moveIntoReadyNudgesTheDispatcher() {
    KanbanView view(Theme::Light());
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns(
        {QStringLiteral("triage"), QStringLiteral("ready")});
    board.tasks.push_back(ParseTask(Task("one", "triage")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");
    view.SetBoardForTest(board, meta);

    QStringList calls;
    view.on_request = [&calls](const QString& method, const QString& path,
                               const QJsonObject&, KanbanView::JsonCallback done) {
      calls.push_back(method + QStringLiteral(" ") + path);
      QJsonObject data;
      if (path.startsWith(QStringLiteral("/dispatch"))) {
        data = QJsonObject{{"spawned", QJsonArray{QJsonArray{"one", "ceo", ""}}}};
      }
      if (done) {
        done(data, QString());
      }
    };

    view.MoveForTest({QStringLiteral("one")}, QStringLiteral("ready"));
    QVERIFY(calls.contains(QStringLiteral("PATCH /tasks/one?board=test")));
    // ``kanban.dispatch_in_gateway`` can be false; without this POST the ready
    // card sits idle until someone starts a dispatcher.
    QVERIFY(calls.contains(
        QStringLiteral("POST /dispatch?max=8&board=test")));
  }

  void dialogsThemeFormControlsAndFollowThemeChanges() {
    KanbanView view(Theme::Light());
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns(
        {QStringLiteral("triage"), QStringLiteral("ready")});
    board.tasks.push_back(ParseTask(Task("one", "triage")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");
    view.SetBoardForTest(board, meta);

    view.OpenTaskForTest(QStringLiteral("one"));
    const QString light_task = view.TaskDialogStyleForTest();
    QVERIFY(light_task.contains(QStringLiteral("QComboBox")));
    QVERIFY(light_task.contains(QStringLiteral("QSpinBox")));
    QVERIFY(light_task.contains(QStringLiteral("QScrollArea")));
    QVERIFY(light_task.contains(Theme::Light().background.name()));

    view.OpenOrchestrationForTest();
    const QString light_orchestration = view.OrchestrationDialogStyleForTest();
    QVERIFY(light_orchestration.contains(QStringLiteral("QCheckBox")));
    QVERIFY(light_orchestration.contains(Theme::Light().background.name()));

    view.SetTheme(Theme::Dark());
    QVERIFY(view.TaskDialogStyleForTest().contains(
        Theme::Dark().background.name()));
    QVERIFY(view.OrchestrationDialogStyleForTest().contains(
        Theme::Dark().background.name()));
  }

  void viewRefreshKeepsSelectionOnlyForLiveCards() {
    KanbanView view(hermes::ui::Theme{});
    KanbanBoard board;
    board.board = QStringLiteral("test");
    board.columns = OrderedColumns(
        {QStringLiteral("triage"), QStringLiteral("ready")});
    board.tasks.push_back(ParseTask(Task("one", "triage")));
    board.tasks.push_back(ParseTask(Task("two", "ready")));
    KanbanBoardMeta meta;
    meta.slug = QStringLiteral("test");
    meta.name = QStringLiteral("Test");

    view.SetBoardForTest(board, meta);
    view.SelectForTest(QStringLiteral("two"));
    QCOMPARE(view.SelectionForTest(), QStringList({QStringLiteral("two")}));

    // A polled refresh keeps the card selected.
    view.SetBoardForTest(board, meta);
    QCOMPARE(view.SelectionForTest(), QStringList({QStringLiteral("two")}));

    // If the card disappeared, the stale selection is dropped.
    KanbanBoard trimmed = board;
    trimmed.tasks.erase(trimmed.tasks.begin() + 1);
    view.SetBoardForTest(trimmed, meta);
    QVERIFY(view.SelectionForTest().isEmpty());
  }
};

// The Kanban test target links PagesView to exercise theme propagation. The
// data pages themselves are out of scope here, so satisfy the one factory
// symbol PagesView calls.
namespace hermes::ui {
QWidget* BuildDataPage(const QString&, const PageContext&, QWidget*) {
  return nullptr;
}
}  // namespace hermes::ui

QTEST_MAIN(KanbanModelTest)
#include "kanban_model_test.moc"
