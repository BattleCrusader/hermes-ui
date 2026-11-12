// Copyright 2026 The Hermes Qt Spike Authors.

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QSvgRenderer>
#include <QSvgWidget>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextEdit>
#include <QTreeView>
#include <QWebSocket>
#include <QWebSocketServer>
#include <QtTest>
#include <vector>

#include "src/artifacts.h"
#include "src/audio_data_url.h"
#include "src/browser_automation.h"
#include "src/cache_store.h"
#include "src/composer.h"
#include "src/flow_layout.h"
#include "src/message_history.h"
#include "src/model.h"
#include "src/model_visibility.h"
#include "src/native_prefs.h"
#include "src/notification_prefs.h"
#include "src/preferences_dialog.h"
#include "src/profile_selection.h"
#include "src/project_appearance.h"
#include "src/project_dialog.h"
#include "src/projects.h"
#include "src/prompt_history_dialog.h"
#include "src/prompt_submit.h"
#include "src/right_rail.h"
#include "src/session_search_bar.h"
#include "src/session_transfer.h"
#include "src/session_usage.h"
#include "src/status_bar.h"
#include "src/subagents.h"
#include "src/terminal_widget.h"
#include "src/todo_state.h"

namespace {

hermes::ui::ProfileSnapshot Snapshot() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.label = QStringLiteral("default");
  snapshot.gateway_version = QStringLiteral("test");
  snapshot.provider = QStringLiteral("p1");
  snapshot.model = QStringLiteral("m1");
  snapshot.reasoning_effort = QStringLiteral("medium");
  hermes::ui::ModelProvider provider;
  provider.name = QStringLiteral("Provider One");
  provider.slug = QStringLiteral("p1");
  provider.models = {QStringLiteral("m1"), QStringLiteral("m2")};
  snapshot.model_providers = {provider};
  return snapshot;
}

bool ExpandRailSection(hermes::ui::RightRail& rail, const QString& id) {
  auto* header = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailSection_%1").arg(id));
  if (header == nullptr) {
    return false;
  }
  if (!header->property("expanded").toBool()) {
    header->click();
  }
  return header->property("expanded").toBool();
}

}  // namespace

// The desktop contract is the model-settings/model-pill/reasoning-pill test
// set. These native cases prove the same values travel through the dialogs and
// composer controls instead of merely existing in the widget tree.
class PreferencesTest : public QObject {
  Q_OBJECT

 private slots:
  void applyEmitsTheSelectedRoute();
  void composerControlsExposeTheSameRoute();
  void composerHistoryBrowsesAndRestoresDraft();
  void composerTokenMeterFormatsContextUsage();
  void composerExpandsStatusLineWhileRunning();
  void selectedTurnRunningFallsBackToStreaming();
  void composerQueuedStatusOpensReview();
  void statusBarShowsUsageInFooter();
  void composerLiveVoiceStateAndPinnedPill();
  void composerSteersQueuesAndPastesImages();
  void sessionUsageMergesNestedAndLegacyFrames();
  void sessionImportAcceptsBareAndWrappedShapes();
  void composerMentionsCompletePathsAndPromoteRefs();
  void notificationPolicyMatchesDesktopAttentionRules();
  void subagentEventsBuildALiveDelegationTree();
  void audioDataUrlDecodesSpeechAndRejectsJunk();
  void attachmentAttachRpcMatchesMediaType();
  void rightRailShowsContextAgentsAndArtifacts();
  void rightRailPreviewsArtifactText();
  void rightRailKeepsArtifactScrollAcrossSnapshots();
  void rightRailProjectGitTerminalPanes();
  void flowLayoutWrapsRowsAtNarrowWidths();
  void artifactDetectionAndNamingMatchDesktop();
  void rightRailBrowsesGeneratedArtifactVersions();
  void rightRailRendersHtmlArtifactAndSource();
  void rightRailShowsTodoTimelineAndFiltersPrompts();
  void sessionSearchBarFindsAndOpensSessions();
  void modelVisibilityFiltersPickerAndPersists();
  void projectsParseListTreeAndMutators();
  void promptHistoryEditsQueuedAndSteeredEntries();
  void promptHistoryInjectsLocalAndServerQueues();
  void promptHistoryEditsAndCancelsServerQueue();
  void projectDialogEditsCreateAndDisplayFields();
  void projectAppearanceParsesColorsIconsAndFiltersDismissedAutos();
  void uiCacheRoundTripsSessionsAndProjects();
  void profileSelectionFallsBackWhenSavedNameIsGone();
  void browserAutomationParsesEndpointsAndConsent();
  void browserAutomationBuildsPointerEventsAndScreenshots();
  void cdpClientDispatchesScreenshotAndCoordinates();
  void cdpClientDiscoversHttpAndAttachesBrowserTarget();
  void browserAutomationDialogSurfacesControls();
  void historyHydratesAndPreservesDisplayReasoning();
};

void PreferencesTest::attachmentAttachRpcMatchesMediaType() {
  using hermes::ui::AttachmentAttachMethod;
  using hermes::ui::BuildAttachmentAttachParams;
  QCOMPARE(AttachmentAttachMethod(QStringLiteral("/tmp/shot.PNG")),
           QStringLiteral("image.attach"));
  QCOMPARE(AttachmentAttachMethod(QStringLiteral("/tmp/doc.pdf")),
           QStringLiteral("pdf.attach"));
  QCOMPARE(AttachmentAttachMethod(QStringLiteral("/tmp/notes.md")),
           QStringLiteral("file.attach"));
  const QJsonObject params = BuildAttachmentAttachParams(
      QStringLiteral("/tmp/report.txt"), QStringLiteral("session-a"));
  QCOMPARE(params.value(QStringLiteral("session_id")).toString(),
           QStringLiteral("session-a"));
  QCOMPARE(params.value(QStringLiteral("path")).toString(),
           QStringLiteral("/tmp/report.txt"));
  QCOMPARE(params.value(QStringLiteral("name")).toString(),
           QStringLiteral("report.txt"));
  QVERIFY(!BuildAttachmentAttachParams(QStringLiteral("/tmp/shot.png"),
                                       QStringLiteral("session-a"))
               .contains(QStringLiteral("name")));
}

void PreferencesTest::audioDataUrlDecodesSpeechAndRejectsJunk() {
  QByteArray bytes;
  QString mime;
  QString error;
  const QByteArray payload("RIFFfake-wave");
  const QString url = QStringLiteral("data:audio/wav;base64,") +
                      QString::fromLatin1(payload.toBase64());
  QVERIFY(hermes::ui::DecodeAudioDataUrl(url, &bytes, &mime, &error));
  QCOMPARE(bytes, payload);
  QCOMPARE(mime, QStringLiteral("audio/wav"));
  QCOMPARE(hermes::ui::AudioFileSuffixForMime(mime), QStringLiteral(".wav"));
  QCOMPARE(hermes::ui::AudioFileSuffixForMime(QStringLiteral("audio/ogg")),
           QStringLiteral(".ogg"));
  QCOMPARE(hermes::ui::AudioFileSuffixForMime(QStringLiteral("audio/mpeg")),
           QStringLiteral(".mp3"));
  QCOMPARE(hermes::ui::AudioMimeForFilePath(QStringLiteral("/tmp/note.m4a")),
           QStringLiteral("audio/mp4"));
  QCOMPARE(hermes::ui::AudioMimeForFilePath(QStringLiteral("/tmp/note.ogg")),
           QStringLiteral("audio/ogg"));
  QVERIFY(hermes::ui::AudioMimeForFilePath(QStringLiteral("/tmp/note.txt"))
              .isEmpty());
  const QString encoded = hermes::ui::EncodeAudioDataUrl(payload, mime);
  QVERIFY(encoded.startsWith(QStringLiteral("data:audio/wav;base64,")));
  QByteArray round_trip;
  QString round_mime;
  QVERIFY(hermes::ui::DecodeAudioDataUrl(encoded, &round_trip, &round_mime,
                                         &error));
  QCOMPARE(round_trip, payload);
  QCOMPARE(round_mime, mime);

  QVERIFY(!hermes::ui::DecodeAudioDataUrl(QStringLiteral("not-a-data-url"),
                                          &bytes, &mime, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!hermes::ui::DecodeAudioDataUrl(
      QStringLiteral("data:text/plain;base64,aGVsbG8="), &bytes, &mime,
      &error));
  QVERIFY(error.contains(QStringLiteral("audio")));
  QVERIFY(!hermes::ui::DecodeAudioDataUrl(
      QStringLiteral("data:audio/mpeg;base64,"), &bytes, &mime, &error));
  QVERIFY(!error.isEmpty());
}

void PreferencesTest::applyEmitsTheSelectedRoute() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::PreferencesDialog dialog(theme);
  dialog.SetSnapshot(Snapshot());

  auto* provider =
      dialog.findChild<QComboBox*>(QStringLiteral("preferencesProvider"));
  auto* model =
      dialog.findChild<QComboBox*>(QStringLiteral("preferencesModel"));
  auto* reasoning =
      dialog.findChild<QComboBox*>(QStringLiteral("preferencesReasoning"));
  auto* apply =
      dialog.findChild<QPushButton*>(QStringLiteral("preferencesApply"));
  QVERIFY(provider != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(reasoning != nullptr);
  QVERIFY(apply != nullptr);
  QCOMPARE(provider->count(), 1);
  QCOMPARE(model->count(), 2);
  QVERIFY(reasoning->findData(QStringLiteral("medium")) >= 0);

  provider->setCurrentIndex(0);
  model->setCurrentIndex(1);
  reasoning->setCurrentIndex(reasoning->findData(QStringLiteral("high")));

  QString got_provider;
  QString got_model;
  QString got_reasoning;
  dialog.on_apply = [&](const QString& provider_id, const QString& model_id,
                        const QString& effort) {
    got_provider = provider_id;
    got_model = model_id;
    got_reasoning = effort;
  };
  apply->click();

  QCOMPARE(got_provider, QStringLiteral("p1"));
  QCOMPARE(got_model, QStringLiteral("m2"));
  QCOMPARE(got_reasoning, QStringLiteral("high"));
}

void PreferencesTest::composerControlsExposeTheSameRoute() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.SetModelCatalog(Snapshot().model_providers, QStringLiteral("p1"),
                           QStringLiteral("m1"));
  composer.SetReasoning(QStringLiteral("high"));

  auto* model = composer.findChild<QPushButton*>(QStringLiteral("modelPicker"));
  auto* reasoning =
      composer.findChild<QPushButton*>(QStringLiteral("reasoningPill"));
  QVERIFY(model != nullptr);
  QVERIFY(reasoning != nullptr);
  QCOMPARE(model->text(), QStringLiteral("m1"));
  QCOMPARE(reasoning->text(), QStringLiteral("high"));
}

void PreferencesTest::composerHistoryBrowsesAndRestoresDraft() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.SetHistory(
      {QStringLiteral("newest prompt"), QStringLiteral("older prompt")});
  auto* editor = composer.findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  composer.SetText(QStringLiteral("draft text"));
  editor->moveCursor(QTextCursor::Start);

  QTest::keyClick(editor, Qt::Key_Up);
  QCOMPARE(composer.Text(), QStringLiteral("newest prompt"));
  QTest::keyClick(editor, Qt::Key_Up);
  QCOMPARE(composer.Text(), QStringLiteral("older prompt"));
  QTest::keyClick(editor, Qt::Key_Up);
  QCOMPARE(composer.Text(), QStringLiteral("older prompt"));
  QTest::keyClick(editor, Qt::Key_Down);
  QCOMPARE(composer.Text(), QStringLiteral("newest prompt"));
  QTest::keyClick(editor, Qt::Key_Down);
  QCOMPARE(composer.Text(), QStringLiteral("draft text"));

  composer.SetText(QStringLiteral("line one\nline two"));
  editor->moveCursor(QTextCursor::Start);
  QTest::keyClick(editor, Qt::Key_Up);
  QCOMPARE(composer.Text(), QStringLiteral("newest prompt"));
  composer.SetHistory(
      {QStringLiteral("newest prompt"), QStringLiteral("older prompt")});
  composer.SetText(QStringLiteral("plain"));
  editor->moveCursor(QTextCursor::End);
  QTest::keyClick(editor, Qt::Key_Up);
  QCOMPARE(composer.Text(), QStringLiteral("plain"));
}

void PreferencesTest::composerTokenMeterFormatsContextUsage() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  QVERIFY(composer.TokenUsageText().isEmpty());
  composer.SetUsage(42000, 200000);
  QCOMPARE(composer.TokenUsageText(), QStringLiteral("42k / 200k · 21%"));
  composer.SetUsage(900, 10000);
  QCOMPARE(composer.TokenUsageText(), QStringLiteral("900 / 10k · 9%"));
  composer.SetUsage(200000, 200000);
  QCOMPARE(composer.TokenUsageText(), QStringLiteral("200k / 200k · 100%"));
}

void PreferencesTest::composerExpandsStatusLineWhileRunning() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  QCOMPARE(composer.height(), 116);
  QSignalSpy heights(&composer, &hermes::ui::Composer::HeightChanged);
  composer.SetRunning(true);
  QCOMPARE(composer.height(), 134);
  QCOMPARE(heights.count(), 1);
  QCOMPARE(heights.takeFirst().at(0).toInt(), 134);
  composer.SetRunning(false);
  QCOMPARE(composer.height(), 116);
  QCOMPARE(heights.count(), 1);
  QCOMPARE(heights.takeFirst().at(0).toInt(), 116);
  // Idempotent calls must not churn the layout.
  composer.SetRunning(false);
  QVERIFY(heights.isEmpty());
}

void PreferencesTest::composerQueuedStatusOpensReview() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(600, 134);
  composer.show();
  composer.SetRunning(true);
  composer.SetQueuedPrompts(2);
  QSignalSpy review(&composer, &hermes::ui::Composer::QueuedReviewRequested);
  QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier,
                    QPoint(40, composer.height() - 10));
  QCOMPARE(review.count(), 1);
  // A click on the action row must not open the queue list.
  QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier, QPoint(40, 70));
  QCOMPARE(review.count(), 1);
}

void PreferencesTest::selectedTurnRunningFallsBackToStreaming() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.selected_session_id = QStringLiteral("chat-a");
  snapshot.running_session_id = QStringLiteral("chat-a");
  snapshot.running = false;
  hermes::ui::ChatMessage message;
  message.streaming = true;
  snapshot.messages.push_back(message);
  QVERIFY(hermes::ui::SelectedTurnRunning(snapshot));

  snapshot.messages.back().streaming = false;
  snapshot.messages.back().reasoning_streaming = true;
  QVERIFY(hermes::ui::SelectedTurnRunning(snapshot));

  // A background chat must not inherit the spinner.
  snapshot.running_session_id = QStringLiteral("chat-b");
  QVERIFY(!hermes::ui::SelectedTurnRunning(snapshot));

  snapshot.running_session_id = QStringLiteral("chat-a");
  snapshot.messages.back().reasoning_streaming = false;
  snapshot.running = true;
  QVERIFY(hermes::ui::SelectedTurnRunning(snapshot));
}

void PreferencesTest::statusBarShowsUsageInFooter() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::StatusBar bar(theme);
  bar.resize(900, 26);
  bar.SetState(QStringLiteral("6 profiles warm"),
               QStringLiteral("switch ready"),
               QStringLiteral("default - gateway 0.23.0"), true);
  bar.SetUsage(QStringLiteral("9.2k / 128k · 7%"));
  QCOMPARE(bar.Usage(), QStringLiteral("9.2k / 128k · 7%"));
  bar.show();
  QTest::qWait(20);

  // The usage string is painted in the footer's right third, immediately left
  // of the gateway label: count non-background pixels there.
  const QImage image = bar.grab().toImage();
  int painted = 0;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = image.width() * 2 / 3; x < image.width(); ++x) {
      if (image.pixelColor(x, y) != theme.sidebar) {
        ++painted;
      }
    }
  }
  QVERIFY2(painted > 100, qUtf8Printable(QString::number(painted)));
  bar.SetUsage(QString());
  QVERIFY(bar.Usage().isEmpty());
}

void PreferencesTest::composerLiveVoiceStateAndPinnedPill() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.resize(700, 160);
  composer.show();
  composer.SetPinnedContextCount(2);
  composer.SetVoiceState(true, QString(), QString());
  QCOMPARE(composer.VoiceStatusText(), QStringLiteral("Listening…"));
  composer.SetVoiceState(true, QStringLiteral("hello there"), QString());
  QCOMPARE(composer.VoiceStatusText(), QStringLiteral("hello there"));
  composer.SetVoiceState(false, QString(), QStringLiteral("Transcribing…"));

  QCOMPARE(composer.VoiceStatusText(), QStringLiteral("Transcribing…"));
  QSignalSpy toggled(&composer, &hermes::ui::Composer::VoiceToggleRequested);
  QRectF voice;
  for (const auto& target : composer.ClickTargets()) {
    if (target.name == QStringLiteral("composer:voice")) {
      voice = target.rect;
    }
  }
  QVERIFY2(!voice.isNull(), "the composer needs a live-voice click target");
  QTest::mouseClick(&composer, Qt::LeftButton, Qt::NoModifier,
                    voice.center().toPoint());
  QCOMPARE(toggled.count(), 1);
}

void PreferencesTest::composerSteersQueuesAndPastesImages() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  auto* editor = composer.findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  QString steered;
  QString queued;
  QObject::connect(&composer, &hermes::ui::Composer::SteerRequested,
                   [&](const QString& text) { steered = text; });
  QObject::connect(&composer, &hermes::ui::Composer::QueueRequested,
                   [&](const QString& text) { queued = text; });
  composer.SetRunning(true);
  composer.SetQueuedPrompts(2);
  composer.SetText(QStringLiteral("keep going"));
  editor->setFocus();
  QTest::keyClick(editor, Qt::Key_Return);
  QCOMPARE(steered, QStringLiteral("keep going"));
  composer.SetText(QStringLiteral("next turn"));
  QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
  QCOMPARE(queued, QStringLiteral("next turn"));
  QVERIFY(steered != queued);

  QSignalSpy dropped(&composer, &hermes::ui::Composer::FilesDropped);
  QImage image(8, 8, QImage::Format_ARGB32);
  image.fill(Qt::red);
  QGuiApplication::clipboard()->setImage(image);
  composer.SetText(QString());
  QTest::keyClick(editor, Qt::Key_V, Qt::ControlModifier);
  QTRY_COMPARE(dropped.count(), 1);
  const QStringList paths = dropped.at(0).at(0).toStringList();
  QCOMPARE(paths.size(), 1);
  QVERIFY(QFileInfo::exists(paths.first()));
  QVERIFY(paths.first().endsWith(QStringLiteral(".png")));

  // Overflow attachments collapse into a +N drawer instead of disappearing
  // from the UI entirely. The drawer lists the hidden files and can remove one.
  composer.resize(700, 160);
  composer.Clear();
  for (int i = 0; i < 12; ++i) {
    composer.AddAttachment(
        QStringLiteral("very-long-attachment-name-%1.txt").arg(i),
        QStringLiteral("/tmp/hermes-attachment-%1.txt").arg(i));
  }
  QMenu* drawer = composer.ShowAttachmentDrawerForTest();
  QVERIFY(drawer != nullptr);
  const QStringList before = composer.AttachmentPaths();
  QCOMPARE(before.size(), 12);
  QVERIFY(drawer->actions().size() < before.size());
  QVERIFY(drawer->actions().size() > 0);
  const QString removed = drawer->actions().last()->text();
  drawer->actions().last()->trigger();
  delete drawer;
  const QStringList after = composer.AttachmentPaths();
  QCOMPARE(after.size(), before.size() - 1);
  QVERIFY(removed.contains(QStringLiteral("very-long-attachment-name")));
  QVERIFY(!after.contains(before.last()));
}

void PreferencesTest::sessionUsageMergesNestedAndLegacyFrames() {
  hermes::ui::SessionUsage usage;
  usage.compressions = 1;
  const QJsonObject legacy = QJsonObject{
      {QStringLiteral("api_calls"), 3},
      {QStringLiteral("input_tokens"), 1200},
      {QStringLiteral("output_tokens"), 80},
      {QStringLiteral("prompt_tokens"), 42000},
      {QStringLiteral("context_length"), 200000},
      {QStringLiteral("total_tokens"), 1280},
      {QStringLiteral("compression_count"), 2},
  };
  usage = hermes::ui::MergeSessionUsage(usage, legacy);
  QCOMPARE(usage.calls, 3);
  QCOMPARE(usage.input, 1200);
  QCOMPARE(usage.output, 80);
  QCOMPARE(usage.context_used, 42000);
  QCOMPARE(usage.context_max, 200000);
  QCOMPARE(usage.context_percent, 21);
  QCOMPARE(usage.total, 1280);
  QCOMPARE(usage.compressions, 2);

  const QJsonObject nested = QJsonObject{
      {QStringLiteral("usage"),
       QJsonObject{
           {QStringLiteral("context_estimated"), true},
           {QStringLiteral("context_max"), 100000},
           {QStringLiteral("context_percent"), 42},
           {QStringLiteral("context_used"), 42000},
           {QStringLiteral("total"), 999},
       }},
      {QStringLiteral("api_calls"), 8},
  };
  usage = hermes::ui::MergeSessionUsage(usage, nested);
  // Nested fields win; the top-level counter fills the omitted `calls`, and
  // the omitted `compressions` keeps its previous value instead of resetting.
  QCOMPARE(usage.context_used, 42000);
  QCOMPARE(usage.context_max, 100000);
  QCOMPARE(usage.context_percent, 42);
  QVERIFY(usage.context_estimated);
  QCOMPARE(usage.total, 999);
  QCOMPARE(usage.calls, 8);
  QCOMPARE(usage.compressions, 2);
}

void PreferencesTest::sessionImportAcceptsBareAndWrappedShapes() {
  QJsonArray sessions;
  QString error;
  const QJsonObject one{{QStringLiteral("id"), QStringLiteral("s1")},
                        {QStringLiteral("messages"), QJsonArray{}}};

  QVERIFY(hermes::ui::ParseSessionImport(
      QJsonDocument(QJsonArray{one}).toJson(), &sessions, &error));
  QCOMPARE(sessions.size(), 1);
  QCOMPARE(sessions.first().toObject().value(QStringLiteral("id")).toString(),
           QStringLiteral("s1"));

  QVERIFY(hermes::ui::ParseSessionImport(
      QJsonDocument(
          QJsonObject{{QStringLiteral("sessions"), QJsonArray{one, one}}})
          .toJson(),
      &sessions, &error));
  QCOMPARE(sessions.size(), 2);

  // A single exported session object is also importable.
  QVERIFY(hermes::ui::ParseSessionImport(QJsonDocument(one).toJson(), &sessions,
                                         &error));
  QCOMPARE(sessions.size(), 1);

  QVERIFY(!hermes::ui::ParseSessionImport(
      QJsonDocument(QJsonObject{{QStringLiteral("sessions"), QJsonArray()}})
          .toJson(),
      &sessions, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!hermes::ui::ParseSessionImport("{not json", &sessions, &error));
}

void PreferencesTest::composerMentionsCompletePathsAndPromoteRefs() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  composer.SetMentionRoot(QStringLiteral("/work"));
  QStringList listed;
  composer.SetPathLister([&](const QString& path,
                             hermes::ui::Composer::PathListCallback done) {
    listed.append(path);
    if (path == QStringLiteral("/work")) {
      done(
          QJsonArray{
              QJsonObject{{QStringLiteral("name"), QStringLiteral("src")},
                          {QStringLiteral("path"), QStringLiteral("/work/src")},
                          {QStringLiteral("isDirectory"), true}},
              QJsonObject{
                  {QStringLiteral("name"), QStringLiteral("README.md")},
                  {QStringLiteral("path"), QStringLiteral("/work/README.md")},
                  {QStringLiteral("isDirectory"), false}}},
          QString());
      return;
    }
    if (path == QStringLiteral("/work/src")) {
      done(QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("main.cc")},
               {QStringLiteral("path"), QStringLiteral("/work/src/main.cc")},
               {QStringLiteral("isDirectory"), false}}},
           QString());
      return;
    }
    done(QJsonArray(), QString());
  });
  auto* editor = composer.findChild<QTextEdit*>();
  QVERIFY(editor != nullptr);
  editor->setFocus();

  composer.SetText(QStringLiteral("@"));
  QVERIFY(composer.MentionMenuVisible());
  QCOMPARE(composer.MentionMenuEntries(),
           QStringList({QStringLiteral("src/"), QStringLiteral("README.md")}));
  QTest::keyClick(editor, Qt::Key_Tab);
  QCOMPARE(composer.Text(), QStringLiteral("@src/"));
  QVERIFY(composer.MentionMenuVisible());
  QCOMPARE(composer.MentionMenuEntries(),
           QStringList({QStringLiteral("main.cc")}));
  QVERIFY(listed.contains(QStringLiteral("/work/src")));
  QTest::keyClick(editor, Qt::Key_Tab);
  QCOMPARE(composer.Text(), QStringLiteral("@src/main.cc "));
  QVERIFY(!composer.MentionMenuVisible());

  QCOMPARE(hermes::ui::Composer::PromotePathRefs(
               QStringLiteral("see @src/main.cc and @docs/")),
           QStringLiteral("see @file:\"src/main.cc\" and @folder:\"docs\""));
  QCOMPARE(hermes::ui::Composer::PromotePathRefs(
               QStringLiteral("@file:\"already/typed\" and @teknium1")),
           QStringLiteral("@file:\"already/typed\" and @teknium1"));
}

void PreferencesTest::subagentEventsBuildALiveDelegationTree() {
  using hermes::ui::ApplySubagentEvent;
  using hermes::ui::BuildSubagentTree;
  using hermes::ui::FormatSubagentDuration;
  using hermes::ui::FormatSubagentTokens;
  using hermes::ui::SubagentIsTerminal;
  using hermes::ui::SubagentProgress;
  using hermes::ui::SubagentStatus;
  using hermes::ui::SubagentStatusText;

  std::vector<SubagentProgress> rows;
  // The wire shape of a spawn: identity kwargs plus the child's goal.
  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.start"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("goal"), QStringLiteral("Research pricing")},
                  {QStringLiteral("model"), QStringLiteral("haiku")},
                  {QStringLiteral("task_count"), 2},
                  {QStringLiteral("task_index"), 0},
                  {QStringLiteral("tool_count"), 0}},
      1000);
  QCOMPARE(rows.size(), std::size_t{1});
  QCOMPARE(rows[0].status, SubagentStatus::kRunning);
  QCOMPARE(rows[0].session_id, QStringLiteral("s1"));
  QCOMPARE(rows[0].model, QStringLiteral("haiku"));

  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.progress"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("preview"), QStringLiteral("Reading docs")},
                  {QStringLiteral("tool_name"), QStringLiteral("web_search")},
                  {QStringLiteral("tool_preview"),
                   QStringLiteral("competitor pricing")},
                  {QStringLiteral("tool_count"), 3},
                  {QStringLiteral("input_tokens"), 1200},
                  {QStringLiteral("output_tokens"), 340},
                  {QStringLiteral("duration_seconds"), 4.2},
                  {QStringLiteral("files_read"),
                   QJsonArray{QStringLiteral("docs/pricing.md")}}},
      2000);
  QCOMPARE(rows[0].tool_count, 3);
  QCOMPARE(rows[0].input_tokens, qint64{1200});
  QCOMPARE(rows[0].output_tokens, qint64{340});
  QCOMPARE(rows[0].files_read, QStringList{QStringLiteral("docs/pricing.md")});
  QCOMPARE(rows[0].current_tool, QStringLiteral("Web Search"));
  QCOMPARE(rows[0].stream.size(), std::size_t{2});
  QCOMPARE(rows[0].stream[0].kind, QStringLiteral("tool"));
  QCOMPARE(rows[0].stream[0].text,
           QStringLiteral("Web Search(\"competitor pricing\")"));
  QCOMPARE(rows[0].stream[1].text, QStringLiteral("Reading docs"));

  // Replaying the same event is idempotent at the stream level.
  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.progress"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("preview"), QStringLiteral("Reading docs")},
                  {QStringLiteral("tool_name"), QStringLiteral("web_search")},
                  {QStringLiteral("tool_preview"),
                   QStringLiteral("competitor pricing")},
                  {QStringLiteral("tool_count"), 3},
                  {QStringLiteral("input_tokens"), 1200},
                  {QStringLiteral("output_tokens"), 340},
                  {QStringLiteral("duration_seconds"), 4.2}},
      2100);
  QCOMPARE(rows[0].stream.size(), std::size_t{2});

  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.thinking"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("text"), QStringLiteral("Comparing tiers")}},
      2200);
  QCOMPARE(rows[0].stream.back().kind, QStringLiteral("thinking"));

  // A child agent nests under its parent and sorts before it in the tree.
  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.start"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("b")},
                  {QStringLiteral("parent_id"), QStringLiteral("a")},
                  {QStringLiteral("goal"), QStringLiteral("Check sources")}},
      2300);
  QCOMPARE(rows.size(), std::size_t{2});
  const auto ordered = BuildSubagentTree(rows);
  QCOMPARE(ordered.size(), std::size_t{2});
  QCOMPARE(ordered[0]->id, QStringLiteral("a"));
  QCOMPARE(ordered[1]->id, QStringLiteral("b"));

  ApplySubagentEvent(
      &rows, QStringLiteral("subagent.complete"), QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("task_count"), 2},
                  {QStringLiteral("task_index"), 0},
                  {QStringLiteral("status"), QStringLiteral("failed")},
                  {QStringLiteral("summary"), QStringLiteral("Timed out")},
                  {QStringLiteral("duration_seconds"), 65.0},
                  {QStringLiteral("cost_usd"), 0.012},
                  {QStringLiteral("files_written"),
                   QJsonArray{QStringLiteral("artifacts/pricing.md")}}},
      64000);
  QCOMPARE(rows[0].status, SubagentStatus::kFailed);
  QVERIFY(SubagentIsTerminal(rows[0].status));
  QVERIFY(rows[0].current_tool.isEmpty());
  QCOMPARE(rows[0].summary, QStringLiteral("Timed out"));
  QCOMPARE(rows[0].stream.back().kind, QStringLiteral("summary"));
  QVERIFY(rows[0].stream.back().error);
  QCOMPARE(rows[0].files_written,
           QStringList{QStringLiteral("artifacts/pricing.md")});
  QCOMPARE(SubagentStatusText(rows[0].status), QStringLiteral("Failed"));
  QCOMPARE(SubagentStatusText(SubagentStatus::kInterrupted),
           QStringLiteral("Interrupted"));
  QCOMPARE(FormatSubagentDuration(4.2), QStringLiteral("4.2s"));
  QCOMPARE(FormatSubagentDuration(65.0), QStringLiteral("1m 5s"));
  QCOMPARE(FormatSubagentTokens(340), QStringLiteral("340"));
  QCOMPARE(FormatSubagentTokens(1200), QStringLiteral("1.2k"));
  QCOMPARE(FormatSubagentTokens(2400000), QStringLiteral("2.4M"));

  // The stream is capped so a chatty child cannot grow the snapshot forever.
  for (int i = 0; i < 40; ++i) {
    ApplySubagentEvent(
        &rows, QStringLiteral("subagent.progress"), QStringLiteral("s1"),
        QJsonObject{
            {QStringLiteral("subagent_id"), QStringLiteral("b")},
            {QStringLiteral("preview"), QStringLiteral("step %1").arg(i)}},
        65000 + i);
  }
  QCOMPARE(rows[1].stream.size(), std::size_t{24});
  QCOMPARE(rows[1].stream.back().text, QStringLiteral("step 39"));
}

void PreferencesTest::rightRailShowsContextAgentsAndArtifacts() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 640);
  rail.show();

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.selected_session_id = QStringLiteral("s1");
  snapshot.usage.context_used = 12000;
  snapshot.usage.context_max = 200000;
  snapshot.usage.context_percent = 6;
  snapshot.usage.total = 18000;
  snapshot.session_control_summary = QStringLiteral("Goal: active — Ship it");
  snapshot.gateway_notices.push_back(hermes::ui::GatewayNotice{
      .key = QStringLiteral("credits"),
      .text = QStringLiteral("1 credit left"),
      .level = QStringLiteral("warn"),
      .serial = 4,
  });
  snapshot.subagents.push_back(hermes::ui::SubagentProgress{
      .id = QStringLiteral("a"),
      .goal = QStringLiteral("Audit pricing"),
      .status = hermes::ui::SubagentStatus::kRunning,
      .duration_seconds = 4.2,
  });
  hermes::ui::ChatMessage first;
  first.role = hermes::ui::MessageRole::kAssistant;
  first.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kArtifact,
      .text = QStringLiteral("Report"),
      .detail = QStringLiteral("/tmp/report.html"),
  });
  hermes::ui::ChatMessage second;
  second.role = hermes::ui::MessageRole::kAssistant;
  second.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kImage,
      .text = QStringLiteral("Chart"),
      .detail = QStringLiteral("/tmp/chart.png"),
  });
  second.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kArtifact,
      .text = QStringLiteral("Report copy"),
      .detail = QStringLiteral("/tmp/report.html"),
  });
  snapshot.messages.push_back(first);
  snapshot.messages.push_back(second);
  rail.SetSnapshot(snapshot);
  QVERIFY(ExpandRailSection(rail, QStringLiteral("context")));
  QVERIFY(ExpandRailSection(rail, QStringLiteral("artifacts")));

  const auto* summary =
      rail.findChild<QLabel*>(QStringLiteral("rightRailContextSummary"));
  QVERIFY(summary != nullptr);
  QCOMPARE(summary->text(), QStringLiteral("12k / 200k tokens · 6%"));
  const auto* bar =
      rail.findChild<QProgressBar*>(QStringLiteral("rightRailContextBar"));
  QVERIFY(bar != nullptr);
  QCOMPARE(bar->value(), 6);
  const auto* detail =
      rail.findChild<QLabel*>(QStringLiteral("rightRailContextDetail"));
  QVERIFY(detail != nullptr);
  QVERIFY(detail->text().contains(QStringLiteral("2 messages")));
  QVERIFY(detail->text().contains(QStringLiteral("m1")));
  const auto* control =
      rail.findChild<QLabel*>(QStringLiteral("rightRailContextControl"));
  QVERIFY(control != nullptr);
  QCOMPARE(control->text(), QStringLiteral("Goal: active — Ship it"));
  QVERIFY(control->isVisible());
  const auto* notice =
      rail.findChild<QLabel*>(QStringLiteral("rightRailContextNotice"));
  QVERIFY(notice != nullptr);
  QCOMPARE(notice->text(), QStringLiteral("Warning · 1 credit left"));
  QVERIFY(notice->isVisible());

  const auto* agent =
      rail.findChild<QLabel*>(QStringLiteral("rightRailAgent_a"));
  QVERIFY(agent != nullptr);
  QVERIFY(agent->text().contains(QStringLiteral("Running")));
  QVERIFY(agent->text().contains(QStringLiteral("Audit pricing")));

  const auto artifacts = hermes::ui::CollectSessionArtifacts(snapshot.messages);
  QCOMPARE(static_cast<int>(artifacts.size()), 2);
  QCOMPARE(artifacts[0].kind, QStringLiteral("artifact"));
  QCOMPARE(artifacts[1].kind, QStringLiteral("image"));
  QCOMPARE(hermes::ui::CompactUsageTokens(2400000), QStringLiteral("2.4M"));

  auto* output =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0"));
  QVERIFY(output != nullptr);
  QString opened;
  rail.on_artifact_open = [&opened](const hermes::ui::SessionArtifact& value) {
    opened = value.value;
  };
  QTest::mouseClick(output, Qt::LeftButton);
  QCOMPARE(opened, QStringLiteral("/tmp/report.html"));
}

void PreferencesTest::rightRailPreviewsArtifactText() {
  QTemporaryDir home;
  QVERIFY(home.isValid());
  QFile file(home.filePath(QStringLiteral("report.txt")));
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("Quarterly pricing report\nAll good.");
  file.close();

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 640);
  rail.show();

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.home = home.path();
  snapshot.selected_session_id = QStringLiteral("s1");
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kArtifact,
      .text = QStringLiteral("Pricing report"),
      .detail = QStringLiteral("report.txt"),
  });
  snapshot.messages.push_back(message);
  rail.SetSnapshot(snapshot);

  auto* preview = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreview_0"));
  QVERIFY(preview != nullptr);
  preview->click();

  auto* body =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactPreviewBody"));
  QVERIFY(body != nullptr);
  QVERIFY(body->text().contains(QStringLiteral("Quarterly pricing report")));
  auto* title =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactPreviewTitle"));
  QVERIFY(title != nullptr);
  QCOMPARE(title->text(), QStringLiteral("Pricing report"));

  QString opened;
  rail.on_artifact_open = [&opened](const hermes::ui::SessionArtifact& value) {
    opened = value.value;
  };
  auto* open = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewOpen"));
  QVERIFY(open != nullptr);
  open->click();
  QCOMPARE(opened, QStringLiteral("report.txt"));
}

void PreferencesTest::rightRailKeepsArtifactScrollAcrossSnapshots() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 300);
  rail.show();

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.selected_session_id = QStringLiteral("s1");
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  for (int i = 0; i < 24; ++i) {
    message.blocks.push_back(hermes::ui::MessageBlock{
        .kind = hermes::ui::BlockKind::kArtifact,
        .text = QStringLiteral("Artifact %1").arg(i),
        .detail = QStringLiteral("/tmp/artifact-%1.txt").arg(i),
    });
  }
  snapshot.messages.push_back(message);
  rail.SetSnapshot(snapshot);

  QVERIFY(ExpandRailSection(rail, QStringLiteral("artifacts")));
  auto* pane =
      rail.findChild<QScrollArea*>(QStringLiteral("rightRailSectionsScroll"));
  QVERIFY(pane != nullptr);
  QCoreApplication::processEvents();
  auto* pane_bar = pane->verticalScrollBar();
  QVERIFY(pane_bar->maximum() > 0);
  pane_bar->setValue(pane_bar->maximum() / 2);
  const int kept_pane = pane_bar->value();
  QVERIFY(kept_pane > 0);

  // Force a real rebuild by changing the artifact set, then verify the list
  // scroll is restored after the deferred layout pass.
  snapshot.messages[0].blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kArtifact,
      .text = QStringLiteral("Artifact 24"),
      .detail = QStringLiteral("/tmp/artifact-24.txt"),
  });
  snapshot.revision = 2;
  rail.SetSnapshot(snapshot);
  QCoreApplication::processEvents();
  QTest::qWait(20);
  QCOMPARE(pane_bar->value(), kept_pane);

  // Periodic session polls resend the same transcript with a new revision.
  // They must neither rebuild the artifact tree nor move the reader.
  QPointer<QPushButton> tracked_row(
      rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0")));
  QVERIFY(tracked_row != nullptr);
  for (int i = 0; i < 5; ++i) {
    snapshot.revision = 3 + i;
    rail.SetSnapshot(snapshot);
    QCoreApplication::processEvents();
    QVERIFY(tracked_row != nullptr);
    QCOMPARE(pane_bar->value(), kept_pane);
  }

  // A selected HTML preview re-renders when the snapshot refreshes. Its
  // document scroll belongs to the reader too.
  QString long_html = QStringLiteral(
      "<!doctype html><html><head><title>Long Report</title></head><body>");
  for (int i = 0; i < 160; ++i) {
    long_html += QStringLiteral("<p>row %1</p>").arg(i);
  }
  long_html += QStringLiteral("</body></html>");
  hermes::ui::ProfileSnapshot html_snapshot = Snapshot();
  html_snapshot.selected_session_id = QStringLiteral("s2");
  hermes::ui::ChatMessage html_message;
  html_message.role = hermes::ui::MessageRole::kAssistant;
  html_message.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kCode,
      .text = long_html,
      .language = QStringLiteral("html"),
  });
  html_snapshot.messages.push_back(html_message);

  hermes::ui::RightRail html_rail(theme);
  html_rail.resize(286, 300);
  html_rail.show();
  html_rail.SetSnapshot(html_snapshot);
  QVERIFY(ExpandRailSection(html_rail, QStringLiteral("artifacts")));
  auto* artifact =
      html_rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0"));
  QVERIFY(artifact != nullptr);
  artifact->click();
  auto* document = html_rail.findChild<QTextBrowser*>(
      QStringLiteral("rightRailArtifactPreviewDocument"));
  QVERIFY(document != nullptr);
  QVERIFY(document->isVisible());
  QVERIFY(document->verticalScrollBar()->maximum() > 0);
  document->verticalScrollBar()->setValue(
      document->verticalScrollBar()->maximum() / 2);
  const int kept_document = document->verticalScrollBar()->value();
  QVERIFY(kept_document > 0);

  // Force the HTML artifact list to change so the preview is re-rendered.
  html_snapshot.messages[0].blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kArtifact,
      .text = QStringLiteral("Second file"),
      .detail = QStringLiteral("/tmp/second.txt"),
  });
  html_snapshot.revision = 2;
  html_rail.SetSnapshot(html_snapshot);
  QCoreApplication::processEvents();
  QTest::qWait(20);
  QCOMPARE(document->verticalScrollBar()->value(), kept_document);

  QPointer<QPushButton> tracked_html_row(
      html_rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0")));
  QVERIFY(tracked_html_row != nullptr);
  for (int i = 0; i < 5; ++i) {
    html_snapshot.revision = 3 + i;
    html_rail.SetSnapshot(html_snapshot);
    QCoreApplication::processEvents();
    QVERIFY(tracked_html_row != nullptr);
    QCOMPARE(document->verticalScrollBar()->value(), kept_document);
  }
}

void PreferencesTest::rightRailProjectGitTerminalPanes() {
  if (QStandardPaths::findExecutable(QStringLiteral("git")).isEmpty()) {
    QSKIP("git is not installed");
  }
  QTemporaryDir project;
  QVERIFY(project.isValid());
  const auto write_file = [&project](const QString& name,
                                     const QString& contents) {
    QFile file(QDir(project.path()).filePath(name));
    if (!file.open(QIODevice::WriteOnly)) {
      return false;
    }
    return file.write(contents.toUtf8()) >= 0;
  };
  QVERIFY(write_file(QStringLiteral("hello.txt"),
                     QStringLiteral("hello from project")));
  QVERIFY(write_file(QStringLiteral("README.md"),
                     QStringLiteral("# Project Atlas\n\n| Area | State |\n"
                                    "| --- | --- |\n| Files | Ready |\n")));
  QVERIFY(write_file(QStringLiteral("page.html"),
                     QStringLiteral("<!doctype html><html><body>"
                                    "<h1>Rendered page</h1></body></html>")));
  QVERIFY(write_file(QStringLiteral("main.py"),
                     QStringLiteral("print('project preview')\n")));
  QImage pixel(8, 8, QImage::Format_ARGB32);
  pixel.fill(QColor(QStringLiteral("#4c8dff")));
  QVERIFY(
      pixel.save(QDir(project.path()).filePath(QStringLiteral("pixel.png"))));
  const auto run_git = [&project](const QStringList& arguments) {
    QProcess process;
    process.setWorkingDirectory(project.path());
    process.start(QStringLiteral("git"), arguments);
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
      qWarning() << process.readAllStandardError();
      return false;
    }
    return true;
  };
  QVERIFY(run_git({QStringLiteral("init")}));
  QVERIFY(run_git({QStringLiteral("config"), QStringLiteral("user.email"),
                   QStringLiteral("test@example.com")}));
  QVERIFY(run_git({QStringLiteral("config"), QStringLiteral("user.name"),
                   QStringLiteral("Hermes Test")}));
  QVERIFY(run_git({QStringLiteral("add"), QStringLiteral("hello.txt")}));
  QVERIFY(run_git({QStringLiteral("commit"), QStringLiteral("-m"),
                   QStringLiteral("init")}));

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(360, 700);
  rail.show();
  rail.SetProjectContext(QStringLiteral("p1"), QStringLiteral("Demo"),
                         project.path());
  QVERIFY(ExpandRailSection(rail, QStringLiteral("project")));
  QVERIFY(ExpandRailSection(rail, QStringLiteral("git")));
  QVERIFY(ExpandRailSection(rail, QStringLiteral("terminal")));

  auto* workspace =
      rail.findChild<QWidget*>(QStringLiteral("rightRailWorkspace"));
  auto* tree =
      rail.findChild<QTreeView*>(QStringLiteral("rightRailProjectTree"));
  auto* model = rail.findChild<QFileSystemModel*>();
  auto* preview =
      rail.findChild<QTextBrowser*>(QStringLiteral("rightRailProjectPreview"));
  QVERIFY(workspace != nullptr);
  QVERIFY(tree != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(preview != nullptr);
  const QModelIndex file_index =
      model->index(QDir(project.path()).filePath(QStringLiteral("hello.txt")));
  QVERIFY(file_index.isValid());
  QTRY_VERIFY(tree->rootIndex().isValid());
  QTRY_VERIFY(model->rowCount(tree->rootIndex()) >= 1);
  QTRY_VERIFY(tree->visualRect(file_index).isValid());
  QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                    tree->visualRect(file_index).center());
  QTRY_VERIFY(
      preview->toPlainText().contains(QStringLiteral("hello from project")));

  const auto preview_file = [&](const QString& name) {
    const QModelIndex index = model->index(QDir(project.path()).filePath(name));
    if (!index.isValid()) {
      return false;
    }
    tree->scrollTo(index);
    QCoreApplication::processEvents();
    QRect rect = tree->visualRect(index);
    for (int attempt = 0; attempt < 100 && !rect.isValid(); ++attempt) {
      QTest::qWait(10);
      rect = tree->visualRect(index);
    }
    if (!rect.isValid()) {
      return false;
    }
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      rect.center());
    return true;
  };
  QVERIFY(preview_file(QStringLiteral("README.md")));
  QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("Project Atlas")));
  QCOMPARE(preview->document()->defaultFont().pixelSize(), 13);
  QVERIFY(preview->toHtml().contains(QStringLiteral("<table")));
  QVERIFY(preview_file(QStringLiteral("page.html")));
  QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("Rendered page")));
  QVERIFY(preview_file(QStringLiteral("pixel.png")));
  QTRY_VERIFY(preview->document()->toHtml().contains(QStringLiteral("<img")));
  QVERIFY(preview_file(QStringLiteral("main.py")));
  QTRY_VERIFY(
      preview->toPlainText().contains(QStringLiteral("project preview")));
  QCOMPARE(preview->document()->defaultFont().pixelSize(), 12);
  QCOMPARE(preview->document()->defaultFont().family(),
           theme.MonoFont(12, QFont::Normal).family());

  auto* git_pane = rail.findChild<QWidget*>(QStringLiteral("rightRailGitPane"));
  auto* git_status =
      rail.findChild<QPlainTextEdit*>(QStringLiteral("rightRailGitStatus"));
  auto* git_message =
      rail.findChild<QPlainTextEdit*>(QStringLiteral("rightRailGitMessage"));
  auto* git_message_status =
      rail.findChild<QLabel*>(QStringLiteral("rightRailGitMessageStatus"));
  auto* git_refresh =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailGitRefresh"));
  auto* git_commit =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailGitCommit"));
  auto* git_push =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailGitPush"));
  QVERIFY(git_pane != nullptr);
  QVERIFY(git_status != nullptr);
  QVERIFY(git_message != nullptr);
  QVERIFY(git_message_status != nullptr);
  QVERIFY(git_refresh != nullptr);
  QVERIFY(git_commit != nullptr);
  QVERIFY(git_push != nullptr);
  QCOMPARE(git_push->text(), QStringLiteral("Commit & Push"));
  QVERIFY(git_refresh->property("railAction").toBool());
  const hermes::ui::Theme dark = hermes::ui::Theme::Dark();
  rail.SetTheme(dark);
  QVERIFY(rail.styleSheet().contains(dark.surface.name()));
  QVERIFY(rail.styleSheet().contains(dark.code_background.name()));
  QCOMPARE(rail.findChild<QLabel*>(QStringLiteral("rightRailTitle"))
               ->font()
               .pixelSize(),
           dark.UiFont(12, QFont::DemiBold).pixelSize());
  QCOMPARE(rail.findChild<QPushButton*>(QStringLiteral("rightRailSection_git"))
               ->font()
               .pixelSize(),
           dark.UiFont(11, QFont::DemiBold).pixelSize());
  QVERIFY(
      git_message_status->styleSheet().contains(dark.text_secondary.name()));
  // Switching themes re-renders the open Markdown document instead of leaving
  // the old theme's table fills and body font in place.
  QVERIFY(preview_file(QStringLiteral("README.md")));
  QTRY_VERIFY(preview->toPlainText().contains(QStringLiteral("Project Atlas")));
  QCOMPARE(preview->document()->defaultFont().pixelSize(),
           dark.UiFont(13, QFont::Normal).pixelSize());
  rail.SetTheme(theme);
  QCOMPARE(preview->document()->defaultFont().pixelSize(),
           theme.UiFont(13, QFont::Normal).pixelSize());
  QVERIFY(git_pane->isVisible());
  QTRY_VERIFY(git_status->toPlainText().contains(QStringLiteral("##")));
  QVERIFY(git_push->isEnabled());

  QVERIFY(write_file(QStringLiteral("second.txt"),
                     QStringLiteral("second change")));
  git_refresh->click();
  QTRY_VERIFY(git_status->toPlainText().contains(QStringLiteral("second.txt")));
  git_message->setPlainText(QStringLiteral("add second"));
  git_commit->click();
  QTRY_VERIFY(git_message->toPlainText().isEmpty());
  QTRY_COMPARE(git_message_status->text(), QStringLiteral("Committed."));
  QTRY_VERIFY(
      !git_status->toPlainText().contains(QStringLiteral("second.txt")));

  auto* terminal_pane =
      rail.findChild<QWidget*>(QStringLiteral("rightRailTerminalPane"));
  auto* terminal = rail.findChild<hermes::ui::TerminalWidget*>(
      QStringLiteral("rightRailTerminalOutput"));
  QVERIFY(terminal_pane != nullptr);
  QVERIFY(terminal != nullptr);
  QVERIFY(terminal_pane->isVisible());
  auto* terminal_copy =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailTerminalCopy"));
  auto* terminal_paste =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailTerminalPaste"));
  QVERIFY(terminal_copy != nullptr);
  QVERIFY(terminal_paste != nullptr);
  QTRY_VERIFY_WITH_TIMEOUT(terminal->IsRunning(), 3000);
  terminal->RunCommandForTest(
      QStringLiteral("printf '__hermes_terminal_ok__\\n'"));
  QTRY_VERIFY_WITH_TIMEOUT(terminal->PlainTextForTest().contains(
                               QStringLiteral("__hermes_terminal_ok__")),
                           3000);
  terminal->SelectAll();
  QGuiApplication::clipboard()->clear();
  terminal_copy->click();
  QVERIFY(QGuiApplication::clipboard()->text().contains(
      QStringLiteral("__hermes_terminal_ok__")));
  QGuiApplication::clipboard()->setText(
      QStringLiteral("printf '__hermes_terminal_paste__\\n'\r"));
  terminal_paste->click();
  QTRY_VERIFY_WITH_TIMEOUT(terminal->PlainTextForTest().contains(
                               QStringLiteral("__hermes_terminal_paste__")),
                           3000);

  auto* git_generate =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailGitGenerate"));
  QVERIFY(git_generate != nullptr);
  QString generated_diff;
  QString generated_recent;
  rail.on_generate_commit_message =
      [&](const QString& diff, const QString& recent_commits, const QString&,
          std::function<void(const QString&, const QString&)> done) {
        generated_diff = diff;
        generated_recent = recent_commits;
        done(QStringLiteral("feat: AI generated subject"), QString());
      };
  QVERIFY(write_file(QStringLiteral("ai.txt"),
                     QStringLiteral("ai-assisted change")));
  git_refresh->click();
  QTRY_VERIFY(git_status->toPlainText().contains(QStringLiteral("ai.txt")));
  QTRY_VERIFY(git_generate->isEnabled());
  git_generate->click();
  QTRY_VERIFY(generated_diff.contains(QStringLiteral("ai.txt")));
  QTRY_VERIFY(generated_recent.contains(QStringLiteral("add second")));
  QTRY_COMPARE(git_message->toPlainText(),
               QStringLiteral("feat: AI generated subject"));
  QTRY_COMPARE(git_message_status->text(),
               QStringLiteral("Generated · review and commit."));

  auto* collapse =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailCollapse"));
  auto* collapsed_bar =
      rail.findChild<QWidget*>(QStringLiteral("rightRailCollapsedBar"));
  QVERIFY(collapse != nullptr);
  QVERIFY(collapsed_bar != nullptr);
  collapse->click();
  QVERIFY(rail.IsCollapsed());
  QTRY_COMPARE(rail.width(), 44);
  QVERIFY(workspace->isHidden());
  QVERIFY(collapsed_bar->isVisible());
  auto* collapsed_git =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailCollapsedTab1"));
  QVERIFY(collapsed_git != nullptr);
  collapsed_git->click();
  QVERIFY(!rail.IsCollapsed());
  QTRY_VERIFY(workspace->isVisible());
  QVERIFY(git_pane->isVisible());
  QCOMPARE(rail.findChild<QPushButton*>(QStringLiteral("rightRailSection_git"))
               ->property("expanded")
               .toBool(),
           true);
}

void PreferencesTest::flowLayoutWrapsRowsAtNarrowWidths() {
  QWidget host;
  auto* layout = new hermes::ui::FlowLayout(&host);
  auto* first = new QPushButton(QStringLiteral("One"), &host);
  auto* second = new QPushButton(QStringLiteral("Two"), &host);
  auto* third = new QPushButton(QStringLiteral("Three"), &host);
  first->setFixedSize(100, 24);
  second->setFixedSize(100, 24);
  third->setFixedSize(100, 24);
  layout->addWidget(first);
  layout->addWidget(second);
  layout->addWidget(third);
  host.resize(220, 200);
  host.show();
  QCoreApplication::processEvents();
  QCOMPARE(layout->heightForWidth(500), 24);
  QCOMPARE(layout->heightForWidth(220), 52);
  QCOMPARE(first->geometry().top(), 0);
  QCOMPARE(second->geometry().top(), 0);
  QCOMPARE(third->geometry().top(), 28);
  QCOMPARE(third->geometry().left(), 0);
}

void PreferencesTest::artifactDetectionAndNamingMatchDesktop() {
  using hermes::ui::ArtifactContentHash;
  using hermes::ui::ArtifactDetection;
  using hermes::ui::ArtifactDownloadName;
  using hermes::ui::ArtifactKind;
  using hermes::ui::ArtifactSlug;
  using hermes::ui::DetectArtifact;

  const QString html_doc =
      QStringLiteral(
          "<!doctype html><html><head><title>Budget Dashboard</title></head>"
          "<body><h1>Quarterly plan</h1><p>") +
      QString(180, QLatin1Char('x')) + QStringLiteral("</p></body></html>");
  const auto html = DetectArtifact(QStringLiteral("html"), html_doc);
  QVERIFY(html.has_value());
  QCOMPARE(html->kind, ArtifactKind::kHtml);
  QCOMPARE(html->title, QStringLiteral("Budget Dashboard"));
  QVERIFY(!DetectArtifact(QStringLiteral("html"),
                          QStringLiteral("<div class=\"chip\">hello</div>"))
               .has_value());
  const QString h1_doc = QStringLiteral(
                             "<!doctype html><html><body><h1>Budget "
                             "<em>Dashboard</em></h1>") +
                         QStringLiteral("<div>x</div>").repeated(30) +
                         QStringLiteral("</body></html>");
  QCOMPARE(DetectArtifact(QStringLiteral("html"), h1_doc)->title,
           QStringLiteral("Budget Dashboard"));
  const QString fragment = QStringLiteral("<section><p>") +
                           QString(1250, QLatin1Char('y')) +
                           QStringLiteral("</p></section>");
  QVERIFY(DetectArtifact(QStringLiteral("html"), fragment).has_value());

  QString svg =
      QStringLiteral("<svg viewBox=\"0 0 10 10\"><title>Org Chart</title>");
  svg += QString(2100, QLatin1Char(' '));
  svg += QStringLiteral("<rect width=\"10\" height=\"10\"/></svg>");
  const auto svg_detection = DetectArtifact(QStringLiteral("svg"), svg);
  QVERIFY(svg_detection.has_value());
  QCOMPARE(svg_detection->kind, ArtifactKind::kSvg);
  QCOMPARE(svg_detection->title, QStringLiteral("Org Chart"));
  QVERIFY(!DetectArtifact(
               QStringLiteral("svg"),
               QStringLiteral("<svg><rect width=\"10\" height=\"10\"/></svg>"))
               .has_value());

  QVERIFY(
      !DetectArtifact(QStringLiteral("python"), QStringLiteral("print(\"hi\")"))
           .has_value());
  QString long_code = QStringLiteral("// dashboard.py\n");
  for (int line = 0; line < 52; ++line) {
    long_code += QStringLiteral("print('line %1')\n").arg(line);
  }
  const auto code = DetectArtifact(QStringLiteral("python"), long_code);
  QVERIFY(code.has_value());
  QCOMPARE(code->kind, ArtifactKind::kCode);
  QCOMPARE(code->title, QStringLiteral("dashboard.py"));
  QVERIFY(!DetectArtifact(QStringLiteral("text"), long_code).has_value());
  QVERIFY(!DetectArtifact(QStringLiteral("markdown"), long_code).has_value());
  QVERIFY(!DetectArtifact(QStringLiteral("diff"), long_code).has_value());
  QVERIFY(!DetectArtifact(QStringLiteral("mermaid"), long_code).has_value());
  QVERIFY(!DetectArtifact(QStringLiteral("listing"), long_code).has_value());

  QString declaration_code;
  for (int line = 0; line < 52; ++line) {
    declaration_code += QStringLiteral("const row%1 = %1;\n").arg(line);
  }
  declaration_code =
      QStringLiteral("export function buildDashboard() {\n") + declaration_code;
  const auto declaration =
      DetectArtifact(QStringLiteral("typescript"), declaration_code);
  QVERIFY(declaration.has_value());
  QCOMPARE(declaration->title, QStringLiteral("buildDashboard"));

  QCOMPARE(
      ArtifactSlug(ArtifactDetection{ArtifactKind::kCode, QStringLiteral("ts"),
                                     QStringLiteral("!!!")}),
      QStringLiteral("code:ts:untitled"));
  QCOMPARE(ArtifactContentHash(QStringLiteral("abc")),
           ArtifactContentHash(QStringLiteral("abc")));
  QVERIFY(ArtifactContentHash(QStringLiteral("abc")) !=
          ArtifactContentHash(QStringLiteral("abd")));
  QCOMPARE(ArtifactDownloadName(ArtifactKind::kCode, QStringLiteral("python"),
                                QStringLiteral("server.py")),
           QStringLiteral("server.py"));
  QCOMPARE(ArtifactDownloadName(ArtifactKind::kHtml, QStringLiteral("html"),
                                QStringLiteral("Pomodoro Timer")),
           QStringLiteral("Pomodoro-Timer.html"));
  QCOMPARE(ArtifactDownloadName(ArtifactKind::kSvg, QStringLiteral("svg"),
                                QStringLiteral("Org Chart")),
           QStringLiteral("Org-Chart.svg"));
  QCOMPARE(
      ArtifactDownloadName(ArtifactKind::kCode, QStringLiteral("typescript"),
                           QStringLiteral("buildDashboard")),
      QStringLiteral("buildDashboard.ts"));
  QCOMPARE(ArtifactDownloadName(ArtifactKind::kCode,
                                QStringLiteral("unknownlang"), QString()),
           QStringLiteral("artifact.txt"));
}

void PreferencesTest::rightRailBrowsesGeneratedArtifactVersions() {
  const auto version = [](const QString& marker) {
    QString code = QStringLiteral("// dashboard.py\n");
    for (int line = 0; line < 52; ++line) {
      code += QStringLiteral("print('line %1')\n").arg(line);
    }
    code += marker + QStringLiteral("\n");
    return code;
  };

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.selected_session_id = QStringLiteral("s1");
  const QStringList markers = {QStringLiteral("first version"),
                               QStringLiteral("second version"),
                               QStringLiteral("third version")};
  for (int index = 0; index < markers.size(); ++index) {
    hermes::ui::ChatMessage message;
    message.role = hermes::ui::MessageRole::kAssistant;
    message.timestamp = QDateTime::currentDateTime().addSecs(index);
    message.blocks.push_back(hermes::ui::MessageBlock{
        .kind = hermes::ui::BlockKind::kCode,
        .text = version(markers[index]),
        .language = QStringLiteral("python"),
    });
    snapshot.messages.push_back(message);
  }
  hermes::ui::ChatMessage duplicate;
  duplicate.role = hermes::ui::MessageRole::kAssistant;
  duplicate.timestamp = QDateTime::currentDateTime().addSecs(10);
  duplicate.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kCode,
      .text = version(QStringLiteral("third version")),
      .language = QStringLiteral("python"),
  });
  snapshot.messages.push_back(duplicate);

  const auto records = hermes::ui::CollectGeneratedArtifacts(
      snapshot.messages, QStringLiteral("s1"));
  QCOMPARE(static_cast<int>(records.size()), 1);
  QCOMPARE(records[0].title, QStringLiteral("dashboard.py"));
  QCOMPARE(static_cast<int>(records[0].versions.size()), 3);
  QVERIFY(records[0].versions.back().content.contains(QStringLiteral("third")));

  const auto artifacts = hermes::ui::CollectSessionArtifacts(snapshot.messages);
  QCOMPARE(static_cast<int>(artifacts.size()), 1);
  QVERIFY(artifacts[0].generated);
  QCOMPARE(artifacts[0].version_index, 2);

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 640);
  rail.show();
  rail.SetSnapshot(snapshot);
  QVERIFY(ExpandRailSection(rail, QStringLiteral("artifacts")));

  QString opened_value;
  rail.on_artifact_open =
      [&opened_value](const hermes::ui::SessionArtifact& artifact) {
        opened_value = artifact.value;
      };
  hermes::ui::SessionArtifact saved;
  QString saved_name;
  rail.on_artifact_save = [&saved, &saved_name](
                              const hermes::ui::SessionArtifact& artifact,
                              const QString& name) {
    saved = artifact;
    saved_name = name;
  };

  auto* card =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0"));
  QVERIFY(card != nullptr);
  QVERIFY(card->text().contains(QStringLiteral("dashboard.py")));
  QVERIFY(card->text().contains(QStringLiteral("3 version(s)")));
  card->click();
  QVERIFY(opened_value.isEmpty());

  auto* title =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactPreviewTitle"));
  QVERIFY(title != nullptr);
  QCOMPARE(title->text(), QStringLiteral("dashboard.py"));
  auto* body =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactPreviewBody"));
  QVERIFY(body != nullptr);
  QVERIFY(body->isVisible());
  QVERIFY(body->text().contains(QStringLiteral("third version")));
  auto* version_label =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactVersionLabel"));
  QVERIFY(version_label != nullptr);
  QCOMPARE(version_label->text(), QStringLiteral("v3 of 3"));

  auto* previous = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactVersionPrev"));
  auto* next = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactVersionNext"));
  QVERIFY(previous != nullptr);
  QVERIFY(next != nullptr);
  QVERIFY(previous->isEnabled());
  QVERIFY(!next->isEnabled());
  previous->click();
  QCOMPARE(version_label->text(), QStringLiteral("v2 of 3"));
  QVERIFY(body->text().contains(QStringLiteral("second version")));
  next->click();
  QCOMPARE(version_label->text(), QStringLiteral("v3 of 3"));
  QVERIFY(body->text().contains(QStringLiteral("third version")));

  auto* copy = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewCopy"));
  QVERIFY(copy != nullptr);
  copy->click();
  QVERIFY(QGuiApplication::clipboard()->text().contains(
      QStringLiteral("third version")));

  auto* save = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewSave"));
  QVERIFY(save != nullptr);
  save->click();
  QCOMPARE(saved_name, QStringLiteral("dashboard.py"));
  QCOMPARE(static_cast<int>(saved.versions.size()), 3);
  QCOMPARE(saved.version_index, 2);
}

void PreferencesTest::rightRailRendersHtmlArtifactAndSource() {
  const QString html = QStringLiteral(
                           "<!doctype html><html><head><title>Pomodoro "
                           "Timer</title></head><body><h1>Pomodoro</h1><p>") +
                       QString(180, QLatin1Char('x')) +
                       QStringLiteral("</p></body></html>");

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.selected_session_id = QStringLiteral("s1");
  hermes::ui::ChatMessage message;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.timestamp = QDateTime::currentDateTime();
  message.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kCode,
      .text = html,
      .language = QStringLiteral("html"),
  });
  snapshot.messages.push_back(message);

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 640);
  rail.show();
  rail.SetSnapshot(snapshot);
  QVERIFY(ExpandRailSection(rail, QStringLiteral("artifacts")));

  auto* card =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0"));
  QVERIFY(card != nullptr);
  QVERIFY(card->text().contains(QStringLiteral("Pomodoro Timer")));
  card->click();

  auto* document = rail.findChild<QTextBrowser*>(
      QStringLiteral("rightRailArtifactPreviewDocument"));
  auto* body =
      rail.findChild<QLabel*>(QStringLiteral("rightRailArtifactPreviewBody"));
  auto* source = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewSource"));
  auto* copy = rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewCopy"));
  QVERIFY(document != nullptr);
  QVERIFY(body != nullptr);
  QVERIFY(source != nullptr);
  QVERIFY(copy != nullptr);
  QVERIFY(document->isVisible());
  QVERIFY(document->toPlainText().contains(QStringLiteral("Pomodoro")));
  QVERIFY(!body->isVisible());
  QCOMPARE(source->text(), QStringLiteral("Source"));

  source->click();
  QCOMPARE(source->text(), QStringLiteral("Rendered"));
  QVERIFY(body->isVisible());
  QVERIFY(body->text().contains(QStringLiteral("<title>")));
  QVERIFY(!document->isVisible());

  source->click();
  QVERIFY(document->isVisible());
  QVERIFY(!body->isVisible());
  copy->click();
  QCOMPARE(QGuiApplication::clipboard()->text(), html);

  const QString svg =
      QStringLiteral(
          "<svg xmlns=\"http://www.w3.org/2000/svg\" "
          "viewBox=\"0 0 100 100\"><title>Org Chart</title>") +
      QStringLiteral("<rect x=\"1\" y=\"2\" width=\"3\" height=\"4\"/>")
          .repeated(80) +
      QStringLiteral("</svg>");
  hermes::ui::ProfileSnapshot svg_snapshot = Snapshot();
  svg_snapshot.selected_session_id = QStringLiteral("s2");
  hermes::ui::ChatMessage svg_message;
  svg_message.role = hermes::ui::MessageRole::kAssistant;
  svg_message.timestamp = QDateTime::currentDateTime();
  svg_message.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kCode,
      .text = svg,
      .language = QStringLiteral("svg"),
  });
  svg_snapshot.messages.push_back(svg_message);
  hermes::ui::RightRail svg_rail(theme);
  svg_rail.resize(286, 640);
  svg_rail.show();
  svg_rail.SetSnapshot(svg_snapshot);
  QVERIFY(ExpandRailSection(svg_rail, QStringLiteral("artifacts")));
  card =
      svg_rail.findChild<QPushButton*>(QStringLiteral("rightRailArtifact_0"));
  QVERIFY(card != nullptr);
  card->click();
  auto* svg_widget = svg_rail.findChild<QSvgWidget*>(
      QStringLiteral("rightRailArtifactPreviewSvg"));
  QVERIFY(svg_widget != nullptr);
  QSvgRenderer direct_svg(svg.toUtf8());
  QVERIFY(direct_svg.isValid());
  QVERIFY(svg_widget->renderer() != nullptr);
  QVERIFY(svg_widget->renderer()->isValid());
  QVERIFY(svg_widget->isVisible());
  auto* svg_source = svg_rail.findChild<QPushButton*>(
      QStringLiteral("rightRailArtifactPreviewSource"));
  auto* svg_body = svg_rail.findChild<QLabel*>(
      QStringLiteral("rightRailArtifactPreviewBody"));
  QVERIFY(svg_source != nullptr);
  QVERIFY(svg_body != nullptr);
  svg_source->click();
  QVERIFY(svg_body->isVisible());
  QVERIFY(svg_body->text().contains(QStringLiteral("<svg")));
  QVERIFY(!svg_widget->isVisible());
}

void PreferencesTest::rightRailShowsTodoTimelineAndFiltersPrompts() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::RightRail rail(theme);
  rail.resize(286, 640);
  rail.show();

  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.selected_session_id = QStringLiteral("s1");
  snapshot.todo_revision = 3;
  snapshot.todos = {
      hermes::ui::TodoItem{.id = QStringLiteral("1"),
                           .content = QStringLiteral("Audit pricing"),
                           .status = QStringLiteral("in_progress")},
      hermes::ui::TodoItem{.id = QStringLiteral("2"),
                           .content = QStringLiteral("Write report"),
                           .status = QStringLiteral("completed")},
  };
  hermes::ui::ChatMessage first;
  first.role = hermes::ui::MessageRole::kUser;
  first.row_id = 11;
  first.timestamp = QDateTime::currentDateTime();
  first.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kParagraph,
      .text = QStringLiteral("First prompt about pricing"),
  });
  hermes::ui::ChatMessage reply;
  reply.role = hermes::ui::MessageRole::kAssistant;
  reply.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kParagraph,
      .text = QStringLiteral("Assistant reply"),
  });
  const QString second_prompt =
      QStringLiteral("Second prompt about reports with a long tail ") +
      QString(140, QLatin1Char('r'));
  hermes::ui::ChatMessage second;
  second.role = hermes::ui::MessageRole::kUser;
  second.row_id = 22;
  second.timestamp = QDateTime::currentDateTime().addSecs(60);
  second.blocks.push_back(hermes::ui::MessageBlock{
      .kind = hermes::ui::BlockKind::kParagraph,
      .text = second_prompt,
  });
  snapshot.messages = {first, reply, second};
  rail.SetSnapshot(snapshot);
  QVERIFY(ExpandRailSection(rail, QStringLiteral("todos")));
  QVERIFY(ExpandRailSection(rail, QStringLiteral("timeline")));

  const auto* summary =
      rail.findChild<QLabel*>(QStringLiteral("rightRailTodoSummary"));
  QVERIFY(summary != nullptr);
  QCOMPARE(summary->text(), QStringLiteral("1 active · 1 done"));
  const auto* todo = rail.findChild<QLabel*>(QStringLiteral("rightRailTodo_1"));
  QVERIFY(todo != nullptr);
  QVERIFY(todo->text().contains(QStringLiteral("[>] Audit pricing")));
  QVERIFY(!rail.findChild<QLabel*>(QStringLiteral("rightRailTodosEmpty"))
               ->isVisible());

  const auto entries = hermes::ui::CollectTimelineEntries(snapshot.messages);
  QCOMPARE(static_cast<int>(entries.size()), 2);
  QCOMPARE(entries[0].row_id, qint64{11});
  QVERIFY(entries[1].preview.startsWith(
      QStringLiteral("Second prompt about reports")));
  auto* second_button =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailTimeline_1"));
  QVERIFY(second_button != nullptr);
  auto* second_preview =
      rail.findChild<QLabel*>(QStringLiteral("rightRailTimelinePreview_1"));
  auto* second_time =
      rail.findChild<QLabel*>(QStringLiteral("rightRailTimelineTime_1"));
  QVERIFY(second_preview != nullptr);
  QVERIFY(second_time != nullptr);
  QVERIFY(second_preview->wordWrap());
  QVERIFY(second_preview->text().startsWith(
      QStringLiteral("Second prompt about reports")));
  QVERIFY(second_button->toolTip().contains(QStringLiteral("width:360px")));
  QVERIFY(second_button->toolTip().contains(
      QStringLiteral("Second prompt about reports")));
  QVERIFY(second_button->toolTip().contains(QStringLiteral("rrrr")));
  QTextDocument tip_document;
  tip_document.setHtml(second_button->toolTip());
  tip_document.setTextWidth(360);
  const QFontMetrics tip_metrics(tip_document.defaultFont());
  QVERIFY(tip_document.size().height() > tip_metrics.lineSpacing() * 2);
  QVERIFY(second_button->focusPolicy() == Qt::StrongFocus);
  QVERIFY(rail.styleSheet().contains(
      QStringLiteral("railTimelineRow=\"true\"]:focus")));
  QVERIFY(rail.styleSheet().contains(
      QStringLiteral("railTimelineRow=\"true\"]:pressed")));
  rail.layout()->activate();
  QCoreApplication::processEvents();
  QVERIFY(second_preview->width() > 0);
  QVERIFY(second_preview->width() <= second_button->width());
  QVERIFY(second_preview->height() >=
          second_preview->fontMetrics().lineSpacing() * 2);
  qint64 opened = 0;
  rail.on_timeline_open = [&opened](qint64 row_id) { opened = row_id; };
  QTest::mouseClick(second_button, Qt::LeftButton);
  QCOMPARE(opened, qint64{22});

  auto* search =
      rail.findChild<QLineEdit*>(QStringLiteral("rightRailTimelineSearch"));
  QVERIFY(search != nullptr);
  search->setText(QStringLiteral("second"));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
  QVERIFY(rail.findChild<QPushButton*>(QStringLiteral("rightRailTimeline_1")) ==
          nullptr);
  auto* filtered =
      rail.findChild<QPushButton*>(QStringLiteral("rightRailTimeline_0"));
  QVERIFY(filtered != nullptr);
  auto* filtered_preview =
      rail.findChild<QLabel*>(QStringLiteral("rightRailTimelinePreview_0"));
  QVERIFY(filtered_preview != nullptr);
  QVERIFY(filtered_preview->text().contains(QStringLiteral("Second prompt")));
  search->clear();
  QTest::keyClicks(search, QStringLiteral("Second"));
  QTRY_VERIFY(search->completer() != nullptr);
  QTRY_VERIFY(search->completer()->model()->rowCount() > 0);
  bool has_second = false;
  for (int row = 0; row < search->completer()->model()->rowCount(); ++row) {
    if (search->completer()->model()->index(row, 0).data().toString().contains(
            QStringLiteral("Second prompt"))) {
      has_second = true;
      break;
    }
  }
  QVERIFY(has_second);

  QJsonObject payload;
  payload.insert(QStringLiteral("revision"), 4);
  payload.insert(
      QStringLiteral("todos"),
      QJsonArray{QJsonObject{
          {QStringLiteral("id"), QStringLiteral("a")},
          {QStringLiteral("content"), QStringLiteral("Nested child")},
          {QStringLiteral("status"), QStringLiteral("pending")},
          {QStringLiteral("parent"), QStringLiteral("1")}}});
  std::vector<hermes::ui::TodoItem> parsed;
  int revision = 0;
  QVERIFY(hermes::ui::ParseTodoState(payload, &parsed, &revision));
  QCOMPARE(revision, 4);
  QCOMPARE(static_cast<int>(parsed.size()), 1);
  QCOMPARE(parsed[0].parent, QStringLiteral("1"));
  QCOMPARE(hermes::ui::TodoStatusMarker(parsed[0].status),
           QStringLiteral("[ ]"));
}

void PreferencesTest::sessionSearchBarFindsAndOpensSessions() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SessionSearchBar bar(theme);
  bar.resize(640, 260);
  bar.show();

  QString seen_query;
  bar.SetSearchHandler(
      [&seen_query](
          const QString& query,
          std::function<void(const QJsonObject&, const QString&)> done) {
        seen_query = query;
        done(QJsonObject{{QStringLiteral("results"),
                          QJsonArray{QJsonObject{
                              {QStringLiteral("session_id"),
                               QStringLiteral("session-42")},
                              {QStringLiteral("title"),
                               QStringLiteral("Pricing investigation")},
                              {QStringLiteral("snippet"),
                               QStringLiteral("We audited the pricing table")},
                              {QStringLiteral("message_count"), 7}}}}},
             QString());
      });
  QString opened;
  bar.on_session_open = [&opened](const QString& session_id) {
    opened = session_id;
  };
  bar.SetSuggestionsProvider([]() {
    return QStringList{QStringLiteral("Pricing investigation"),
                       QStringLiteral("Project Atlas")};
  });

  auto* input = bar.findChild<QLineEdit*>(QStringLiteral("sessionSearchInput"));
  auto* results =
      bar.findChild<QListWidget*>(QStringLiteral("sessionSearchResults"));
  auto* status = bar.findChild<QLabel*>(QStringLiteral("sessionSearchStatus"));
  QVERIFY(input != nullptr);
  QVERIFY(results != nullptr);
  QVERIFY(status != nullptr);

  input->setText(QStringLiteral("pricing"));
  bar.TriggerSearchForTest();
  QCOMPARE(seen_query, QStringLiteral("pricing"));
  QCOMPARE(results->count(), 1);
  QVERIFY(results->isVisible());
  QVERIFY(results->item(0)->text().contains(
      QStringLiteral("Pricing investigation")));
  QVERIFY(status->text().contains(QStringLiteral("1 result")));
  const hermes::ui::Theme dark = hermes::ui::Theme::Dark();
  bar.SetTheme(dark);
  QVERIFY(bar.styleSheet().contains(dark.surface.name()));
  QVERIFY(bar.styleSheet().contains(dark.foreground.name()));
  QCOMPARE(input->font().pixelSize(),
           dark.UiFont(13, QFont::Normal).pixelSize());
  QCOMPARE(results->font().pixelSize(),
           dark.UiFont(12, QFont::Normal).pixelSize());
  QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                    results->visualItemRect(results->item(0)).center());
  QCOMPARE(opened, QStringLiteral("session-42"));

  // Suggestions are recomputed as the reader types rather than frozen at
  // construction. Sessions and project names both feed the same popup.
  input->clear();
  QTest::keyClicks(input, QStringLiteral("pri"));
  QTRY_VERIFY(input->completer() != nullptr);
  QTRY_VERIFY(input->completer()->model()->rowCount() >= 1);
  QCOMPARE(input->completer()->model()->index(0, 0).data().toString(),
           QStringLiteral("Pricing investigation"));

  QTest::keyClick(input, Qt::Key_Escape);
  QVERIFY(input->text().isEmpty());
  QVERIFY(!results->isVisible());
}

void PreferencesTest::modelVisibilityFiltersPickerAndPersists() {
  hermes::ui::SetNativePref(QStringLiteral("native.model_visibility"),
                            QString());
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::Composer composer(theme);
  hermes::ui::ModelProvider first;
  first.name = QStringLiteral("Provider One");
  first.slug = QStringLiteral("p1");
  first.models = {QStringLiteral("m1"), QStringLiteral("m2")};
  hermes::ui::ModelProvider second;
  second.name = QStringLiteral("Provider Two");
  second.slug = QStringLiteral("p2");
  second.models = {QStringLiteral("m3")};
  composer.SetModelCatalog({first, second}, QStringLiteral("p1"),
                           QStringLiteral("m1"));

  const QSet<QString> hidden{hermes::ui::ModelVisibilityKey(
                                 QStringLiteral("p1"), QStringLiteral("m2")),
                             hermes::ui::ModelVisibilityKey(
                                 QStringLiteral("p2"), QStringLiteral("m3"))};
  composer.SetHiddenModels(hidden);
  const auto visible = hermes::ui::VisibleModelCatalog({first, second}, hidden);
  QCOMPARE(static_cast<int>(visible.size()), 1);
  QCOMPARE(visible[0].slug, QStringLiteral("p1"));
  QCOMPARE(static_cast<int>(visible[0].models.size()), 1);
  QCOMPARE(visible[0].models[0], QStringLiteral("m1"));

  QDialog* dialog = composer.ShowModelVisibilityDialogForTest();
  QVERIFY(dialog != nullptr);
  dialog->show();
  auto* m1 =
      dialog->findChild<QCheckBox*>(QStringLiteral("modelVisibility_p1_m1"));
  auto* m2 =
      dialog->findChild<QCheckBox*>(QStringLiteral("modelVisibility_p1_m2"));
  QVERIFY(m1 != nullptr);
  QVERIFY(m2 != nullptr);
  QVERIFY(m1->isChecked());
  QVERIFY(!m2->isChecked());

  m2->setChecked(true);
  auto* save =
      dialog->findChild<QPushButton*>(QStringLiteral("modelVisibilitySave"));
  QVERIFY(save != nullptr);
  QTest::mouseClick(save, Qt::LeftButton);
  QCOMPARE(composer.HiddenModels().size(), 1);
  QVERIFY(composer.HiddenModels().contains(hermes::ui::ModelVisibilityKey(
      QStringLiteral("p2"), QStringLiteral("m3"))));
  QCOMPARE(hermes::ui::ParseHiddenModels(hermes::ui::NativePref(
               QStringLiteral("native.model_visibility"))),
           composer.HiddenModels());

  auto* all =
      dialog->findChild<QPushButton*>(QStringLiteral("modelVisibilityAll"));
  QVERIFY(all != nullptr);
  QTest::mouseClick(all, Qt::LeftButton);
  QTest::mouseClick(save, Qt::LeftButton);
  QVERIFY(composer.HiddenModels().isEmpty());
  QVERIFY(hermes::ui::FormatHiddenModels(
              QSet<QString>{QStringLiteral("b/2"), QStringLiteral("a/1")}) ==
          QStringLiteral("a/1\nb/2"));
  dialog->deleteLater();
}

void PreferencesTest::projectsParseListTreeAndMutators() {
  using hermes::ui::ArtifactsWorkingDirectory;
  using hermes::ui::BuildProjectCreateParams;
  using hermes::ui::BuildProjectFolderParams;
  using hermes::ui::BuildProjectUpdateParams;
  using hermes::ui::ParseProjectsList;
  using hermes::ui::ParseProjectsTree;
  using hermes::ui::ProjectsState;
  using hermes::ui::ProjectWorkingDirectory;

  const QJsonObject list{
      {QStringLiteral("active_id"), QStringLiteral("p1")},
      {QStringLiteral("projects"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("p1")},
               {QStringLiteral("name"), QStringLiteral("Alpha")},
               {QStringLiteral("description"), QStringLiteral("First")},
               {QStringLiteral("folders"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("path"), QStringLiteral("/code/alpha")},
                    {QStringLiteral("label"), QStringLiteral("Alpha")},
                    {QStringLiteral("is_primary"), true}}}}},
           QJsonObject{{QStringLiteral("id"), QStringLiteral("p2")},
                       {QStringLiteral("name"), QStringLiteral("Beta")},
                       {QStringLiteral("archived"), true}}}}};
  ProjectsState state;
  QString error;
  QVERIFY(ParseProjectsList(list, &state, &error));
  QVERIFY(error.isEmpty());
  QCOMPARE(state.active_id, QStringLiteral("p1"));
  QCOMPARE(static_cast<int>(state.projects.size()), 2);
  QCOMPARE(state.projects[0].name, QStringLiteral("Alpha"));
  QCOMPARE(state.projects[0].primary_path, QStringLiteral("/code/alpha"));
  QVERIFY(state.projects[1].archived);

  const QJsonObject tree{
      {QStringLiteral("active_id"), QStringLiteral("p2")},
      {QStringLiteral("projects"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("p1")},
               {QStringLiteral("label"), QStringLiteral("Alpha")},
               {QStringLiteral("path"), QStringLiteral("/tree/alpha")},
               {QStringLiteral("sessionCount"), 3},
               {QStringLiteral("sessionIds"), QJsonArray{QStringLiteral("s1")}},
               {QStringLiteral("previewSessions"),
                QJsonArray{
                    QJsonObject{{QStringLiteral("id"), QStringLiteral("s2")}}}},
               {QStringLiteral("repos"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("id"), QStringLiteral("r1")},
                    {QStringLiteral("groups"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("id"), QStringLiteral("main")},
                         {QStringLiteral("sessions"),
                          QJsonArray{
                              QJsonObject{{QStringLiteral("id"),
                                           QStringLiteral("s3")}}}}}}}}}}},
           QJsonObject{{QStringLiteral("id"), QStringLiteral("home")},
                       {QStringLiteral("label"), QStringLiteral("Home")},
                       {QStringLiteral("isNoProject"), true}}}}};
  QVERIFY(ParseProjectsTree(tree, &state, &error));
  QCOMPARE(state.active_id, QStringLiteral("p2"));
  QCOMPARE(static_cast<int>(state.tree.size()), 2);
  QCOMPARE(state.tree[0].session_count, 3);
  QCOMPARE(state.tree[0].repo_count, 1);
  QCOMPARE(state.tree[0].session_ids,
           (QStringList{QStringLiteral("s1"), QStringLiteral("s2"),
                        QStringLiteral("s3")}));
  QVERIFY(state.tree[1].is_no_project);
  QCOMPARE(ProjectWorkingDirectory(state, QStringLiteral("p1")),
           QStringLiteral("/tree/alpha"));
  QCOMPARE(ProjectWorkingDirectory(state, QStringLiteral("p2")), QString());
  QCOMPARE(ProjectWorkingDirectory(state, hermes::ui::AllProjectsId()),
           QString());
  QCOMPARE(ArtifactsWorkingDirectory(state, QStringLiteral("p1")),
           QStringLiteral("/tree/alpha"));
  QCOMPARE(ArtifactsWorkingDirectory(state, hermes::ui::AllProjectsId()),
           QDir::homePath());
  QCOMPARE(ArtifactsWorkingDirectory(state, QStringLiteral("p2")),
           QDir::homePath());

  const QJsonObject create = BuildProjectCreateParams(
      QStringLiteral("Alpha"),
      {QStringLiteral("/code/alpha"), QStringLiteral(" /code/shared "),
       QString()},
      QStringLiteral("/code/alpha"), QStringLiteral(" First "), true);
  QCOMPARE(create.value(QStringLiteral("name")).toString(),
           QStringLiteral("Alpha"));
  QCOMPARE(create.value(QStringLiteral("folders")).toArray().size(), 2);
  QCOMPARE(create.value(QStringLiteral("primary_path")).toString(),
           QStringLiteral("/code/alpha"));
  QCOMPARE(create.value(QStringLiteral("description")).toString(),
           QStringLiteral("First"));
  QVERIFY(create.value(QStringLiteral("use")).toBool());

  const QJsonObject update =
      BuildProjectUpdateParams(QStringLiteral(" Alpha "), QString());
  QCOMPARE(update.value(QStringLiteral("name")).toString(),
           QStringLiteral("Alpha"));
  QVERIFY(update.contains(QStringLiteral("description")));
  QVERIFY(update.value(QStringLiteral("description")).toString().isEmpty());

  const QJsonObject folder = BuildProjectFolderParams(
      QStringLiteral("p1"), QStringLiteral("/code/alpha"), true);
  QCOMPARE(folder.value(QStringLiteral("id")).toString(), QStringLiteral("p1"));
  QVERIFY(folder.value(QStringLiteral("is_primary")).toBool());
}

void PreferencesTest::promptHistoryEditsQueuedAndSteeredEntries() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  QStringList queued{QStringLiteral("queued one")};
  QStringList steered{QStringLiteral("steered one"),
                      QStringLiteral("steered two")};
  hermes::ui::PromptHistoryDialog dialog(theme, &queued, &steered);
  dialog.show();
  QTest::qWait(10);

  int changes = 0;
  QString loaded;
  dialog.on_changed = [&changes]() { ++changes; };
  dialog.on_load = [&loaded](const QString& text) { loaded = text; };

  auto* list =
      dialog.findChild<QListWidget*>(QStringLiteral("queuedPromptList"));
  auto* editor =
      dialog.findChild<QPlainTextEdit*>(QStringLiteral("queuedPromptEditor"));
  auto* save =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptEdit"));
  auto* remove =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptRemove"));
  auto* load =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptLoad"));
  QVERIFY(list != nullptr);
  QVERIFY(editor != nullptr);
  QVERIFY(save != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(load != nullptr);
  QCOMPARE(list->count(), 3);
  QCOMPARE(list->item(0)->text().left(8), QStringLiteral("Queued ·"));

  list->setCurrentRow(0);
  editor->setPlainText(QStringLiteral("queued edited"));
  QTest::mouseClick(save, Qt::LeftButton);
  QCOMPARE(queued[0], QStringLiteral("queued edited"));
  QCOMPARE(changes, 1);

  list->setCurrentRow(1);
  editor->setPlainText(QStringLiteral("steered edited"));
  QTest::mouseClick(save, Qt::LeftButton);
  QCOMPARE(steered[0], QStringLiteral("steered edited"));
  QCOMPARE(changes, 2);

  list->setCurrentRow(1);
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(steered, (QStringList{QStringLiteral("steered two")}));
  QCOMPARE(changes, 3);
  QCOMPARE(list->count(), 2);

  list->setCurrentRow(1);
  QTest::mouseClick(load, Qt::LeftButton);
  QCOMPARE(loaded, QStringLiteral("steered two"));
}

void PreferencesTest::promptHistoryInjectsLocalAndServerQueues() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  QStringList queued{QStringLiteral("local queued prompt")};
  QStringList steered{QStringLiteral("local steered prompt")};
  hermes::ui::PromptHistoryDialog dialog(theme, &queued, &steered);
  dialog.show();
  QTest::qWait(10);

  auto* list =
      dialog.findChild<QListWidget*>(QStringLiteral("queuedPromptList"));
  auto* inject =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptSteer"));
  QVERIFY(list != nullptr);
  QVERIFY(inject != nullptr);
  QCOMPARE(inject->text(), QStringLiteral("Inject now"));

  QString injected;
  hermes::ui::PromptHistoryDialog::ActionDone finish_local;
  dialog.on_inject_local =
      [&injected, &finish_local](
          const QString& text,
          hermes::ui::PromptHistoryDialog::ActionDone done) {
        injected = text;
        finish_local = std::move(done);
      };

  list->setCurrentRow(0);
  QVERIFY(inject->isVisible());
  QVERIFY(inject->isEnabled());
  QTest::mouseClick(inject, Qt::LeftButton);
  QCOMPARE(injected, QStringLiteral("local queued prompt"));
  QCOMPARE(queued.size(), 1);
  QVERIFY(finish_local != nullptr);
  finish_local(false, QStringLiteral("steer rejected"));
  QCOMPARE(queued.size(), 1);
  QCOMPARE(list->count(), 2);

  QTest::mouseClick(inject, Qt::LeftButton);
  QVERIFY(finish_local != nullptr);
  finish_local(true, QString());
  QCOMPARE(queued.size(), 0);
  QCOMPARE(list->count(), 1);
  QCOMPARE(steered.size(), 1);

  // Re-injecting a delivered steer leaves the local history intact.
  list->setCurrentRow(0);
  QCOMPARE(inject->text(), QStringLiteral("Inject again"));
  QTest::mouseClick(inject, Qt::LeftButton);
  QCOMPARE(injected, QStringLiteral("local steered prompt"));
  finish_local(true, QString());
  QCOMPARE(steered, (QStringList{QStringLiteral("local steered prompt")}));

  hermes::ui::PromptHistoryDialog::ServerPrompt server;
  server.admission_id = QStringLiteral("admission-1");
  server.text = QStringLiteral("server queued prompt");
  server.status = QStringLiteral("queued");
  server.controls = QStringList{QStringLiteral("steer")};
  dialog.SetServerPrompts({server});
  QCOMPARE(list->count(), 2);
  list->setCurrentRow(0);
  QCOMPARE(inject->text(), QStringLiteral("Inject now"));

  QString server_injected;
  hermes::ui::PromptHistoryDialog::ActionDone finish_server;
  dialog.on_steer_server =
      [&server_injected, &finish_server](
          const hermes::ui::PromptHistoryDialog::ServerPrompt& prompt,
          hermes::ui::PromptHistoryDialog::ActionDone done) {
        server_injected = prompt.text;
        finish_server = std::move(done);
      };
  QTest::mouseClick(inject, Qt::LeftButton);
  QCOMPARE(server_injected, QStringLiteral("server queued prompt"));
  QCOMPARE(list->count(), 2);
  finish_server(false, QStringLiteral("owner rejected"));
  QCOMPARE(list->count(), 2);

  QTest::mouseClick(inject, Qt::LeftButton);
  finish_server(true, QString());
  QCOMPARE(list->count(), 1);
  QCOMPARE(steered.size(), 1);

  // The modal-safe refresh path feeds the same snapshot repeatedly; rows must
  // not be duplicated and the selected admission must stay selected.
  dialog.SetServerPrompts({server});
  dialog.SetServerPrompts({server});
  QCOMPARE(list->count(), 2);
  QCOMPARE(list->currentItem()->data(Qt::UserRole + 2).toString(),
           QStringLiteral("admission-1"));

  // Closing the dialog while the steer ACK is in flight must not strand the
  // local queue entry.
  QStringList late_queued{QStringLiteral("late queued prompt")};
  QStringList late_steered;
  auto* late =
      new hermes::ui::PromptHistoryDialog(theme, &late_queued, &late_steered);
  late->show();
  QTest::qWait(10);
  hermes::ui::PromptHistoryDialog::ActionDone finish_late;
  late->on_inject_local =
      [&finish_late](const QString&,
                     hermes::ui::PromptHistoryDialog::ActionDone done) {
        finish_late = std::move(done);
      };
  auto* late_list =
      late->findChild<QListWidget*>(QStringLiteral("queuedPromptList"));
  auto* late_inject =
      late->findChild<QPushButton*>(QStringLiteral("queuedPromptSteer"));
  QVERIFY(late_list != nullptr);
  QVERIFY(late_inject != nullptr);
  late_list->setCurrentRow(0);
  QTest::mouseClick(late_inject, Qt::LeftButton);
  QVERIFY(finish_late != nullptr);
  delete late;
  finish_late(true, QString());
  QCOMPARE(late_queued.size(), 0);
}

void PreferencesTest::promptHistoryEditsAndCancelsServerQueue() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  QStringList queued;
  QStringList steered;
  hermes::ui::PromptHistoryDialog dialog(theme, &queued, &steered);
  hermes::ui::PromptHistoryDialog::ServerPrompt first;
  first.admission_id = QStringLiteral("a1");
  first.input_id = QStringLiteral("input-1");
  first.text = QStringLiteral("first queued prompt");
  first.status = QStringLiteral("queued");
  first.controls =
      QStringList{QStringLiteral("steer"), QStringLiteral("replace")};
  hermes::ui::PromptHistoryDialog::ServerPrompt unknown;
  unknown.admission_id = QStringLiteral("a2");
  unknown.input_id = QStringLiteral("input-2");
  unknown.text = QStringLiteral("lost across restart");
  unknown.status = QStringLiteral("unknown");
  unknown.execution_generation = 12;
  dialog.SetServerPrompts({first, unknown});
  dialog.show();
  QTest::qWait(10);

  auto* list =
      dialog.findChild<QListWidget*>(QStringLiteral("queuedPromptList"));
  auto* editor =
      dialog.findChild<QPlainTextEdit*>(QStringLiteral("queuedPromptEditor"));
  auto* save =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptEdit"));
  auto* remove =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptRemove"));
  auto* steer =
      dialog.findChild<QPushButton*>(QStringLiteral("queuedPromptSteer"));
  QVERIFY(list != nullptr);
  QVERIFY(editor != nullptr);
  QVERIFY(save != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(steer != nullptr);
  QCOMPARE(list->count(), 2);
  QCOMPARE(list->item(0)->text().left(8), QStringLiteral("Queued ·"));
  QCOMPARE(list->item(1)->text().left(9), QStringLiteral("Unknown ·"));

  QString replaced_admission;
  QString replaced_text;
  dialog.on_replace_server =
      [&replaced_admission, &replaced_text](
          const hermes::ui::PromptHistoryDialog::ServerPrompt& prompt,
          const QString& text,
          hermes::ui::PromptHistoryDialog::ActionDone done) {
        replaced_admission = prompt.admission_id;
        replaced_text = text;
        done(true, QString());
      };
  list->setCurrentRow(0);
  QVERIFY(save->isEnabled());
  QVERIFY(!editor->isReadOnly());
  QVERIFY(steer->isVisible());
  QVERIFY(steer->isEnabled());
  editor->setPlainText(QStringLiteral("edited queued prompt"));
  QTest::mouseClick(save, Qt::LeftButton);
  QCOMPARE(replaced_admission, QStringLiteral("a1"));
  QCOMPARE(replaced_text, QStringLiteral("edited queued prompt"));
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text().left(9), QStringLiteral("Unknown ·"));

  // The unknown admission has no replace/steer verbs; only resolve/cancel.
  list->setCurrentRow(0);
  QVERIFY(!save->isEnabled());
  QVERIFY(editor->isReadOnly());
  QVERIFY(steer->isVisible());
  QVERIFY(!steer->isEnabled());
  QString canceled_admission;
  dialog.on_cancel_server =
      [&canceled_admission](
          const hermes::ui::PromptHistoryDialog::ServerPrompt& prompt,
          hermes::ui::PromptHistoryDialog::ActionDone done) {
        canceled_admission = prompt.admission_id;
        done(true, QString());
      };
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(canceled_admission, QStringLiteral("a2"));
  QCOMPARE(list->count(), 0);

  hermes::ui::PromptHistoryDialog::ServerPrompt steerable = first;
  steerable.admission_id = QStringLiteral("a3");
  dialog.SetServerPrompts({steerable});
  QCOMPARE(list->count(), 1);
  list->setCurrentRow(0);
  QString steered_admission;
  dialog.on_steer_server =
      [&steered_admission](
          const hermes::ui::PromptHistoryDialog::ServerPrompt& prompt,
          hermes::ui::PromptHistoryDialog::ActionDone done) {
        steered_admission = prompt.admission_id;
        done(true, QString());
      };
  QTest::mouseClick(steer, Qt::LeftButton);
  QCOMPARE(steered_admission, QStringLiteral("a3"));
  QCOMPARE(list->count(), 0);

  // The owner's FIFO can reorder between snapshots. Selection follows the
  // admission id, not the old row index.
  dialog.SetServerPrompts({first, unknown});
  list->setCurrentRow(1);
  dialog.SetServerPrompts({unknown, first});
  QCOMPARE(list->currentItem()->data(Qt::UserRole + 2).toString(),
           QStringLiteral("a2"));

  const QJsonObject normal = hermes::ui::BuildPromptSubmitParams(
      QStringLiteral("s1"), QStringLiteral("hello"), QStringLiteral("work"),
      QStringLiteral("submission-1"));
  QVERIFY(!normal.contains(QStringLiteral("queued")));
  const QJsonObject queued_submit = hermes::ui::BuildPromptSubmitParams(
      QStringLiteral("s1"), QStringLiteral("run after"), QStringLiteral("work"),
      QStringLiteral("submission-2"), 0, false, false, true);
  QCOMPARE(queued_submit.value(QStringLiteral("queued")).toBool(), true);
}

void PreferencesTest::projectDialogEditsCreateAndDisplayFields() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::ProjectDialog create(theme, nullptr);
  create.show();
  QTest::qWait(10);
  QVERIFY(create.is_create());
  auto* name = create.findChild<QLineEdit*>(QStringLiteral("projectName"));
  auto* description =
      create.findChild<QLineEdit*>(QStringLiteral("projectDescription"));
  auto* folders =
      create.findChild<QPlainTextEdit*>(QStringLiteral("projectFolders"));
  auto* primary =
      create.findChild<QComboBox*>(QStringLiteral("projectPrimary"));
  auto* use = create.findChild<QCheckBox*>(QStringLiteral("projectUse"));
  QVERIFY(name != nullptr);
  QVERIFY(description != nullptr);
  QVERIFY(folders != nullptr);
  QVERIFY(primary != nullptr);
  QVERIFY(use != nullptr);
  name->setText(QStringLiteral("Gamma"));
  description->setText(QStringLiteral("Native project"));
  folders->setPlainText(
      QStringLiteral("/code/gamma\n/code/gamma\n"
                     "/code/shared\n"));
  QCOMPARE(create.folders(), (QStringList{QStringLiteral("/code/gamma"),
                                          QStringLiteral("/code/shared")}));
  QCOMPARE(primary->count(), 3);
  primary->setCurrentIndex(primary->findData(QStringLiteral("/code/shared")));
  QCOMPARE(create.primary_path(), QStringLiteral("/code/shared"));
  QVERIFY(create.use_after_create());

  hermes::ui::ProjectInfo existing;
  existing.id = QStringLiteral("p1");
  existing.name = QStringLiteral("Alpha");
  existing.description = QStringLiteral("Existing");
  existing.primary_path = QStringLiteral("/code/alpha");
  existing.folders.push_back(
      {QStringLiteral("/code/alpha"), QStringLiteral("Alpha"), true});
  hermes::ui::ProjectDialog edit(theme, &existing);
  QVERIFY(!edit.is_create());
  QCOMPARE(edit.name(), QStringLiteral("Alpha"));
  QCOMPARE(edit.description(), QStringLiteral("Existing"));
  QCOMPARE(edit.folders(), QStringList{QStringLiteral("/code/alpha")});
  QCOMPARE(edit.original_folders(), QStringList{QStringLiteral("/code/alpha")});
  auto* edit_folders =
      edit.findChild<QPlainTextEdit*>(QStringLiteral("projectFolders"));
  auto* edit_primary =
      edit.findChild<QComboBox*>(QStringLiteral("projectPrimary"));
  QVERIFY(edit_folders->isReadOnly());
  QVERIFY(!edit_primary->isEnabled());
}

void PreferencesTest::
    projectAppearanceParsesColorsIconsAndFiltersDismissedAutos() {
  QCOMPARE(hermes::ui::ProjectIconCodepoint(QStringLiteral("rocket")),
           static_cast<ushort>(0xEB44));
  QCOMPARE(hermes::ui::ProjectIconCodepoint(QStringLiteral("not-an-icon")),
           static_cast<ushort>(0xEA83));
  QCOMPARE(hermes::ui::ProjectAppearanceColors().size(), 12);
  QCOMPARE(static_cast<int>(hermes::ui::ProjectAppearanceIcons().size()), 28);

  const QColor hue =
      hermes::ui::ProjectColorValue(QStringLiteral("hsl(120 68% 58%)"));
  QVERIFY(hue.isValid());
  QCOMPARE(qRound(hue.hslHueF() * 360.0), 120);
  QCOMPARE(hermes::ui::ProjectColorValue(QStringLiteral("#f43f5e"))
               .name(QColor::HexRgb),
           QStringLiteral("#f43f5e"));
  QVERIFY(!hermes::ui::ProjectColorValue(QStringLiteral("junk")).isValid());
  QVERIFY(!hermes::ui::ProjectColorValue(QString()).isValid());

  hermes::ui::ProjectNode auto_node;
  auto_node.id = QStringLiteral("/code/auto");
  auto_node.is_auto = true;
  hermes::ui::ProjectNode explicit_node;
  explicit_node.id = QStringLiteral("p_real");
  hermes::ui::ProjectNode home_node;
  home_node.id = QStringLiteral("__home__");
  home_node.is_no_project = true;
  const QStringList dismissed{QStringLiteral("/code/auto"),
                              QStringLiteral("p_real")};
  const QString encoded = hermes::ui::EncodeDismissedAutoProjectIds(dismissed);
  QCOMPARE(hermes::ui::DecodeDismissedAutoProjectIds(encoded), dismissed);
  QVERIFY(hermes::ui::DecodeDismissedAutoProjectIds(QStringLiteral("junk"))
              .isEmpty());
  const std::vector<hermes::ui::ProjectNode> visible =
      hermes::ui::FilterVisibleProjects({auto_node, explicit_node, home_node},
                                        {QStringLiteral("/code/auto")});
  QCOMPARE(static_cast<int>(visible.size()), 2);
  QCOMPARE(visible[0].id, QStringLiteral("p_real"));
  QCOMPARE(visible[1].id, QStringLiteral("__home__"));

  const QJsonObject appearance = hermes::ui::BuildProjectAppearanceParams(
      QStringLiteral("rocket"), QStringLiteral(""));
  QCOMPARE(appearance.value(QStringLiteral("icon")).toString(),
           QStringLiteral("rocket"));
  QVERIFY(appearance.contains(QStringLiteral("color")));
  QVERIFY(appearance.value(QStringLiteral("color")).toString().isEmpty());
  const QJsonObject created = hermes::ui::BuildProjectCreateParams(
      QStringLiteral("Auto"), {QStringLiteral("/code/auto")},
      QStringLiteral("/code/auto"), QString(), false, QStringLiteral("rocket"),
      QStringLiteral("#f43f5e"));
  QCOMPARE(created.value(QStringLiteral("icon")).toString(),
           QStringLiteral("rocket"));
  QCOMPARE(created.value(QStringLiteral("color")).toString(),
           QStringLiteral("#f43f5e"));

  hermes::ui::ProjectAppearanceDialog dialog{QString(), QString()};
  QString applied_color;
  QString applied_icon;
  dialog.on_apply = [&](const QString& color, const QString& icon) {
    applied_color = color;
    applied_icon = icon;
  };
  dialog.show();
  QTest::qWait(10);
  auto* first_color =
      dialog.findChild<QPushButton*>(QStringLiteral("projectColor0"));
  auto* rocket =
      dialog.findChild<QPushButton*>(QStringLiteral("projectIcon_rocket"));
  QVERIFY(first_color != nullptr);
  QVERIFY(rocket != nullptr);
  first_color->click();
  QCOMPARE(applied_color, hermes::ui::ProjectAppearanceColors().at(0));
  QVERIFY(applied_icon.isEmpty());
  dialog.show();
  QTest::qWait(10);
  rocket->click();
  QCOMPARE(applied_color, hermes::ui::ProjectAppearanceColors().at(0));
  QCOMPARE(applied_icon, QStringLiteral("rocket"));
}

void PreferencesTest::uiCacheRoundTripsSessionsAndProjects() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QByteArray old_cache = qgetenv("HERMES_UI_CACHE");
  qputenv("HERMES_UI_CACHE", dir.path().toUtf8());

  hermes::ui::CachedSessionState session_cache;
  session_cache.profile = QStringLiteral("default");
  session_cache.selected_session_id = QStringLiteral("s1");
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Cached chat");
  session.preview = QStringLiteral("hello");
  session.model = QStringLiteral("m1");
  session.provider = QStringLiteral("p1");
  session.reasoning_effort = QStringLiteral("high");
  session.started_at_secs = 100;
  session.last_active_secs = 200;
  session.message_count = 2;
  session.revision = 4;
  session_cache.sessions.push_back(session);

  hermes::ui::ChatMessage message;
  message.row_id = 42;
  message.role = hermes::ui::MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::fromSecsSinceEpoch(200);
  message.streaming = true;
  message.reasoning = QStringLiteral("thinking");
  message.reasoning_streaming = true;
  message.raw_text = QStringLiteral("answer");
  message.duration_secs = 9;
  hermes::ui::MessageBlock block;
  block.kind = hermes::ui::BlockKind::kCode;
  block.text = QStringLiteral("print(1)");
  block.language = QStringLiteral("python");
  block.id = QStringLiteral("tool-1");
  message.blocks.push_back(block);
  message.reactions.push_back({QStringLiteral("+1"), QStringLiteral("me")});
  session_cache.messages.push_back(message);

  QVERIFY(hermes::ui::SessionCachePath(QStringLiteral("default")) !=
          hermes::ui::SessionCachePath(QStringLiteral("work")));
  QVERIFY(
      !hermes::ui::LoadSessionCache(QStringLiteral("missing"), &session_cache));
  QString error;
  QVERIFY(hermes::ui::SaveSessionCache(session_cache, &error));

  hermes::ui::CachedSessionState loaded_session;
  QVERIFY(
      hermes::ui::LoadSessionCache(QStringLiteral("default"), &loaded_session));
  QCOMPARE(loaded_session.selected_session_id, QStringLiteral("s1"));
  QCOMPARE(static_cast<int>(loaded_session.sessions.size()), 1);
  QCOMPARE(loaded_session.sessions[0].title, QStringLiteral("Cached chat"));
  QCOMPARE(loaded_session.sessions[0].revision, 4);
  QCOMPARE(loaded_session.sessions[0].reasoning_effort, QStringLiteral("high"));
  QCOMPARE(static_cast<int>(loaded_session.messages.size()), 1);
  QCOMPARE(loaded_session.messages[0].row_id, static_cast<qint64>(42));
  QCOMPARE(loaded_session.messages[0].blocks[0].kind,
           hermes::ui::BlockKind::kCode);
  QCOMPARE(loaded_session.messages[0].blocks[0].detail, QString());
  QCOMPARE(loaded_session.messages[0].reactions[0].emoji, QStringLiteral("+1"));
  // A cached row is history, never a live spinner.
  QVERIFY(!loaded_session.messages[0].streaming);
  QVERIFY(!loaded_session.messages[0].reasoning_streaming);
  QCOMPARE(loaded_session.messages[0].duration_secs, -1.0);

  hermes::ui::CachedProjectState project_cache;
  project_cache.profile = QStringLiteral("default");
  project_cache.scope = QStringLiteral("p1");
  project_cache.projects.loaded = true;
  project_cache.projects.profile = QStringLiteral("default");
  project_cache.projects.active_id = QStringLiteral("p1");
  hermes::ui::ProjectInfo project;
  project.id = QStringLiteral("p1");
  project.slug = QStringLiteral("alpha");
  project.name = QStringLiteral("Alpha");
  project.primary_path = QStringLiteral("/code/alpha");
  project.folders.push_back(
      {QStringLiteral("/code/alpha"), QStringLiteral("Alpha"), true});
  project_cache.projects.projects.push_back(project);
  hermes::ui::ProjectNode node;
  node.id = QStringLiteral("p1");
  node.label = QStringLiteral("Alpha");
  node.path = QStringLiteral("/code/alpha");
  node.session_count = 1;
  node.session_ids = {QStringLiteral("s1")};
  project_cache.projects.tree.push_back(node);
  QVERIFY(hermes::ui::SaveProjectCache(project_cache, &error));

  hermes::ui::CachedProjectState loaded_project;
  QVERIFY(
      hermes::ui::LoadProjectCache(QStringLiteral("default"), &loaded_project));
  QCOMPARE(loaded_project.scope, QStringLiteral("p1"));
  QVERIFY(loaded_project.projects.loaded);
  QCOMPARE(loaded_project.projects.active_id, QStringLiteral("p1"));
  QCOMPARE(loaded_project.projects.projects[0].name, QStringLiteral("Alpha"));
  QCOMPARE(loaded_project.projects.projects[0].folders[0].path,
           QStringLiteral("/code/alpha"));
  QVERIFY(loaded_project.projects.projects[0].folders[0].is_primary);
  QCOMPARE(loaded_project.projects.tree[0].session_ids,
           QStringList{QStringLiteral("s1")});

  // The cache keeps a useful recent window, never a second full database.
  hermes::ui::CachedSessionState dense;
  dense.profile = QStringLiteral("dense");
  for (int i = 0; i < 140; ++i) {
    hermes::ui::ChatMessage row;
    row.row_id = i;
    row.role = hermes::ui::MessageRole::kAssistant;
    row.raw_text = QStringLiteral("row %1").arg(i);
    dense.messages.push_back(row);
  }
  QVERIFY(hermes::ui::SaveSessionCache(dense, &error));
  hermes::ui::CachedSessionState loaded_dense;
  QVERIFY(hermes::ui::LoadSessionCache(QStringLiteral("dense"), &loaded_dense));
  QCOMPARE(static_cast<int>(loaded_dense.messages.size()), 120);
  QCOMPARE(loaded_dense.messages.front().row_id, static_cast<qint64>(20));
  QCOMPARE(loaded_dense.messages.back().row_id, static_cast<qint64>(139));

  // Corrupt/old files are ignored instead of breaking startup.
  {
    QFile file(hermes::ui::SessionCachePath(QStringLiteral("default")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("{not json");
  }
  QVERIFY(!hermes::ui::LoadSessionCache(QStringLiteral("default"),
                                        &loaded_session));
  {
    QFile file(hermes::ui::ProjectCachePath(QStringLiteral("default")));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(
        QJsonDocument(
            QJsonObject{{QStringLiteral("version"), 999},
                        {QStringLiteral("profile"), QStringLiteral("default")},
                        {QStringLiteral("projects"), QJsonArray{}},
                        {QStringLiteral("tree"), QJsonArray{}}})
            .toJson(QJsonDocument::Compact));
  }
  QVERIFY(!hermes::ui::LoadProjectCache(QStringLiteral("default"),
                                        &loaded_project));

  if (old_cache.isEmpty()) {
    qunsetenv("HERMES_UI_CACHE");
  } else {
    qputenv("HERMES_UI_CACHE", old_cache);
  }
}

void PreferencesTest::profileSelectionFallsBackWhenSavedNameIsGone() {
  const QStringList profiles{QStringLiteral("default"), QStringLiteral("work"),
                             QStringLiteral("research")};
  QCOMPARE(hermes::ui::ProfileIndexByName(profiles, QStringLiteral("work")), 1);
  QCOMPARE(hermes::ui::ProfileIndexByName(profiles, QStringLiteral("research")),
           2);
  QCOMPARE(hermes::ui::ProfileIndexByName(profiles, QStringLiteral("deleted")),
           -1);
  QCOMPARE(hermes::ui::ProfileIndexByName(profiles, QString()), -1);
}

void PreferencesTest::browserAutomationParsesEndpointsAndConsent() {
  using hermes::ui::BrowserConsentState;
  using hermes::ui::BrowserManageConnected;
  using hermes::ui::BrowserManageMessages;
  using hermes::ui::BrowserManageUrl;
  using hermes::ui::BrowserUseRealProfile;
  using hermes::ui::BuildBrowserConsentPatch;
  using hermes::ui::BuildBrowserManageParams;
  using hermes::ui::NormalizeCdpEndpoint;
  using hermes::ui::ParseCdpEndpoint;
  using hermes::ui::ShouldOfferBrowserConsent;

  QString error;
  auto endpoint = ParseCdpEndpoint(QString(), &error);
  QVERIFY(endpoint.has_value());
  QCOMPARE(endpoint->Display(), QStringLiteral("http://127.0.0.1:9222"));
  QVERIFY(!endpoint->direct_page);

  endpoint = ParseCdpEndpoint(QStringLiteral("localhost:9333"), &error);
  QVERIFY(endpoint.has_value());
  QCOMPARE(endpoint->Display(), QStringLiteral("http://localhost:9333"));

  endpoint = ParseCdpEndpoint(
      QStringLiteral("https://browser.test:9443/json/version"), &error);
  QVERIFY(endpoint.has_value());
  QCOMPARE(endpoint->Display(), QStringLiteral("https://browser.test:9443"));

  endpoint = ParseCdpEndpoint(
      QStringLiteral("ws://127.0.0.1:9222/devtools/page/ABC"), &error);
  QVERIFY(endpoint.has_value());
  QVERIFY(endpoint->direct_page);
  QVERIFY(!endpoint->direct_browser);
  QCOMPARE(endpoint->Display(),
           QStringLiteral("ws://127.0.0.1:9222/devtools/page/ABC"));

  endpoint = ParseCdpEndpoint(
      QStringLiteral("wss://browser.test/devtools/browser/XYZ"), &error);
  QVERIFY(endpoint.has_value());
  QVERIFY(endpoint->direct_browser);
  QCOMPARE(NormalizeCdpEndpoint(
               QStringLiteral("wss://browser.test/devtools/browser/XYZ")),
           QStringLiteral("wss://browser.test/devtools/browser/XYZ"));

  QVERIFY(!ParseCdpEndpoint(QStringLiteral("ftp://127.0.0.1:21"), &error)
               .has_value());
  QVERIFY(error.contains(QStringLiteral("scheme")));
  QVERIFY(!ParseCdpEndpoint(QStringLiteral("ws://127.0.0.1:9222/not-devtools"),
                            &error)
               .has_value());

  const QJsonObject config{
      {QStringLiteral("browser"),
       QJsonObject{{QStringLiteral("use_real_profile"), true}}}};
  QVERIFY(BrowserUseRealProfile(config));
  QVERIFY(!BrowserUseRealProfile(QJsonObject{}));
  QCOMPARE(BuildBrowserConsentPatch(true)
               .value(QStringLiteral("browser"))
               .toObject()
               .value(QStringLiteral("use_real_profile"))
               .toBool(),
           true);

  BrowserConsentState consent;
  QVERIFY(ShouldOfferBrowserConsent(consent));
  consent.use_real_profile = true;
  QVERIFY(!ShouldOfferBrowserConsent(consent));
  consent.use_real_profile = false;
  consent.prompt_dismissed = true;
  QVERIFY(!ShouldOfferBrowserConsent(consent));
  consent.prompt_dismissed = false;
  consent.muted = true;
  QVERIFY(!ShouldOfferBrowserConsent(consent));

  const QJsonObject manage = BuildBrowserManageParams(
      QStringLiteral("connect"), QStringLiteral("http://127.0.0.1:9222"),
      QStringLiteral("default"));
  QCOMPARE(manage.value(QStringLiteral("action")).toString(),
           QStringLiteral("connect"));
  QCOMPARE(manage.value(QStringLiteral("url")).toString(),
           QStringLiteral("http://127.0.0.1:9222"));
  QCOMPARE(manage.value(QStringLiteral("profile")).toString(),
           QStringLiteral("default"));
  const QJsonObject status =
      BuildBrowserManageParams(QString(), QString(), QString());
  QCOMPARE(status.value(QStringLiteral("action")).toString(),
           QStringLiteral("status"));
  QVERIFY(!status.contains(QStringLiteral("url")));
  QVERIFY(!status.contains(QStringLiteral("profile")));

  const QJsonObject result{
      {QStringLiteral("connected"), true},
      {QStringLiteral("url"), QStringLiteral("ws://127.0.0.1:9222/page")},
      {QStringLiteral("messages"),
       QJsonArray{QStringLiteral("launched"), QString()}}};
  QVERIFY(BrowserManageConnected(result));
  QCOMPARE(BrowserManageUrl(result),
           QStringLiteral("ws://127.0.0.1:9222/page"));
  QCOMPARE(BrowserManageMessages(result), QStringLiteral("launched"));
}

void PreferencesTest::browserAutomationBuildsPointerEventsAndScreenshots() {
  using hermes::ui::BrowserScreenshotName;
  using hermes::ui::BuildBrowserClickEvents;
  using hermes::ui::BuildBrowserDragEvents;
  using hermes::ui::DecodeBrowserScreenshot;
  using hermes::ui::SaveBrowserScreenshot;

  const auto click =
      BuildBrowserClickEvents(10.0, 20.0, QStringLiteral("left"), 2);
  QCOMPARE(click.size(), static_cast<std::size_t>(3));
  QCOMPARE(click[0].value(QStringLiteral("type")).toString(),
           QStringLiteral("mouseMoved"));
  QCOMPARE(click[0].value(QStringLiteral("buttons")).toInt(), 0);
  QCOMPARE(click[1].value(QStringLiteral("type")).toString(),
           QStringLiteral("mousePressed"));
  QCOMPARE(click[1].value(QStringLiteral("button")).toString(),
           QStringLiteral("left"));
  QCOMPARE(click[1].value(QStringLiteral("buttons")).toInt(), 1);
  QCOMPARE(click[1].value(QStringLiteral("clickCount")).toInt(), 2);
  QCOMPARE(click[2].value(QStringLiteral("type")).toString(),
           QStringLiteral("mouseReleased"));
  QCOMPARE(click[2].value(QStringLiteral("buttons")).toInt(), 0);
  QCOMPARE(click[2].value(QStringLiteral("x")).toDouble(), 10.0);
  QCOMPARE(click[2].value(QStringLiteral("y")).toDouble(), 20.0);

  const auto right =
      BuildBrowserClickEvents(1.0, 2.0, QStringLiteral("right"), 1);
  QCOMPARE(right[1].value(QStringLiteral("button")).toString(),
           QStringLiteral("right"));
  QCOMPARE(right[1].value(QStringLiteral("buttons")).toInt(), 2);

  const auto drag =
      BuildBrowserDragEvents(1.0, 2.0, 30.0, 40.0, QStringLiteral("left"));
  QCOMPARE(drag.size(), static_cast<std::size_t>(4));
  QCOMPARE(drag[1].value(QStringLiteral("type")).toString(),
           QStringLiteral("mousePressed"));
  QCOMPARE(drag[2].value(QStringLiteral("type")).toString(),
           QStringLiteral("mouseMoved"));
  QCOMPARE(drag[2].value(QStringLiteral("buttons")).toInt(), 1);
  QCOMPARE(drag[2].value(QStringLiteral("x")).toDouble(), 30.0);
  QCOMPARE(drag[3].value(QStringLiteral("type")).toString(),
           QStringLiteral("mouseReleased"));

  const QByteArray png("PNG\x00\x01", 5);
  const QJsonObject screenshot{
      {QStringLiteral("data"), QString::fromLatin1(png.toBase64())}};
  QByteArray decoded;
  QString error;
  QVERIFY(DecodeBrowserScreenshot(screenshot, &decoded, &error));
  QCOMPARE(decoded, png);
  QVERIFY(!DecodeBrowserScreenshot(QJsonObject{}, &decoded, &error));
  QVERIFY(!error.isEmpty());

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.filePath(QStringLiteral("shot.png"));
  QVERIFY(SaveBrowserScreenshot(png, path, &error));
  QFile saved(path);
  QVERIFY(saved.open(QIODevice::ReadOnly));
  QCOMPARE(saved.readAll(), png);
  QVERIFY(!SaveBrowserScreenshot(QByteArray(), path, &error));

  const QDateTime stamp(QDate(2026, 11, 11), QTime(9, 5, 7));
  QCOMPARE(BrowserScreenshotName(stamp),
           QStringLiteral("hermes-browser-20261111-090507.png"));
}

void PreferencesTest::cdpClientDispatchesScreenshotAndCoordinates() {
  using hermes::ui::CdpClient;

  QWebSocketServer server(QStringLiteral("cdp-test"),
                          QWebSocketServer::NonSecureMode);
  QVERIFY(server.listen(QHostAddress::LocalHost, 0));
  QVector<QJsonObject> commands;
  QWebSocket* peer = nullptr;
  QObject::connect(&server, &QWebSocketServer::newConnection, &server, [&]() {
    peer = server.nextPendingConnection();
    if (peer == nullptr) {
      return;
    }
    QObject::connect(
        peer, &QWebSocket::textMessageReceived, &server,
        [&](const QString& text) {
          const QJsonObject frame =
              QJsonDocument::fromJson(text.toUtf8()).object();
          commands.append(frame);
          QJsonObject response{{QStringLiteral("id"),
                                frame.value(QStringLiteral("id")).toInt()}};
          if (frame.value(QStringLiteral("method")).toString() ==
              QStringLiteral("Page.captureScreenshot")) {
            response.insert(QStringLiteral("result"),
                            QJsonObject{{QStringLiteral("data"),
                                         QString::fromLatin1(
                                             QByteArray("PNG").toBase64())}});
          } else {
            response.insert(QStringLiteral("result"), QJsonObject());
          }
          peer->sendTextMessage(QString::fromUtf8(
              QJsonDocument(response).toJson(QJsonDocument::Compact)));
        });
  });

  CdpClient client;
  QSignalSpy connected(&client, &CdpClient::Connected);
  const QString websocket =
      QStringLiteral("ws://127.0.0.1:%1/devtools/page/TEST")
          .arg(server.serverPort());
  client.Connect(websocket);
  QTRY_COMPARE(connected.count(), 1);
  QVERIFY(client.IsConnected());
  QTRY_VERIFY(peer != nullptr);

  QByteArray png;
  QString screenshot_error;
  client.CaptureScreenshot([&](const QByteArray& bytes, const QString& error) {
    png = bytes;
    screenshot_error = error;
  });
  QTRY_VERIFY(!png.isEmpty());
  QVERIFY(screenshot_error.isEmpty());
  QCOMPARE(png, QByteArray("PNG"));

  bool click_done = false;
  QString click_error;
  client.Click(12.0, 34.0, QStringLiteral("left"), 2,
               [&](const QJsonObject&, const QString& error) {
                 click_done = true;
                 click_error = error;
               });
  QTRY_VERIFY(click_done);
  QVERIFY(click_error.isEmpty());

  bool drag_done = false;
  client.Drag(1.0, 2.0, 3.0, 4.0, QStringLiteral("left"),
              [&](const QJsonObject&, const QString& error) {
                drag_done = true;
                click_error = error;
              });
  QTRY_VERIFY(drag_done);
  QVERIFY(click_error.isEmpty());

  QStringList input_types;
  for (const QJsonObject& frame : commands) {
    if (frame.value(QStringLiteral("method")).toString() ==
        QStringLiteral("Input.dispatchMouseEvent")) {
      input_types.append(frame.value(QStringLiteral("params"))
                             .toObject()
                             .value(QStringLiteral("type"))
                             .toString());
    }
  }
  QCOMPARE(input_types,
           QStringList(
               {QStringLiteral("mouseMoved"), QStringLiteral("mousePressed"),
                QStringLiteral("mouseReleased"), QStringLiteral("mouseMoved"),
                QStringLiteral("mousePressed"), QStringLiteral("mouseMoved"),
                QStringLiteral("mouseReleased")}));

  client.Disconnect();
  QVERIFY(!client.IsConnected());
  if (peer != nullptr) {
    peer->deleteLater();
  }
  server.close();
}

void PreferencesTest::cdpClientDiscoversHttpAndAttachesBrowserTarget() {
  using hermes::ui::CdpClient;

  QWebSocketServer browser(QStringLiteral("cdp-browser-test"),
                           QWebSocketServer::NonSecureMode);
  QVERIFY(browser.listen(QHostAddress::LocalHost, 0));
  bool attach_flatten = false;
  QString attached_target;
  QString screenshot_session;
  QWebSocket* peer = nullptr;
  QObject::connect(&browser, &QWebSocketServer::newConnection, &browser, [&]() {
    peer = browser.nextPendingConnection();
    if (peer == nullptr) {
      return;
    }
    QObject::connect(
        peer, &QWebSocket::textMessageReceived, &browser,
        [&](const QString& text) {
          const QJsonObject frame =
              QJsonDocument::fromJson(text.toUtf8()).object();
          const QString method =
              frame.value(QStringLiteral("method")).toString();
          QJsonObject result;
          if (method == QStringLiteral("Target.getTargets")) {
            result.insert(
                QStringLiteral("targetInfos"),
                QJsonArray{QJsonObject{
                    {QStringLiteral("type"), QStringLiteral("page")},
                    {QStringLiteral("targetId"), QStringLiteral("PAGE1")}}});
          } else if (method == QStringLiteral("Target.attachToTarget")) {
            const QJsonObject params =
                frame.value(QStringLiteral("params")).toObject();
            attached_target =
                params.value(QStringLiteral("targetId")).toString();
            attach_flatten = params.value(QStringLiteral("flatten")).toBool();
            result.insert(QStringLiteral("sessionId"),
                          QStringLiteral("SESSION1"));
          } else if (method == QStringLiteral("Page.captureScreenshot")) {
            screenshot_session =
                frame.value(QStringLiteral("sessionId")).toString();
            result.insert(QStringLiteral("data"),
                          QString::fromLatin1(QByteArray("PNG2").toBase64()));
          }
          QJsonObject response{
              {QStringLiteral("id"), frame.value(QStringLiteral("id")).toInt()},
              {QStringLiteral("result"), result}};
          peer->sendTextMessage(QString::fromUtf8(
              QJsonDocument(response).toJson(QJsonDocument::Compact)));
        });
  });

  QTcpServer http;
  QVERIFY(http.listen(QHostAddress::LocalHost, 0));
  QObject::connect(&http, &QTcpServer::newConnection, &http, [&]() {
    QTcpSocket* socket = http.nextPendingConnection();
    QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
      socket->readAll();
      const QString websocket =
          QStringLiteral("ws://127.0.0.1:%1/devtools/page/PAGE1")
              .arg(browser.serverPort());
      const QByteArray body =
          QStringLiteral(
              "[{\"type\":\"page\",\"webSocketDebuggerUrl\":\"%1\"}]")
              .arg(websocket)
              .toUtf8();
      socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n");
      socket->write("Content-Length: ");
      socket->write(QByteArray::number(body.size()));
      socket->write("\r\nConnection: close\r\n\r\n");
      socket->write(body);
      socket->disconnectFromHost();
    });
  });

  CdpClient page_client;
  QSignalSpy page_connected(&page_client, &CdpClient::Connected);
  page_client.Connect(
      QStringLiteral("http://127.0.0.1:%1").arg(http.serverPort()));
  QTRY_COMPARE(page_connected.count(), 1);
  QTRY_VERIFY(peer != nullptr);
  QByteArray png;
  QString error;
  page_client.CaptureScreenshot(
      [&](const QByteArray& bytes, const QString& capture_error) {
        png = bytes;
        error = capture_error;
      });
  QTRY_VERIFY(!png.isEmpty());
  QVERIFY(error.isEmpty());
  QCOMPARE(png, QByteArray("PNG2"));
  QVERIFY(screenshot_session.isEmpty());
  page_client.Disconnect();

  CdpClient browser_client;
  QSignalSpy browser_connected(&browser_client, &CdpClient::Connected);
  browser_client.Connect(
      QStringLiteral("ws://127.0.0.1:%1/devtools/browser/BROWSER")
          .arg(browser.serverPort()));
  QTRY_COMPARE(browser_connected.count(), 1);
  QTRY_VERIFY(attach_flatten);
  QCOMPARE(attached_target, QStringLiteral("PAGE1"));
  png.clear();
  error.clear();
  browser_client.CaptureScreenshot(
      [&](const QByteArray& bytes, const QString& capture_error) {
        png = bytes;
        error = capture_error;
      });
  QTRY_VERIFY(!png.isEmpty());
  QVERIFY(error.isEmpty());
  QCOMPARE(screenshot_session, QStringLiteral("SESSION1"));
  browser_client.Disconnect();

  if (peer != nullptr) {
    peer->deleteLater();
  }
  http.close();
  browser.close();
}

void PreferencesTest::browserAutomationDialogSurfacesControls() {
  using hermes::ui::BrowserAutomationDialog;
  using hermes::ui::BrowserConsentDialog;
  using hermes::ui::CdpCallback;

  int manage_calls = 0;
  BrowserAutomationDialog::Hooks hooks;
  hooks.manage = [&](const QString& action, const QString&, CdpCallback done) {
    ++manage_calls;
    if (action == QStringLiteral("connect")) {
      done(QJsonObject{{QStringLiteral("connected"), false},
                       {QStringLiteral("messages"),
                        QJsonArray{QStringLiteral("No debug browser found")}}},
           QString());
      return;
    }
    done(QJsonObject{{QStringLiteral("connected"), false}}, QString());
  };
  BrowserAutomationDialog dialog(std::move(hooks));
  QCOMPARE(dialog.Endpoint(), QStringLiteral("http://127.0.0.1:9222"));
  QVERIFY(dialog.findChild<QLineEdit*>(
              QStringLiteral("browserAutomationUrl")) != nullptr);
  QVERIFY(dialog.findChild<QPushButton*>(
              QStringLiteral("browserAutomationScreenshot")) != nullptr);
  QVERIFY(dialog.findChild<QPushButton*>(
              QStringLiteral("browserAutomationClick")) != nullptr);
  QVERIFY(dialog.findChild<QDoubleSpinBox*>(
              QStringLiteral("browserAutomationX2")) != nullptr);
  auto* screenshot = dialog.findChild<QPushButton*>(
      QStringLiteral("browserAutomationScreenshot"));
  QVERIFY(screenshot != nullptr);
  QVERIFY(!screenshot->isEnabled());
  auto* connect = dialog.findChild<QPushButton*>(
      QStringLiteral("browserAutomationConnect"));
  QVERIFY(connect != nullptr);
  connect->click();
  QTRY_COMPARE(manage_calls, 1);
  auto* status =
      dialog.findChild<QLabel*>(QStringLiteral("browserAutomationStatus"));
  QVERIFY(status != nullptr);
  QVERIFY(status->text().contains(QStringLiteral("No debug browser found")));
  QVERIFY(connect->isEnabled());

  BrowserConsentDialog consent;
  QVERIFY(consent.findChild<QPushButton*>(
              QStringLiteral("browserConsentEnable")) != nullptr);
  auto* dont_ask =
      consent.findChild<QCheckBox*>(QStringLiteral("browserConsentDontAsk"));
  QVERIFY(dont_ask != nullptr);
  QVERIFY(!consent.dont_ask_again());
  dont_ask->setChecked(true);
  QVERIFY(consent.dont_ask_again());
}

void PreferencesTest::historyHydratesAndPreservesDisplayReasoning() {
  using hermes::ui::ChatMessage;
  using hermes::ui::HistoryContainsAssistant;
  using hermes::ui::MessageRole;
  using hermes::ui::PreserveLiveReasoning;
  using hermes::ui::ReasoningTextFromDetails;
  using hermes::ui::ReasoningTextFromRow;

  // display_reasoning is authoritative, including when a commentary split
  // intentionally leaves it empty.
  QCOMPARE(ReasoningTextFromRow(QJsonObject{
               {QStringLiteral("display_reasoning"), QStringLiteral("Shown")},
               {QStringLiteral("reasoning"), QStringLiteral("Raw")}}),
           QStringLiteral("Shown"));
  QCOMPARE(ReasoningTextFromRow(QJsonObject{
               {QStringLiteral("display_reasoning"), QString()},
               {QStringLiteral("reasoning"), QStringLiteral("Raw")}}),
           QString());
  QCOMPARE(ReasoningTextFromRow(QJsonObject{
               {QStringLiteral("reasoning"), QStringLiteral("Raw thought")}}),
           QStringLiteral("Raw thought"));

  // Structured provider envelopes expose prose but never opaque replay data.
  const QJsonArray envelope{
      QJsonObject{
          {QStringLiteral("type"), QStringLiteral("reasoning.summary")},
          {QStringLiteral("summary"), QStringLiteral("Checked the lock.")},
          {QStringLiteral("signature"), QStringLiteral("sig")}},
      QJsonObject{
          {QStringLiteral("type"), QStringLiteral("reasoning.text")},
          {QStringLiteral("text"), QStringLiteral("Return the result.")}}};
  QCOMPARE(ReasoningTextFromDetails(envelope),
           QStringLiteral("Checked the lock.\n\nReturn the result."));
  QCOMPARE(ReasoningTextFromDetails(QString::fromUtf8(
               QJsonDocument(envelope).toJson(QJsonDocument::Compact))),
           QStringLiteral("Checked the lock.\n\nReturn the result."));
  QCOMPARE(ReasoningTextFromDetails(QStringLiteral(
               "[{\"type\":\"thinking\",\"thinking\":\"truncated\"")),
           QString());

  const QDateTime now = QDateTime::currentDateTime();
  ChatMessage live;
  live.role = MessageRole::kAssistant;
  live.timestamp = now;
  live.reasoning = QStringLiteral("The answer lives in reasoning.");

  QVERIFY(!HistoryContainsAssistant({}, live));
  ChatMessage user;
  user.role = MessageRole::kUser;
  user.timestamp = now.addSecs(-2);
  QVERIFY(!HistoryContainsAssistant({user}, live));

  ChatMessage hydrated;
  hydrated.role = MessageRole::kAssistant;
  hydrated.timestamp = now;
  hydrated.raw_text = QStringLiteral("Answer.");
  std::vector<ChatMessage> rows{hydrated};
  QVERIFY(HistoryContainsAssistant(rows, live));
  PreserveLiveReasoning(&rows, {live});
  QCOMPARE(rows.front().reasoning, live.reasoning);
  QCOMPARE(rows.front().raw_text, QStringLiteral("Answer."));

  // History reasoning wins when it is already present.
  hydrated.reasoning = QStringLiteral("Persisted reasoning.");
  rows = {hydrated};
  PreserveLiveReasoning(&rows, {live});
  QCOMPARE(rows.front().reasoning, QStringLiteral("Persisted reasoning."));
}

QTEST_MAIN(PreferencesTest)
#include "preferences_test.moc"

void PreferencesTest::notificationPolicyMatchesDesktopAttentionRules() {
  hermes::ui::NotificationPrefs prefs;
  using hermes::ui::NotificationKind;
  using hermes::ui::ShouldDeliverNotification;

  // Completion alerts only fire for the selected session while the window is
  // in the background, so a busy gateway cannot spam per-session toasts.
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kTurnDone, prefs, false,
                                     true, false));
  QVERIFY(ShouldDeliverNotification(NotificationKind::kTurnDone, prefs, true,
                                    true, false));
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kTurnDone, prefs, true,
                                     false, false));

  // Blocking prompts break through for another chat even while focused.
  QVERIFY(ShouldDeliverNotification(NotificationKind::kApproval, prefs, false,
                                    false, false));
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kApproval, prefs, false,
                                     true, false));
  QVERIFY(ShouldDeliverNotification(NotificationKind::kInput, prefs, true, true,
                                    false));

  // Global notifications have no session and only require backgrounding.
  QVERIFY(ShouldDeliverNotification(NotificationKind::kCredits, prefs, true,
                                    false, true));
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kCredits, prefs, false,
                                     false, true));

  prefs.kinds[hermes::ui::NotificationKindIndex(NotificationKind::kApproval)] =
      false;
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kApproval, prefs, false,
                                     false, false));
  prefs.enabled = false;
  prefs.kinds[hermes::ui::NotificationKindIndex(NotificationKind::kCredits)] =
      true;
  QVERIFY(!ShouldDeliverNotification(NotificationKind::kCredits, prefs, true,
                                     false, true));

  hermes::ui::NotificationThrottle throttle;
  QVERIFY(throttle.Allow(QStringLiteral("turnDone:s1"), 10000));
  QVERIFY(!throttle.Allow(QStringLiteral("turnDone:s1"), 10500));
  QVERIFY(throttle.Allow(QStringLiteral("turnDone:s2"), 10500));
  QVERIFY(throttle.Allow(QStringLiteral("turnDone:s1"), 11000));
}
