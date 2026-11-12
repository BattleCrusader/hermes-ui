// Copyright 2026 The Hermes Qt Spike Authors.

#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDialog>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QPixmap>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <cstdio>

#include "src/gateway_endpoint.h"
#include "src/gateway_http.h"
#include "src/gateway_socket.h"
#include "src/main_window.h"
#include "src/theme.h"

namespace {

void LoadBundledFonts() {
  const QStringList fonts = {
      QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"),
      QStringLiteral(":/fonts/JetBrainsMono-Bold.ttf"),
      QStringLiteral(":/fonts/codicon.ttf"),
  };
  for (const QString& path : fonts) {
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0) {
      qWarning("failed to load font %s", qUtf8Printable(path));
    }
  }
}

// Exercises discovery, the control-socket ticket, and both HTTP legs without
// starting the UI. This is the smoke test used while porting the gateway.
int RunGatewayProbe() {
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    qCritical("endpoint: %s", qUtf8Printable(error));
    return 1;
  }
  qInfo("endpoint origin=%s instance=%.8s code=%s",
        qUtf8Printable(endpoint.api_origin),
        qUtf8Printable(endpoint.instance_id),
        qUtf8Printable(endpoint.code_sha));

  hermes::ui::GatewayHttp http(hermes::ui::HermesHome(),
                               QStringLiteral("default"), endpoint);
  QJsonDocument status;
  if (!http.GetJson(QStringLiteral("/api/status"), &status, &error, false)) {
    qCritical("status: %s", qUtf8Printable(error));
    return 1;
  }
  const QJsonObject status_object = status.object();
  const QJsonArray profiles =
      status_object.value(QStringLiteral("profiles")).toArray();
  qInfo(
      "version=%s profiles=%lld auth_required=%d",
      qUtf8Printable(status_object.value(QStringLiteral("version")).toString()),
      static_cast<qint64>(profiles.size()),
      status_object.value(QStringLiteral("auth_required")).toBool() ? 1 : 0);

  QJsonDocument sessions;
  if (!http.GetJson(QStringLiteral("/api/sessions?limit=3&order=recent"
                                   "&exclude_sources=cron"),
                    &sessions, &error)) {
    qCritical("sessions: %s", qUtf8Printable(error));
    return 1;
  }
  const QJsonArray rows =
      sessions.object().value(QStringLiteral("sessions")).toArray();
  qInfo("sessions=%lld", static_cast<qint64>(rows.size()));
  for (const QJsonValue& value : rows) {
    const QJsonObject row = value.toObject();
    qInfo("  session %.12s messages=%d",
          qUtf8Printable(row.value(QStringLiteral("id")).toString()),
          row.value(QStringLiteral("message_count")).toInt());
  }
  return 0;
}

int RunWebSocketProbe(QApplication* app) {
  hermes::ui::GatewayEndpoint endpoint;
  QString error;
  if (!hermes::ui::DiscoverEndpoint(&endpoint, &error)) {
    qCritical("endpoint: %s", qUtf8Printable(error));
    return 1;
  }
  auto* socket = new hermes::ui::GatewaySocket(
      hermes::ui::HermesHome(), QStringLiteral("default"), endpoint, app);
  QObject::connect(socket, &hermes::ui::GatewaySocket::Connected,
                   [](const QString& protocol) {
                     qInfo("ws connected protocol=%s",
                           qUtf8Printable(protocol));
                   });
  QObject::connect(socket, &hermes::ui::GatewaySocket::EventReceived,
                   [](const QString& type, const QString& session_id,
                      const QJsonObject& payload) {
                     qInfo("event type=%s session=%.8s payload_keys=%lld",
                           qUtf8Printable(type), qUtf8Printable(session_id),
                           static_cast<qint64>(payload.size()));
                   });
  QObject::connect(socket, &hermes::ui::GatewaySocket::SocketError,
                   [](const QString& message) {
                     qWarning("ws: %s", qUtf8Printable(message));
                   });
  socket->Connect();
  QTimer::singleShot(2500, app, &QCoreApplication::quit);
  return app->exec();
}

}  // namespace

int main(int argc, char* argv[]) {
  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("HermesUI"));
  QApplication::setApplicationDisplayName(QStringLiteral("Hermes"));

  LoadBundledFonts();
  QFont app_font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
  app_font.setPixelSize(13);
  QApplication::setFont(app_font);

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Native Qt spike for the Hermes desktop shell."));
  parser.addHelpOption();
  const QCommandLineOption screenshot_option(
      QStringLiteral("screenshot"),
      QStringLiteral("Grab the window to a PNG and exit."),
      QStringLiteral("path"));
  const QCommandLineOption profile_option(
      QStringLiteral("profile"), QStringLiteral("Start on this profile."),
      QStringLiteral("name"));
  const QCommandLineOption theme_option(
      QStringLiteral("theme"), QStringLiteral("light or dark."),
      QStringLiteral("mode"), QStringLiteral("light"));
  const QCommandLineOption bench_option(
      QStringLiteral("bench"),
      QStringLiteral("Run N synchronous profile switches and exit."),
      QStringLiteral("count"), QStringLiteral("0"));
  const QCommandLineOption duration_option(
      QStringLiteral("duration"), QStringLiteral("Quit after N milliseconds."),
      QStringLiteral("ms"), QStringLiteral("0"));
  const QCommandLineOption screenshot_delay_option(
      QStringLiteral("screenshot-delay"),
      QStringLiteral("Wait N milliseconds before the screenshot."),
      QStringLiteral("ms"), QStringLiteral("1800"));
  const QCommandLineOption send_option(
      QStringLiteral("send"),
      QStringLiteral("Submit this text on the active profile."),
      QStringLiteral("text"));
  const QCommandLineOption static_option(
      QStringLiteral("static"),
      QStringLiteral("Do not start the background profile streams."));
  const QCommandLineOption probe_option(
      QStringLiteral("probe"),
      QStringLiteral("Probe gateway discovery, tickets and REST, then exit."));
  const QCommandLineOption probe_ws_option(
      QStringLiteral("probe-ws"),
      QStringLiteral("Probe the gateway WebSocket handshake, then exit."));
  const QCommandLineOption new_session_option(
      QStringLiteral("new-session"),
      QStringLiteral("Create a new session on the active profile."));
  const QCommandLineOption auto_approve_option(
      QStringLiteral("auto-approve"),
      QStringLiteral("Answer the first approval with 'once' (testing)."));
  const QCommandLineOption auto_clarify_option(
      QStringLiteral("auto-clarify"),
      QStringLiteral("Answer clarify requests with this text (testing)."),
      QStringLiteral("answer"));
  const QCommandLineOption palette_option(
      QStringLiteral("palette"),
      QStringLiteral("Open the command palette at startup (testing)."));
  const QCommandLineOption ui_inventory_option(
      QStringLiteral("ui-inventory"),
      QStringLiteral("Print the widget inventory as JSON and exit."));
  const QCommandLineOption settings_option(
      QStringLiteral("settings"),
      QStringLiteral("Open Settings on this page id (providers, keys, ...)."),
      QStringLiteral("page"));
  const QCommandLineOption preferences_option(
      QStringLiteral("preferences"),
      QStringLiteral("Open Preferences at startup (testing)."));
  const QCommandLineOption pin_option(
      QStringLiteral("pin"),
      QStringLiteral("Pin this session id through the row-menu worker path "
                     "(verification)."),
      QStringLiteral("session"));
  const QCommandLineOption composer_text_option(
      QStringLiteral("composer-text"),
      QStringLiteral("Prefill the composer (verification)."),
      QStringLiteral("text"));
  const QCommandLineOption slash_option(
      QStringLiteral("slash"),
      QStringLiteral("Run a slash command at startup (verification)."),
      QStringLiteral("command"));
  const QCommandLineOption demo_running_option(
      QStringLiteral("demo-running"),
      QStringLiteral("With --demo, show the running/generating indicators."));
  const QCommandLineOption demo_complete_option(
      QStringLiteral("demo-complete"),
      QStringLiteral("Fire the completion feedback (sound, flash, status)."));
  const QCommandLineOption steer_rpc_option(
      QStringLiteral("steer-rpc"),
      QStringLiteral("Send session.steer directly (verification)."),
      QStringLiteral("text"));
  const QCommandLineOption probe_steer_option(
      QStringLiteral("probe-steer"),
      QStringLiteral("End-to-end steer check: mint a session, run a tiny turn, "
                     "steer it, delete the session."));
  const QCommandLineOption click_object_option(
      QStringLiteral("click-object"),
      QStringLiteral("Click the widget whose objectName matches, then exit 0 "
                     "if it was found."),
      QStringLiteral("name"));
  const QCommandLineOption purge_option(
      QStringLiteral("purge-probes"),
      QStringLiteral("Delete sessions created by the steer probe."));
  const QCommandLineOption press_option(
      QStringLiteral("press"),
      QStringLiteral("Send a key sequence to the focused widget after 1.2 s "
                     "(verification), e.g. Ctrl+K."),
      QStringLiteral("sequence"));
  const QCommandLineOption key_audit_option(
      QStringLiteral("key-audit"),
      QStringLiteral("Print every window shortcut and exit non-zero when two "
                     "actions share a sequence."));
  const QCommandLineOption demo_option(
      QStringLiteral("demo"),
      QStringLiteral("Show a synthetic transcript (reasoning, code, text) "
                     "instead of the live session."));
  const QCommandLineOption demo_click_option(
      QStringLiteral("demo-click"),
      QStringLiteral("With --demo, click a transcript target by name "
                     "(default: the reasoning header)."),
      QStringLiteral("target"));
  const QCommandLineOption session_option(
      QStringLiteral("session"),
      QStringLiteral("Open this session id at startup (verification)."),
      QStringLiteral("id"));
  const QCommandLineOption attach_option(
      QStringLiteral("attach"),
      QStringLiteral("Upload a file through the composer attach path and exit "
                     "(verification)."),
      QStringLiteral("path"));
  const QCommandLineOption page_option(
      QStringLiteral("page"),
      QStringLiteral("Open a workspace page at startup "
                     "(capabilities, messaging, artifacts, cron, profiles)."),
      QStringLiteral("id"));
  parser.addOption(screenshot_option);
  parser.addOption(profile_option);
  parser.addOption(theme_option);
  parser.addOption(bench_option);
  parser.addOption(duration_option);
  parser.addOption(screenshot_delay_option);
  parser.addOption(send_option);
  parser.addOption(static_option);
  parser.addOption(probe_option);
  parser.addOption(probe_ws_option);
  parser.addOption(new_session_option);
  parser.addOption(auto_approve_option);
  parser.addOption(auto_clarify_option);
  parser.addOption(palette_option);
  parser.addOption(ui_inventory_option);
  parser.addOption(settings_option);
  parser.addOption(preferences_option);
  parser.addOption(page_option);
  parser.addOption(pin_option);
  parser.addOption(attach_option);
  parser.addOption(session_option);
  parser.addOption(demo_option);
  parser.addOption(key_audit_option);
  parser.addOption(composer_text_option);
  parser.addOption(press_option);
  parser.addOption(slash_option);
  parser.addOption(steer_rpc_option);
  parser.addOption(probe_steer_option);
  parser.addOption(click_object_option);
  parser.addOption(purge_option);
  parser.addOption(demo_running_option);
  parser.addOption(demo_complete_option);
  parser.addOption(demo_click_option);
  parser.process(app);

  if (parser.isSet(probe_option)) {
    return RunGatewayProbe();
  }
  if (parser.isSet(probe_ws_option)) {
    return RunWebSocketProbe(&app);
  }

  const hermes::ui::ThemeMode mode =
      parser.value(theme_option) == QStringLiteral("dark")
          ? hermes::ui::ThemeMode::kDark
          : hermes::ui::ThemeMode::kLight;

  hermes::ui::MainWindow window(mode, !parser.isSet(static_option));
  window.SetAutoApprove(parser.isSet(auto_approve_option));
  window.SetAutoClarify(parser.value(auto_clarify_option));
  window.show();

  const QString profile = parser.value(profile_option);
  if (!profile.isEmpty()) {
    QTimer::singleShot(0, &window, [&window, profile]() {
      window.ActivateProfileByName(profile);
    });
  }

  if (parser.isSet(new_session_option)) {
    QTimer::singleShot(250, &window, [&window]() { window.NewSession(); });
  }
  if (parser.isSet(palette_option)) {
    QTimer::singleShot(1200, &window,
                       [&window]() { window.ShowCommandPalette(); });
  }
  if (parser.isSet(preferences_option)) {
    QTimer::singleShot(1200, &window,
                       [&window]() { window.ShowPreferences(); });
  }
  if (parser.isSet(settings_option)) {
    const QString page = parser.value(settings_option);
    QTimer::singleShot(900, &window,
                       [&window, page]() { window.ShowSettingsPage(page); });
  }
  if (parser.isSet(pin_option)) {
    const QString session = parser.value(pin_option);
    QTimer::singleShot(1500, &window, [&window, session]() {
      window.PinSessionForProbe(session);
    });
  }
  if (parser.isSet(slash_option)) {
    const QString command = parser.value(slash_option);
    QTimer::singleShot(2600, &window, [&window, command]() {
      window.RunSlashForProbe(command);
    });
  }
  if (parser.isSet(demo_running_option)) {
    QTimer::singleShot(900, &window,
                       [&window]() { window.SetDemoRunningForProbe(true); });
  }
  if (parser.isSet(click_object_option)) {
    const QString name = parser.value(click_object_option);
    // Widgets appear only after the gateway answers, so poll for the target
    // instead of firing at a fixed moment.
    auto* finder = new QTimer(&window);
    finder->setInterval(150);
    QObject::connect(
        finder, &QTimer::timeout, &window, [&window, name, &app, finder]() {
          static int attempts = 0;
          QWidget* target = window.findChild<QWidget*>(name);
          if (target == nullptr) {
            if (++attempts > 60) {
              qWarning("click-object: no widget named %s",
                       qUtf8Printable(name));
              finder->stop();
              app.exit(2);
            }
            return;
          }
          finder->stop();
          qInfo("click-object: clicking %s (%s)", qUtf8Printable(name),
                target->metaObject()->className());
          if (auto* button = qobject_cast<QAbstractButton*>(target)) {
            button->click();
          } else {
            const QPointF center(target->width() / 2.0, target->height() / 2.0);
            QMouseEvent press(QEvent::MouseButtonPress, center,
                              target->mapToGlobal(center.toPoint()),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(target, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, center,
                                target->mapToGlobal(center.toPoint()),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(target, &release);
          }
        });
    finder->start();
  }
  if (parser.isSet(purge_option)) {
    QTimer::singleShot(4000, &window,
                       [&window]() { window.PurgeProbeSessions(); });
  }
  if (parser.isSet(probe_steer_option)) {
    QTimer::singleShot(1500, &window, [&window]() { window.RunSteerProbe(); });
  }
  if (parser.isSet(steer_rpc_option)) {
    const QString text = parser.value(steer_rpc_option);
    QTimer::singleShot(2600, &window,
                       [&window, text]() { window.SteerForProbe(text); });
  }
  if (parser.isSet(demo_complete_option)) {
    QTimer::singleShot(1400, &window,
                       [&window]() { window.DemoCompleteForProbe(); });
  }
  if (parser.isSet(composer_text_option)) {
    const QString text = parser.value(composer_text_option);
    QTimer::singleShot(800, &window, [&window, text]() {
      window.SetComposerTextForProbe(text);
    });
  }
  if (parser.isSet(press_option)) {
    const QString sequence = parser.value(press_option);
    QTimer::singleShot(1200, &window, [&window, sequence]() {
      window.PressKeyForProbe(sequence);
    });
  }
  if (parser.isSet(composer_text_option) && parser.isSet(screenshot_option)) {
    const QString shot =
        parser.value(screenshot_option) + QStringLiteral(".menu");
    QTimer::singleShot(1400, &window, [shot]() {
      for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->objectName() != QStringLiteral("slashMenu")) {
          continue;
        }
        fprintf(stdout, "slash menu visible=%d geometry=%dx%d at %d,%d\n",
                widget->isVisible() ? 1 : 0, widget->width(), widget->height(),
                widget->x(), widget->y());
        fflush(stdout);
        const QPixmap pixmap = widget->grab();
        fprintf(stdout, "slash menu grab=%dx%d saved=%d\n", pixmap.width(),
                pixmap.height(), pixmap.save(shot, "PNG") ? 1 : 0);
        fflush(stdout);
      }
    });
  }
  if (parser.isSet(key_audit_option)) {
    const QStringList rows = window.ShortcutAudit();
    bool duplicate = false;
    for (const QString& row : rows) {
      if (row.startsWith(QStringLiteral("DUPLICATE"))) {
        duplicate = true;
      }
      fprintf(stdout, "%s\n", qUtf8Printable(row));
    }
    fflush(stdout);
    QTimer::singleShot(0, &app,
                       [&app, duplicate]() { app.exit(duplicate ? 1 : 0); });
  }
  if (parser.isSet(demo_option)) {
    QTimer::singleShot(600, &window,
                       [&window]() { window.ShowDemoTranscript(); });
  }
  if (parser.isSet(demo_click_option)) {
    const QString target = parser.value(demo_click_option).isEmpty()
                               ? QStringLiteral("chat:reasoning")
                               : parser.value(demo_click_option);
    QTimer::singleShot(1300, &window,
                       [&window, target]() { window.ClickForProbe(target); });
  }
  if (parser.isSet(session_option)) {
    const QString session = parser.value(session_option);
    QTimer::singleShot(900, &window, [&window, session]() {
      window.SelectSessionForProbe(session);
    });
  }
  if (parser.isSet(attach_option)) {
    const QString path = parser.value(attach_option);
    QTimer::singleShot(1500, &window,
                       [&window, path]() { window.AttachFileForProbe(path); });
  }
  if (parser.isSet(page_option)) {
    const QString page = parser.value(page_option);
    QTimer::singleShot(1000, &window,
                       [&window, page]() { window.ShowWorkspacePage(page); });
  }
  if (parser.isSet(ui_inventory_option)) {
    QTimer::singleShot(1800, &app, [&window]() {
      QJsonArray widgets;
      for (QWidget* widget : window.findChildren<QWidget*>()) {
        const QString object = widget->objectName();
        const QString accessible = widget->accessibleName();
        if (object.isEmpty() && accessible.isEmpty()) {
          continue;
        }
        QJsonObject row;
        row.insert(QStringLiteral("object"), object);
        row.insert(QStringLiteral("accessible"), accessible);
        row.insert(QStringLiteral("class"), widget->metaObject()->className());
        widgets.append(row);
      }
      QJsonObject root;
      root.insert(QStringLiteral("widgets"), widgets);
      QTextStream out(stdout);
      out << QJsonDocument(root).toJson(QJsonDocument::Compact) << "\n";
      out.flush();
      QCoreApplication::quit();
    });
  }

  const QString send_text = parser.value(send_option);
  if (!send_text.isEmpty()) {
    QTimer::singleShot(1500, &window, [&window, send_text]() {
      window.SubmitText(send_text);
    });
  }

  const QString screenshot_path = parser.value(screenshot_option);
  const int bench_count = parser.value(bench_option).toInt();
  const int duration_ms = parser.value(duration_option).toInt();
  const int screenshot_delay_ms = parser.value(screenshot_delay_option).toInt();

  if (bench_count > 0) {
    QTimer::singleShot(
        900, &window, [&window, &app, bench_count, screenshot_path]() {
          window.BenchSwitch(bench_count);
          if (!screenshot_path.isEmpty()) {
            QTimer::singleShot(200, &window,
                               [&window, &app, screenshot_path]() {
                                 QWidget* target = &window;
                                 if (auto* dialog = window.findChild<QDialog*>(
                                         QStringLiteral("preferences"));
                                     dialog != nullptr && dialog->isVisible()) {
                                   target = dialog;
                                 }
                                 target->grab().save(screenshot_path, "PNG");
                                 app.quit();
                               });
          } else {
            app.quit();
          }
        });
  } else if (!screenshot_path.isEmpty()) {
    QTimer::singleShot(
        screenshot_delay_ms, &window, [&window, &app, screenshot_path]() {
          QWidget* target = &window;
          if (auto* dialog =
                  window.findChild<QDialog*>(QStringLiteral("preferences"));
              dialog != nullptr && dialog->isVisible()) {
            target = dialog;
          }
          target->grab().save(screenshot_path, "PNG");
          app.quit();
        });
  } else if (duration_ms > 0) {
    QTimer::singleShot(duration_ms, &app, &QCoreApplication::quit);
  }

  return app.exec();
}
