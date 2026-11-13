// Copyright 2026 The Hermes Qt Spike Authors.
//
// Sidebar contract: the workspace nav rows, the profile rail's add button and
// the session list are custom-painted, so their hit geometry is part of the
// public behaviour rather than a Qt child-widget accident.

#include "src/sidebar.h"

#include <QCompleter>
#include <QCoreApplication>
#include <QImage>
#include <QLineEdit>
#include <QScrollBar>
#include <QtTest>
#include <vector>

#include "src/projects.h"
#include "src/theme.h"

namespace {

hermes::ui::ProfileSnapshot SnapshotWithSessions() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");
  for (int i = 0; i < 3; ++i) {
    hermes::ui::SessionSummary session;
    session.id = QStringLiteral("s%1").arg(i);
    session.title = QStringLiteral("Session %1").arg(i);
    session.last_active_secs = 1762750000 + i;
    session.pinned = i == 2;
    snapshot.sessions.push_back(session);
  }
  snapshot.today_count = 2;
  return snapshot;
}

}  // namespace

class SidebarTest : public QObject {
  Q_OBJECT

 private slots:
  void navRowsRouteToWorkspacePages();
  void addProfileButtonInvokesCallback();
  void activityPulseTracksBackgroundSession();
  void activityPulseTracksBackgroundProfile();
  void activityPulseTracksBackgroundProject();
  void sessionRowSelectsSession();
  void projectRowsScopeSessionsAndAdd();
  void scopedProjectKeepsNewLocalChatVisible();
  void scopedProjectHidesChatFromAnotherProject();
  void emptyProjectStateStillOffersCreate();
  void projectScrollSurvivesSnapshotsAndHomeLeads();
  void sessionDensityChangesRowHeightAndContext();
  void collapsedSidebarKeepsNewChatAndNavigation();
};

void SidebarTest::projectScrollSurvivesSnapshotsAndHomeLeads() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  sidebar.SetSnapshot(snapshot);

  std::vector<hermes::ui::ProjectNode> projects;
  hermes::ui::ProjectNode home;
  home.id = QStringLiteral("__no_project__");
  home.label = QStringLiteral("Home");
  home.is_no_project = true;
  home.session_ids = {QStringLiteral("s0")};
  home.session_count = 1;
  projects.push_back(home);
  for (int i = 0; i < 14; ++i) {
    hermes::ui::ProjectNode node;
    node.id = QStringLiteral("p%1").arg(i);
    node.label = QStringLiteral("Project %1").arg(i);
    node.session_count = 0;
    projects.push_back(node);
  }
  sidebar.SetProjects(projects, hermes::ui::AllProjectsId(), QString(), true);

  // Home owns sessions no project claimed and stays above the project
  // switcher, even when the tree is long enough to scroll.
  QStringList project_order;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("sidebar:project:"))) {
      project_order.append(target.name);
    }
  }
  QVERIFY(project_order.indexOf(QStringLiteral("sidebar:project:all")) == 0);
  QCOMPARE(project_order.value(1),
           QStringLiteral("sidebar:project:__no_project__"));

  auto* scrollbar =
      sidebar.findChild<QScrollBar*>(QStringLiteral("sessionScrollBar"));
  QVERIFY(scrollbar != nullptr);
  QVERIFY(scrollbar->isVisible());
  QVERIFY(scrollbar->maximum() > 0);
  scrollbar->setValue(scrollbar->maximum() / 2);
  QVERIFY(scrollbar->value() > 0);
  const int kept = scrollbar->value();

  // Snapshot refreshes and identical project-tree refreshes must not jump.
  sidebar.SetSnapshot(snapshot);
  QCoreApplication::processEvents();
  QCOMPARE(scrollbar->value(), kept);
  sidebar.SetProjects(projects, hermes::ui::AllProjectsId(), QString(), true);
  QCoreApplication::processEvents();
  QCOMPARE(scrollbar->value(), kept);

  // Scoping to Home exposes its unassigned sessions as nested rows.
  sidebar.SetProjects(projects, home.id, QString(), true);
  QCoreApplication::processEvents();
  QCOMPARE(scrollbar->value(), 0);
  bool home_session_visible = false;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:session:s0")) {
      home_session_visible = true;
    }
  }
  QVERIFY(home_session_visible);
}

void SidebarTest::sessionDensityChangesRowHeightAndContext() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  snapshot.sessions[0].preview =
      QStringLiteral("A detailed second line with session context.");
  sidebar.SetSnapshot(snapshot);

  auto row_height = [&sidebar](const QString& id) {
    for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
      if (target.name == QStringLiteral("sidebar:session:") + id) {
        return target.rect.height();
      }
    }
    return -1.0;
  };

  QCOMPARE(sidebar.session_density(), hermes::ui::SessionListDensity::kCompact);
  QCOMPARE(qRound(row_height(QStringLiteral("s0"))), 30);

  sidebar.SetSessionDensity(hermes::ui::SessionListDensity::kComfortable);
  QCOMPARE(qRound(row_height(QStringLiteral("s0"))), 36);

  sidebar.SetSessionDensity(hermes::ui::SessionListDensity::kDetailed);
  QCOMPARE(qRound(row_height(QStringLiteral("s0"))), 52);
  // Detailed rows keep the title and preview in the same clickable row; the
  // preview is painted, not a child widget that could steal the click.
  bool child_label = false;
  for (QObject* child : sidebar.findChildren<QWidget*>()) {
    if (child->inherits("QLabel")) {
      child_label = true;
    }
  }
  QVERIFY(!child_label);
}

void SidebarTest::collapsedSidebarKeepsNewChatAndNavigation() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();
  sidebar.SetSnapshot(SnapshotWithSessions());

  int new_chats = 0;
  QStringList navigated;
  int collapse_events = 0;
  sidebar.on_new_chat = [&new_chats]() { ++new_chats; };
  sidebar.on_nav = [&navigated](const QString& id) { navigated.append(id); };
  sidebar.on_collapsed_changed = [&collapse_events](bool) {
    ++collapse_events;
  };

  QRectF collapse;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:collapse")) {
      collapse = target.rect;
    }
  }
  QVERIFY(!collapse.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    collapse.center().toPoint());
  QVERIFY(sidebar.IsCollapsed());
  QCOMPARE(collapse_events, 1);
  QTRY_COMPARE(sidebar.width(), 46);
  QVERIFY(sidebar.findChild<QLineEdit*>(QStringLiteral("sessionSearch"))
              ->isHidden());

  // The compact rail is not a dead icon strip: new chat and the workspace
  // routes stay hit-testable at the collapsed geometry.
  QRectF new_chat;
  QRectF artifacts;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:new-chat")) {
      new_chat = target.rect;
    } else if (target.name == QStringLiteral("sidebar:nav:artifacts")) {
      artifacts = target.rect;
    }
  }
  QVERIFY(!new_chat.isNull());
  QVERIFY(!artifacts.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    new_chat.center().toPoint());
  QCOMPARE(new_chats, 1);
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    artifacts.center().toPoint());
  QCOMPARE(navigated, QStringList{QStringLiteral("artifacts")});

  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:collapse")) {
      collapse = target.rect;
    }
  }
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    collapse.center().toPoint());
  QVERIFY(!sidebar.IsCollapsed());
  QCOMPARE(collapse_events, 2);
  QTRY_VERIFY(sidebar.width() >= 180);
  auto* search = sidebar.findChild<QLineEdit*>(QStringLiteral("sessionSearch"));
  QVERIFY(search->isVisible());

  // The filter field completes current session titles and project labels as
  // the reader types; it is not a static placeholder list.
  QTest::keyClicks(search, QStringLiteral("Session"));
  QTRY_VERIFY(search->completer() != nullptr);
  QTRY_COMPARE(search->completer()->model()->rowCount(), 3);
  QCOMPARE(search->completer()->model()->index(0, 0).data().toString().left(7),
           QStringLiteral("Session"));
}

void SidebarTest::navRowsRouteToWorkspacePages() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();
  sidebar.SetSnapshot(SnapshotWithSessions());

  QStringList navigated;
  sidebar.on_nav = [&navigated](const QString& id) { navigated.append(id); };

  // Nav rows are 26px tall starting at y=46 in the order the desktop lists
  // them: Capabilities, Messaging, Artifacts, Webhooks, Scheduled tasks, Bots.
  const QStringList expected{
      QStringLiteral("capabilities"), QStringLiteral("messaging"),
      QStringLiteral("artifacts"),    QStringLiteral("webhooks"),
      QStringLiteral("cron"),         QStringLiteral("profiles")};
  for (int i = 0; i < expected.size(); ++i) {
    QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                      QPoint(60, 46 + i * 28 + 13));
  }
  QCOMPARE(navigated, expected);
}

void SidebarTest::addProfileButtonInvokesCallback() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();
  std::vector<hermes::ui::ProfileChip> chips;
  for (const char* label : {"default", "work"}) {
    hermes::ui::ProfileChip chip;
    chip.label = QLatin1String(label);
    chips.push_back(chip);
  }
  sidebar.SetProfileChips(chips, 0);

  int added = 0;
  sidebar.on_add_profile = [&added]() { ++added; };
  // The add cell sits after the chips on the rail, bottom-left.
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    QPoint(8 + 2 * 24 + 10, 720 - 30 + 10));
  QCOMPARE(added, 1);
}

void SidebarTest::activityPulseTracksBackgroundSession() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  snapshot.selected_session_id = QStringLiteral("s1");
  snapshot.sessions[0].title.clear();
  sidebar.SetSnapshot(snapshot);
  QCoreApplication::processEvents();

  QRectF row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:session:s0")) {
      row = target.rect;
    }
  }
  QVERIFY(!row.isNull());

  const QImage before = sidebar.grab().toImage();
  sidebar.SetRunning(true, QStringLiteral("s0"));
  sidebar.repaint();
  QCoreApplication::processEvents();
  const QImage after = sidebar.grab().toImage();
  const qreal dpr = after.devicePixelRatio();
  const QPoint sample(qRound((row.left() + 8) * dpr),
                      qRound(row.center().y() * dpr));
  // The turn belongs to s0 while s1 is selected: the dot must follow the
  // working session, not the selection.
  QVERIFY(before.pixelColor(sample) != after.pixelColor(sample));
}

void SidebarTest::activityPulseTracksBackgroundProfile() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();

  std::vector<hermes::ui::ProfileChip> chips(2);
  chips[0].label = QStringLiteral("default");
  chips[0].color = theme.accent;
  chips[1].label = QStringLiteral("work");
  chips[1].color = theme.accent;
  sidebar.SetProfileChips(chips, 0);
  QCoreApplication::processEvents();

  const QImage before = sidebar.grab().toImage();
  chips[1].running = true;
  sidebar.SetProfileChips(chips, 0);
  sidebar.repaint();
  QCoreApplication::processEvents();
  const QImage after = sidebar.grab().toImage();
  const qreal dpr = after.devicePixelRatio();
  // Second chip on the rail: 8px left padding + one 20px chip + 4px gap.
  const QPoint sample(qRound((8 + 20 + 4 + 19) * dpr),
                      qRound((sidebar.height() - 30 + 19) * dpr));
  QVERIFY(before.pixelColor(sample) != after.pixelColor(sample));
}

void SidebarTest::activityPulseTracksBackgroundProject() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 500);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  snapshot.selected_session_id = QStringLiteral("s1");
  sidebar.SetSnapshot(snapshot);

  hermes::ui::ProjectNode project;
  project.id = QStringLiteral("p1");
  project.label = QStringLiteral("Project");
  project.session_count = 1;
  project.session_ids = {QStringLiteral("s0")};
  sidebar.SetProjects({project}, hermes::ui::AllProjectsId(), QString(), true);
  QCoreApplication::processEvents();

  QRectF row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:project:p1")) {
      row = target.rect;
    }
  }
  QVERIFY(!row.isNull());

  const QImage before = sidebar.grab().toImage();
  sidebar.SetRunning(true, QStringLiteral("s0"));
  sidebar.repaint();
  QCoreApplication::processEvents();
  const QImage after = sidebar.grab().toImage();
  const qreal dpr = after.devicePixelRatio();
  const QPoint sample(qRound((row.left() + 36) * dpr),
                      qRound(row.center().y() * dpr));
  // The project bullet aggregates the busy session even when the project
  // itself is not the active scope.
  QVERIFY(before.pixelColor(sample) != after.pixelColor(sample));
}

void SidebarTest::sessionRowSelectsSession() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();
  sidebar.SetSnapshot(SnapshotWithSessions());

  QString selected;
  sidebar.on_session_selected = [&selected](const QString& id) {
    selected = id;
  };

  // Pinned sessions sort above the TODAY section, so the first row is "s2".
  QRectF row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:session:s2")) {
      row = target.rect;
    }
  }
  QVERIFY(!row.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    row.center().toPoint());
  QCOMPARE(selected, QStringLiteral("s2"));
}

void SidebarTest::projectRowsScopeSessionsAndAdd() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();
  sidebar.SetSnapshot(SnapshotWithSessions());

  hermes::ui::ProjectNode first;
  first.id = QStringLiteral("p1");
  first.label = QStringLiteral("Project One");
  first.session_ids = {QStringLiteral("s0"), QStringLiteral("s1")};
  first.session_count = 2;
  hermes::ui::ProjectNode second;
  second.id = QStringLiteral("p2");
  second.label = QStringLiteral("Project Two");
  second.session_ids = {QStringLiteral("s2")};
  second.session_count = 1;
  sidebar.SetProjects({first, second}, hermes::ui::AllProjectsId(),
                      QStringLiteral("p2"));

  QString selected;
  sidebar.on_project_selected = [&selected](const QString& id) {
    selected = id;
  };
  int adds = 0;
  sidebar.on_project_add = [&adds]() { ++adds; };

  QRectF project_row;
  QRectF add_row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:project:p1")) {
      project_row = target.rect;
    } else if (target.name == QStringLiteral("sidebar:project-add")) {
      add_row = target.rect;
    }
  }
  QVERIFY(!project_row.isNull());
  QVERIFY(!add_row.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    project_row.center().toPoint());
  QCOMPARE(selected, QStringLiteral("p1"));

  QStringList scoped_sessions;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("sidebar:session:"))) {
      scoped_sessions.push_back(
          target.name.mid(QStringLiteral("sidebar:session:").size()));
    }
  }
  QCOMPARE(scoped_sessions,
           (QStringList{QStringLiteral("s0"), QStringLiteral("s1")}));

  // The chevron toggles the same activation path as the row label, so the
  // project's sessions stay beneath the row instead of in the flat recency
  // list. Collapsing returns to All projects, which restores every session.
  QRectF second_toggle;
  QRectF all_row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:project-toggle:p2")) {
      second_toggle = target.rect;
    } else if (target.name == QStringLiteral("sidebar:project:all")) {
      all_row = target.rect;
    }
  }
  QVERIFY(!second_toggle.isNull());
  QVERIFY(!all_row.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    second_toggle.center().toPoint());
  QCOMPARE(selected, QStringLiteral("p2"));
  QStringList second_scope;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("sidebar:session:"))) {
      second_scope.push_back(
          target.name.mid(QStringLiteral("sidebar:session:").size()));
    }
  }
  QCOMPARE(second_scope, (QStringList{QStringLiteral("s2")}));

  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    all_row.center().toPoint());
  QCOMPARE(selected, QString());
  QStringList all_sessions;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("sidebar:session:"))) {
      all_sessions.push_back(
          target.name.mid(QStringLiteral("sidebar:session:").size()));
    }
  }
  all_sessions.sort();
  QCOMPARE(all_sessions,
           (QStringList{QStringLiteral("s0"), QStringLiteral("s1"),
                        QStringLiteral("s2")}));

  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    add_row.center().toPoint());
  QCOMPARE(adds, 1);
}

void SidebarTest::scopedProjectKeepsNewLocalChatVisible() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  hermes::ui::SessionSummary fresh;
  fresh.id = QStringLiteral("s3");
  fresh.title = QStringLiteral("New chat");
  fresh.source = QStringLiteral("local");
  fresh.project_id = QStringLiteral("p1");
  fresh.last_active_secs = 1762750003;
  snapshot.sessions.push_back(fresh);
  snapshot.selected_session_id = fresh.id;
  sidebar.SetSnapshot(snapshot);

  hermes::ui::ProjectNode project;
  project.id = QStringLiteral("p1");
  project.label = QStringLiteral("Project One");
  project.session_ids = {QStringLiteral("s0"), QStringLiteral("s1")};
  project.session_count = 2;
  sidebar.SetProjects({project}, project.id, project.id);

  QStringList scoped_sessions;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name.startsWith(QStringLiteral("sidebar:session:"))) {
      scoped_sessions.push_back(
          target.name.mid(QStringLiteral("sidebar:session:").size()));
    }
  }
  QCOMPARE(scoped_sessions,
           (QStringList{QStringLiteral("s0"), QStringLiteral("s1"),
                        QStringLiteral("s3")}));
}

void SidebarTest::scopedProjectHidesChatFromAnotherProject() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();

  hermes::ui::ProfileSnapshot snapshot = SnapshotWithSessions();
  hermes::ui::SessionSummary other;
  other.id = QStringLiteral("s3");
  other.title = QStringLiteral("Other project chat");
  other.source = QStringLiteral("local");
  other.project_id = QStringLiteral("p2");
  other.last_active_secs = 1762750003;
  snapshot.sessions.push_back(other);
  snapshot.selected_session_id = other.id;
  sidebar.SetSnapshot(snapshot);

  hermes::ui::ProjectNode project;
  project.id = QStringLiteral("p1");
  project.label = QStringLiteral("Project One");
  project.session_ids = {QStringLiteral("s0"), QStringLiteral("s1")};
  project.session_count = 2;
  sidebar.SetProjects({project}, project.id, project.id);

  const auto scoped_sessions = [&sidebar]() {
    QStringList ids;
    for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
      if (target.name.startsWith(QStringLiteral("sidebar:session:"))) {
        ids.push_back(
            target.name.mid(QStringLiteral("sidebar:session:").size()));
      }
    }
    return ids;
  };
  QCOMPARE(scoped_sessions(),
           (QStringList{QStringLiteral("s0"), QStringLiteral("s1")}));

  // Empty projects must not inherit the selected local chat either; the main
  // window starts a blank session tagged with the project instead.
  project.session_ids.clear();
  project.session_count = 0;
  snapshot.sessions.back().project_id = QStringLiteral("p1");
  sidebar.SetSnapshot(snapshot);
  sidebar.SetProjects({project}, project.id, project.id);
  QCOMPARE(scoped_sessions(), (QStringList{QStringLiteral("s3")}));

  // The tag only covers a session the backend tree has not placed yet. Once the
  // tree assigns it elsewhere, project membership wins over the stale tag.
  hermes::ui::ProjectNode other_project;
  other_project.id = QStringLiteral("p2");
  other_project.label = QStringLiteral("Project Two");
  other_project.session_ids = {other.id};
  other_project.session_count = 1;
  sidebar.SetProjects({project, other_project}, project.id, project.id);
  QVERIFY(scoped_sessions().isEmpty());

  // Home is the backend's catch-all for unclaimed sessions. It must not
  // override the project tag before the real project picks the chat up.
  hermes::ui::ProjectNode home;
  home.id = QStringLiteral("__no_project__");
  home.label = QStringLiteral("Home");
  home.is_no_project = true;
  home.session_ids = {other.id};
  home.session_count = 1;
  sidebar.SetProjects({home, project}, project.id, project.id);
  QCOMPARE(scoped_sessions(), (QStringList{QStringLiteral("s3")}));
}

void SidebarTest::emptyProjectStateStillOffersCreate() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Sidebar sidebar(theme);
  sidebar.resize(theme.sidebar_width, 720);
  sidebar.show();
  sidebar.SetSnapshot(SnapshotWithSessions());

  int adds = 0;
  sidebar.on_project_add = [&adds]() { ++adds; };

  // Without a successful project fetch the section stays hidden, so a gateway
  // without the projects capability does not grow an empty switcher.
  sidebar.SetProjects({}, hermes::ui::AllProjectsId(), QString());
  bool seen_add = false;
  bool seen_all = false;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    seen_add = seen_add || target.name == QStringLiteral("sidebar:project-add");
    seen_all = seen_all || target.name == QStringLiteral("sidebar:project:all");
  }
  QVERIFY(!seen_add);
  QVERIFY(!seen_all);

  // A successful empty list still needs the desktop's "add project"
  // affordance, not just the command palette entry.
  sidebar.SetProjects({}, hermes::ui::AllProjectsId(), QString(), true);
  QRectF add_row;
  for (const hermes::ui::ClickTarget& target : sidebar.ClickTargets()) {
    if (target.name == QStringLiteral("sidebar:project-add")) {
      add_row = target.rect;
    } else if (target.name == QStringLiteral("sidebar:project:all")) {
      seen_all = true;
    }
  }
  QVERIFY(seen_all);
  QVERIFY(!add_row.isNull());
  QTest::mouseClick(&sidebar, Qt::LeftButton, Qt::NoModifier,
                    add_row.center().toPoint());
  QCOMPARE(adds, 1);
  QVERIFY(sidebar.ClickTargets().size() > 0);
}

QTEST_MAIN(SidebarTest)

#include "sidebar_test.moc"
