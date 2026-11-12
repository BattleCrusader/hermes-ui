// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/settings_view.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPair>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QTextBrowser>
#include <QTextFrame>
#include <QTextTable>
#include <QtTest>
#include <memory>
#include <utility>
#include <vector>

#include "src/appearance_prefs.h"
#include "src/fallback_models.h"
#include "src/native_prefs.h"
#include "src/notification_prefs.h"
#include "src/qr_code.h"
#include "src/quick_entry.h"
#include "src/settings_pages.h"
#include "src/theme_presets.h"

namespace {

hermes::ui::ProfileSnapshot Snapshot() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.label = QStringLiteral("default");
  snapshot.config_loaded = true;
  snapshot.config = QJsonObject{
      {QStringLiteral("model_context_length"), 123},
      {QStringLiteral("display"),
       QJsonObject{{QStringLiteral("show_reasoning"), true}}},
  };
  snapshot.config_schema = QJsonObject{
      {QStringLiteral("model_context_length"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
                   {QStringLiteral("description"),
                    QStringLiteral("Context window override")}}},
      {QStringLiteral("display.show_reasoning"),
       QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")},
                   {QStringLiteral("description"),
                    QStringLiteral("Show reasoning blocks")}}},
  };
  return snapshot;
}

}  // namespace

class SettingsViewTest : public QObject {
  Q_OBJECT

 private slots:
  void initTestCase();
  void rendersDesktopSectionsAndSavesSparsePatch();
  void rendersAboutAndSessionPages();
  void rendersEveryDataPageFromSnapshot();
  void pageActionsRouteThroughWorkerCallback();
  void scheduledTaskRowsWrapAndUseIconActions();
  void capabilityToolsetsAreCheckboxes();
  void sessionsArchiveAndTransfer();
  void themesPageAppliesPreset();
  void rendersWorkspacePagesFromSnapshot();
  void rendersAgentRosterFromSubagentEvents();
  void rendersStarmapGraphAndDeletesNode();
  void quickEntrySubmitsOnEnter();
  void capabilitiesComputerUseStatusAndGrant();
  void capabilitiesTerminalBackendSelectsAndReloads();
  void capabilitiesConnectorConnectPollsOperationAndReloads();
  void capabilitiesLateConnectorListIsIgnoredAfterPageDestroyed();
  void capabilitiesMergesAccountOperationIntoConnectorRow();
  void botsEditModelDescriptionSoulAndExportArchive();
  void pluginSettingsSaveSchemaAndSecrets();
  void webhooksEnableCreateAndToggleSubscriptions();
  void customEndpointEditorValidatesAndSaves();
  void artifactsSpotEditorSavesAndShowsImages();
  void artifactsPreviewRendersMarkdownHtmlAndCsv();
  void artifactsScrollSurvivesPageRebuild();
  void notificationsPersistKindsAndSendTest();
  void preferencesApplyKeybindsAndFontScale();
  void appearancePagePersistsDisplayPreferences();
  void appearancePreferencesClampAndApplyTheme();
  void voiceSectionExposesAudioDevices();
  void fontScaleScalesThemeFonts();
  void delegationEditsModelFallbacksAndProfileRoutes();
  void modelSectionEditsOrderedFallbackChain();
  void modelSectionEditsProviderModelAndReasoning();
  void modelAuxiliaryAndMoaPagesRender();
  void voiceFieldsFollowSelectedProviders();
  void memorySectionShowsLayersAndProviderConfig();
  void settingsSubpagesDeepLinkAndSearchFields();
};

void SettingsViewTest::voiceFieldsFollowSelectedProviders() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  auto snapshot = Snapshot();
  snapshot.config.insert(QStringLiteral("voice"),
                         QJsonObject{{QStringLiteral("voice_chat_mode"),
                                      QStringLiteral("chained")}});
  snapshot.config.insert(
      QStringLiteral("tts"),
      QJsonObject{
          {QStringLiteral("provider"), QStringLiteral("edge")},
          {QStringLiteral("edge"),
           QJsonObject{
               {QStringLiteral("voice"), QStringLiteral("en-US-AriaNeural")}}},
          {QStringLiteral("openai"),
           QJsonObject{{QStringLiteral("voice"), QStringLiteral("nova")}}}});
  snapshot.config.insert(
      QStringLiteral("stt"),
      QJsonObject{
          {QStringLiteral("enabled"), false},
          {QStringLiteral("provider"), QStringLiteral("local")},
          {QStringLiteral("local"),
           QJsonObject{{QStringLiteral("model"), QStringLiteral("base")}}}});
  view.show();
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  int voice_row = -1;
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("voice")) {
      voice_row = i;
      break;
    }
  }
  QVERIFY(voice_row >= 0);
  nav->setCurrentRow(voice_row);

  auto chip = [&view](const QString& id) {
    return view.findChild<QPushButton*>(
        QStringLiteral("settingsSubpage_voice_") + id);
  };
  auto* speech = chip(QStringLiteral("speech"));
  auto* transcription = chip(QStringLiteral("transcription"));
  QVERIFY(speech != nullptr);
  QVERIFY(transcription != nullptr);
  QVERIFY(!speech->isChecked());

  speech->click();
  auto* edge =
      view.findChild<QWidget*>(QStringLiteral("setting_tts_edge_voice"));
  auto* openai =
      view.findChild<QWidget*>(QStringLiteral("setting_tts_openai_voice"));
  QVERIFY(edge != nullptr);
  QVERIFY(openai != nullptr);
  QVERIFY(edge->isVisible());
  QVERIFY(!openai->isVisible());

  // The provider list is a closed picker using the desktop's enum values.
  auto* provider =
      view.findChild<QComboBox*>(QStringLiteral("settingControl_tts_provider"));
  QVERIFY(provider != nullptr);
  QVERIFY(provider->findData(QStringLiteral("edge")) >= 0);
  QVERIFY(provider->findData(QStringLiteral("elevenlabs")) >= 0);
  provider->setCurrentIndex(provider->findData(QStringLiteral("openai")));
  QVERIFY(!edge->isVisible());
  QVERIFY(openai->isVisible());

  // Transcription exposes only the STT family.
  chip(QStringLiteral("transcription"))->click();
  auto* local =
      view.findChild<QWidget*>(QStringLiteral("setting_stt_local_model"));
  auto* stt_enabled =
      view.findChild<QWidget*>(QStringLiteral("setting_stt_enabled"));
  QVERIFY(local != nullptr);
  QVERIFY(stt_enabled != nullptr);
  QVERIFY(!local->isVisible());
  QVERIFY(stt_enabled->isVisible());

  // Turning STT on reveals only the selected local provider's model.
  auto* enabled = stt_enabled->findChild<QCheckBox*>();
  QVERIFY(enabled != nullptr);
  enabled->setChecked(true);
  QVERIFY(local->isVisible());
  auto* stt_provider =
      view.findChild<QComboBox*>(QStringLiteral("settingControl_stt_provider"));
  QVERIFY(stt_provider != nullptr);
  stt_provider->setCurrentIndex(
      stt_provider->findData(QStringLiteral("openai")));
  QVERIFY(!local->isVisible());

  // Voice/model names accept provider aliases not yet in the suggestion list.
  chip(QStringLiteral("speech"))->click();
  auto* openai_voice = view.findChild<QComboBox*>(
      QStringLiteral("settingControl_tts_openai_voice"));
  QVERIFY(openai_voice != nullptr);
  QVERIFY(openai_voice->isEditable());
  openai_voice->setCurrentText(QStringLiteral("custom-clone-id"));
  QCOMPARE(openai_voice->currentText(), QStringLiteral("custom-clone-id"));
}

void SettingsViewTest::memorySectionShowsLayersAndProviderConfig() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  auto snapshot = Snapshot();
  snapshot.name = QStringLiteral("work");
  snapshot.config.insert(
      QStringLiteral("memory"),
      QJsonObject{{QStringLiteral("memory_enabled"), true},
                  {QStringLiteral("user_profile_enabled"), false},
                  {QStringLiteral("provider"), QStringLiteral("byterover")}});

  const QJsonObject builtin_files{{QStringLiteral("memory"), 2048},
                                  {QStringLiteral("user"), 0}};
  const QJsonArray providers{
      QJsonObject{{QStringLiteral("name"), QStringLiteral("byterover")},
                  {QStringLiteral("status"), QStringLiteral("ready")}}};
  hermes::ui::PageState memory;
  memory.loaded = true;
  memory.data.insert(
      QStringLiteral("status"),
      QJsonObject{{QStringLiteral("active"), QStringLiteral("byterover")},
                  {QStringLiteral("providers"), providers},
                  {QStringLiteral("builtin_files"), builtin_files}});
  snapshot.pages.insert(QStringLiteral("memory"), memory);

  QStringList requested;
  view.on_page_request = [&requested](const QString& page) {
    requested.append(page);
  };
  view.show();
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  int memory_row = -1;
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("memory")) {
      memory_row = i;
      break;
    }
  }
  QVERIFY(memory_row >= 0);
  nav->setCurrentRow(memory_row);

  auto* active = view.findChild<QWidget*>(QStringLiteral("memoryLayerActive"));
  auto* builtin =
      view.findChild<QWidget*>(QStringLiteral("memoryLayerBuiltin"));
  QVERIFY(active != nullptr);
  QVERIFY(builtin != nullptr);
  bool saw_active = false;
  for (QLabel* label : active->findChildren<QLabel*>()) {
    saw_active = saw_active ||
                 label->text().contains(QStringLiteral("byterover")) ||
                 label->text().contains(QStringLiteral("Ready"));
  }
  QVERIFY(saw_active);
  bool saw_builtin = false;
  for (QLabel* label : builtin->findChildren<QLabel*>()) {
    saw_builtin =
        saw_builtin || label->text().contains(QStringLiteral("MEMORY.md only"));
  }
  QVERIFY(saw_builtin);
  QTRY_VERIFY(requested.contains(QStringLiteral("memory:byterover")));

  const QJsonArray fields{
      QJsonObject{{QStringLiteral("key"), QStringLiteral("workspace")},
                  {QStringLiteral("label"), QStringLiteral("Workspace")},
                  {QStringLiteral("kind"), QStringLiteral("text")},
                  {QStringLiteral("group"), QStringLiteral("General")},
                  {QStringLiteral("value"), QStringLiteral("default")},
                  {QStringLiteral("description"),
                   QStringLiteral("Memory workspace name")}},
      QJsonObject{{QStringLiteral("key"), QStringLiteral("save_messages")},
                  {QStringLiteral("label"), QStringLiteral("Save messages")},
                  {QStringLiteral("kind"), QStringLiteral("bool")},
                  {QStringLiteral("group"), QStringLiteral("General")},
                  {QStringLiteral("value"), QStringLiteral("false")}},
      QJsonObject{{QStringLiteral("key"), QStringLiteral("api_key")},
                  {QStringLiteral("label"), QStringLiteral("API key")},
                  {QStringLiteral("kind"), QStringLiteral("secret")},
                  {QStringLiteral("group"), QStringLiteral("Credentials")},
                  {QStringLiteral("value"), QStringLiteral("")},
                  {QStringLiteral("is_set"), true}}};
  const QJsonObject provider_config{
      {QStringLiteral("name"), QStringLiteral("byterover")},
      {QStringLiteral("label"), QStringLiteral("Byterover")},
      {QStringLiteral("fields"), fields}};
  hermes::ui::PageState provider;
  provider.loaded = true;
  provider.data.insert(QStringLiteral("config"), provider_config);
  snapshot.pages.insert(QStringLiteral("memory:byterover"), provider);
  snapshot.revision += 1;
  view.SetSnapshot(snapshot);

  QVERIFY(view.findChild<QLabel*>(QStringLiteral("memoryProviderHeading")) !=
          nullptr);
  auto* text_row =
      view.findChild<QWidget*>(QStringLiteral("memoryField_workspace"));
  auto* bool_control =
      view.findChild<QCheckBox*>(QStringLiteral("memoryControl_save_messages"));
  auto* secret =
      view.findChild<QLineEdit*>(QStringLiteral("memoryControl_api_key"));
  QVERIFY(text_row != nullptr);
  QVERIFY(bool_control != nullptr);
  QVERIFY(secret != nullptr);
  QVERIFY(secret->echoMode() == QLineEdit::Password);
  QVERIFY(secret->placeholderText().contains(QStringLiteral("Leave blank")));

  QString acted_page;
  QString acted_method;
  QString acted_path;
  QJsonObject acted_body;
  view.on_page_action = [&](const QString& page, const QString& method,
                            const QString& path, const QJsonObject& body) {
    acted_page = page;
    acted_method = method;
    acted_path = path;
    acted_body = body;
  };
  bool_control->setChecked(true);
  QCOMPARE(acted_page, QStringLiteral("memory:byterover"));
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QVERIFY(acted_path.contains(
      QStringLiteral("/api/memory/providers/byterover/config")));
  QVERIFY(acted_path.contains(QStringLiteral("surface=declared")));
  QVERIFY(acted_path.contains(QStringLiteral("profile=work")));
  QCOMPARE(acted_body.value(QStringLiteral("values"))
               .toObject()
               .value(QStringLiteral("save_messages"))
               .toString(),
           QStringLiteral("true"));

  auto* workspace =
      view.findChild<QLineEdit*>(QStringLiteral("memoryControl_workspace"));
  QVERIFY(workspace != nullptr);
  workspace->setText(QStringLiteral("team-workspace"));
  QMetaObject::invokeMethod(workspace, "editingFinished");
  QCOMPARE(acted_body.value(QStringLiteral("values"))
               .toObject()
               .value(QStringLiteral("workspace"))
               .toString(),
           QStringLiteral("team-workspace"));
}

void SettingsViewTest::initTestCase() {
  // Preferences live in QSettings; redirect the store so the suite never
  // touches the user's real com.hermes.hermes-ui domain.
  qputenv(
      "HERMES_UI_PREFS",
      (QDir::tempPath() + QStringLiteral("/hermes-ui-test-prefs")).toUtf8());
}

void SettingsViewTest::rendersDesktopSectionsAndSavesSparsePatch() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QJsonObject saved;
  view.on_save = [&saved](const QJsonObject& patch) { saved = patch; };
  view.show();
  view.SetSnapshot(Snapshot());

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  // Nine config sections + four application links. The desktop SECTIONS table
  // is the source of the nav list, so this is a cheap drift guard.
  QVERIFY(nav->count() >= 13);

  int chat_row = -1;
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() == QStringLiteral("chat")) {
      chat_row = i;
      break;
    }
  }
  QVERIFY(chat_row >= 0);
  nav->setCurrentRow(chat_row);

  auto* row = view.findChild<QWidget*>(
      QStringLiteral("setting_display_show_reasoning"));
  QVERIFY(row != nullptr);
  auto* box = row->findChild<QCheckBox*>();
  QVERIFY(box != nullptr);
  box->setChecked(false);

  QTest::qWait(800);
  QVERIFY(saved.contains(QStringLiteral("display")));
  QCOMPARE(saved.value(QStringLiteral("display"))
               .toObject()
               .value(QStringLiteral("show_reasoning"))
               .toBool(),
           false);
}

void SettingsViewTest::rendersAboutAndSessionPages() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  auto snapshot = Snapshot();
  snapshot.gateway_version = QStringLiteral("0.23.0");
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Session one");
  session.model = QStringLiteral("m1");
  session.source = QStringLiteral("desktop");
  snapshot.sessions.push_back(session);
  view.show();
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  auto row_for = [nav](const QString& id) {
    for (int i = 0; i < nav->count(); ++i) {
      if (nav->item(i)->data(Qt::UserRole).toString() == id) {
        return i;
      }
    }
    return -1;
  };

  nav->setCurrentRow(row_for(QStringLiteral("about")));
  bool saw_gateway = false;
  for (QLabel* label : view.findChildren<QLabel*>()) {
    if (label->text().contains(QStringLiteral("0.23.0"))) {
      saw_gateway = true;
    }
  }
  QVERIFY(saw_gateway);

  nav->setCurrentRow(row_for(QStringLiteral("sessions")));
  auto* list = view.findChild<QListWidget*>(QStringLiteral("settingsSessions"));
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), 1);
  QCOMPARE(list->item(0)->text(), QStringLiteral("Session one"));
}

namespace {

// One page's worth of the real gateway payload shapes.
hermes::ui::ProfileSnapshot DataSnapshot() {
  hermes::ui::ProfileSnapshot snapshot = Snapshot();
  snapshot.gateway_version = QStringLiteral("0.23.0");
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Session one");
  snapshot.sessions.push_back(session);

  const QJsonObject env{
      {QStringLiteral("OPENAI_API_KEY"),
       QJsonObject{
           {QStringLiteral("is_set"), true},
           {QStringLiteral("redacted_value"), QStringLiteral("sk-…abcd")},
           {QStringLiteral("category"), QStringLiteral("llm")},
           {QStringLiteral("provider"), QStringLiteral("openai")},
           {QStringLiteral("provider_primary"), true}}},
  };

  hermes::ui::PageState providers;
  providers.loaded = true;
  providers.data = QJsonObject{
      {QStringLiteral("options"),
       QJsonObject{{QStringLiteral("providers"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("name"), QStringLiteral("OpenAI")},
                        {QStringLiteral("slug"), QStringLiteral("openai")},
                        {QStringLiteral("models"),
                         QJsonArray{QStringLiteral("gpt-5"),
                                    QStringLiteral("gpt-5-mini")}}}}}}},
      {QStringLiteral("endpoints"),
       QJsonObject{{QStringLiteral("endpoints"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("ep1")},
                        {QStringLiteral("name"), QStringLiteral("Local vLLM")},
                        {QStringLiteral("base_url"),
                         QStringLiteral("http://127.0.0.1:8000/v1")}}}}}},
      {QStringLiteral("env"), env},
  };
  snapshot.pages.insert(QStringLiteral("providers"), providers);
  snapshot.pages.insert(QStringLiteral("keys"), providers);

  hermes::ui::PageState vault;
  vault.loaded = true;
  vault.data = QJsonObject{
      {QStringLiteral("pool"),
       QJsonObject{
           {QStringLiteral("providers"),
            QJsonArray{QJsonObject{
                {QStringLiteral("provider"), QStringLiteral("anthropic")},
                {QStringLiteral("entries"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("index"), 1},
                     {QStringLiteral("label"), QStringLiteral("work")},
                     {QStringLiteral("source"), QStringLiteral("manual")},
                     {QStringLiteral("token_preview"),
                      QStringLiteral("sk-…1234")},
                     {QStringLiteral("last_status"),
                      QStringLiteral("ok")}}}}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("vault"), vault);

  hermes::ui::PageState gateway;
  gateway.loaded = true;
  gateway.data = QJsonObject{
      {QStringLiteral("access"),
       QJsonObject{
           {QStringLiteral("enabled"), false},
           {QStringLiteral("mode"), QStringLiteral("off")},
           {QStringLiteral("port"), 8765},
           {QStringLiteral("public_url"), QString()},
           {QStringLiteral("lan_address"), QStringLiteral("192.168.1.5")},
           {QStringLiteral("gateway"), QStringLiteral("running")},
           {QStringLiteral("nous_signed_in"), true}}},
  };
  snapshot.pages.insert(QStringLiteral("gateway"), gateway);

  hermes::ui::PageState plugins;
  plugins.loaded = true;
  plugins.data = QJsonObject{
      {QStringLiteral("plugins"),
       QJsonArray{QJsonObject{
           {QStringLiteral("name"), QStringLiteral("core")},
           {QStringLiteral("label"), QStringLiteral("Core")},
           {QStringLiteral("version"), QStringLiteral("1.0.0")},
           {QStringLiteral("description"), QStringLiteral("Built-in tools")}}}},
      {QStringLiteral("hub"),
       QJsonObject{
           {QStringLiteral("plugins"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("core")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")},
                    {QStringLiteral("description"),
                     QStringLiteral("Built-in tools")},
                    {QStringLiteral("runtime_status"),
                     QStringLiteral("enabled")},
                    {QStringLiteral("can_update_git"), true},
                    {QStringLiteral("can_remove"), true}},
                QJsonObject{
                    {QStringLiteral("name"), QStringLiteral("browserbase")},
                    {QStringLiteral("version"), QStringLiteral("1.0.0")},
                    {QStringLiteral("description"),
                     QStringLiteral("Cloud browser")},
                    {QStringLiteral("runtime_status"),
                     QStringLiteral("disabled")}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("plugins"), plugins);

  hermes::ui::PageState plugincatalog;
  plugincatalog.loaded = true;
  plugincatalog.data.insert(
      QStringLiteral("catalog"),
      QJsonObject{
          {QStringLiteral("entries"),
           QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("0bull")},
               {QStringLiteral("description"), QStringLiteral("Phone control")},
               {QStringLiteral("installed"), false}}}}});
  snapshot.pages.insert(QStringLiteral("plugincatalog"), plugincatalog);

  hermes::ui::PageState billing;
  billing.loaded = true;
  billing.data = QJsonObject{
      {QStringLiteral("portal"),
       QJsonObject{
           {QStringLiteral("logged_in"), true},
           {QStringLiteral("portal_url"),
            QStringLiteral("https://portal.nousresearch.com")},
           {QStringLiteral("plan"), QStringLiteral("Pro")},
           {QStringLiteral("features"),
            QJsonArray{QJsonObject{
                {QStringLiteral("label"), QStringLiteral("Tool gateway")},
                {QStringLiteral("state"),
                 QStringLiteral("via Nous Portal")}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("billing"), billing);

  hermes::ui::PageState sessions;
  sessions.loaded = true;
  sessions.data = QJsonObject{
      {QStringLiteral("stats"),
       QJsonObject{{QStringLiteral("total"), 42},
                   {QStringLiteral("active_store"), 40},
                   {QStringLiteral("archived"), 2},
                   {QStringLiteral("messages"), 900}}},
  };
  snapshot.pages.insert(QStringLiteral("sessions"), sessions);

  hermes::ui::PageState about;
  about.loaded = true;
  about.data = QJsonObject{
      {QStringLiteral("status"),
       QJsonObject{{QStringLiteral("version"), QStringLiteral("0.23.0")},
                   {QStringLiteral("status"), QStringLiteral("ready")}}},
      {QStringLiteral("update"),
       QJsonObject{
           {QStringLiteral("install_method"), QStringLiteral("git")},
           {QStringLiteral("current_version"), QStringLiteral("0.23.0")},
           {QStringLiteral("update_available"), false},
           {QStringLiteral("can_apply"), true},
           {QStringLiteral("message"), QStringLiteral("Up to date")}}},
  };
  snapshot.pages.insert(QStringLiteral("about"), about);

  hermes::ui::PageState update;
  update.loaded = true;
  update.data = QJsonObject{
      {QStringLiteral("update"),
       QJsonObject{
           {QStringLiteral("install_method"), QStringLiteral("git")},
           {QStringLiteral("current_version"), QStringLiteral("0.23.0")},
           {QStringLiteral("update_available"), false},
           {QStringLiteral("can_apply"), true},
           {QStringLiteral("message"), QStringLiteral("Up to date")}}},
  };
  snapshot.pages.insert(QStringLiteral("update"), update);

  hermes::ui::PageState cron;
  cron.loaded = true;
  cron.data = QJsonObject{
      {QStringLiteral("jobs"),
       QJsonArray{QJsonObject{
           {QStringLiteral("id"), QStringLiteral("job1")},
           {QStringLiteral("name"), QStringLiteral("Nightly digest")},
           {QStringLiteral("schedule"), QStringLiteral("0 3 * * *")},
           {QStringLiteral("next_run"), QStringLiteral("2026-11-11T03:00Z")},
           {QStringLiteral("enabled"), true}}}},
      {QStringLiteral("delivery"),
       QJsonObject{{QStringLiteral("targets"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("id"), QStringLiteral("local")},
                        {QStringLiteral("name"),
                         QStringLiteral("Local (save only)")}}}}}},
      {QStringLiteral("runs"),
       QJsonObject{{QStringLiteral("job1"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("status"), QStringLiteral("completed")},
                        {QStringLiteral("started_at"),
                         QStringLiteral("2026-11-10T03:00Z")}}}}}},
      {QStringLiteral("blueprints"),
       QJsonObject{
           {QStringLiteral("blueprints"),
            QJsonArray{QJsonObject{
                {QStringLiteral("key"), QStringLiteral("morning-brief")},
                {QStringLiteral("title"), QStringLiteral("Morning briefing")},
                {QStringLiteral("description"), QStringLiteral("Daily brief")},
                {QStringLiteral("fields"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("time")},
                     {QStringLiteral("type"), QStringLiteral("time")},
                     {QStringLiteral("label"), QStringLiteral("What time?")},
                     {QStringLiteral("default"),
                      QStringLiteral("08:00")}}}}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("cron"), cron);

  hermes::ui::PageState localmodels;
  localmodels.loaded = true;
  localmodels.data = QJsonObject{
      {QStringLiteral("status"),
       QJsonObject{
           {QStringLiteral("enabled"), false},
           {QStringLiteral("runtime_installed"), true},
           {QStringLiteral("runtime_backend"), QStringLiteral("llama.cpp")},
           {QStringLiteral("server_running"), false},
           {QStringLiteral("models_dir"),
            QStringLiteral("/Users/x/.hermes/models")}}},
      {QStringLiteral("hardware"),
       QJsonObject{{QStringLiteral("uma"), true},
                   {QStringLiteral("vram_total_bytes"),
                    static_cast<double>(16LL * 1024 * 1024 * 1024)},
                   {QStringLiteral("ram_available_bytes"),
                    static_cast<double>(4LL * 1024 * 1024 * 1024)}}},
      {QStringLiteral("catalog"),
       QJsonObject{
           {QStringLiteral("models"),
            QJsonArray{QJsonObject{
                {QStringLiteral("id"), QStringLiteral("lfm2.5-8b")},
                {QStringLiteral("display_name"), QStringLiteral("LFM2.5 8B")},
                {QStringLiteral("size_label"), QStringLiteral("4.9 GB")},
                {QStringLiteral("downloaded"), true},
                {QStringLiteral("fits"), true}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("localmodels"), localmodels);

  hermes::ui::PageState delegation;
  delegation.loaded = true;
  delegation.data = QJsonObject{
      {QStringLiteral("routing"),
       QJsonObject{
           {QStringLiteral("configured"), QStringLiteral("default")},
           {QStringLiteral("backends"),
            QJsonArray{QJsonObject{
                {QStringLiteral("name"), QStringLiteral("default")},
                {QStringLiteral("label"), QStringLiteral("Default")},
                {QStringLiteral("available"), true}}}},
           {QStringLiteral("model_map"),
            QJsonObject{{QStringLiteral("options"), QJsonArray()}}},
           {QStringLiteral("profile_routes"),
            QJsonObject{{QStringLiteral("profiles"),
                         QJsonArray{QJsonObject{{QStringLiteral("name"),
                                                 QStringLiteral("ceo")}}}}}},
           {QStringLiteral("profiles"),
            QJsonArray{QStringLiteral("ceo"), QStringLiteral("trader")}}}},
  };
  snapshot.pages.insert(QStringLiteral("delegation"), delegation);

  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  capabilities.data = QJsonObject{
      {QStringLiteral("toolsets"),
       QJsonObject{{QStringLiteral("toolsets"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("name"), QStringLiteral("terminal")},
                        {QStringLiteral("label"), QStringLiteral("Terminal")},
                        {QStringLiteral("enabled"), true}}}}}},
      {QStringLiteral("skills"),
       QJsonObject{
           {QStringLiteral("skills"),
            QJsonArray{QJsonObject{
                {QStringLiteral("name"), QStringLiteral("github-pr-workflow")},
                {QStringLiteral("description"), QStringLiteral("PRs")}}}}}},
      {QStringLiteral("mcp"),
       QJsonObject{{QStringLiteral("servers"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("name"), QStringLiteral("memory")},
                        {QStringLiteral("enabled"), true}}}}}},
  };
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageState prefs;
  prefs.loaded = true;
  prefs.data = QJsonObject{
      {QStringLiteral("prefs"),
       QJsonObject{
           {QStringLiteral("values"),
            QJsonObject{
                {QStringLiteral("notifications.enabled"), QStringLiteral("1")},
                {QStringLiteral("notifications.kind.turnDone"),
                 QStringLiteral("1")},
                {QStringLiteral("native.font_scale"), QStringLiteral("1.10")},
                {QStringLiteral("keybind.command_palette"),
                 QStringLiteral("Ctrl+Shift+P")}}}}},
  };
  snapshot.pages.insert(QStringLiteral("notifications"), prefs);
  snapshot.pages.insert(QStringLiteral("keybinds"), prefs);
  snapshot.pages.insert(QStringLiteral("appearance"), prefs);
  return snapshot;
}

}  // namespace

void SettingsViewTest::rendersEveryDataPageFromSnapshot() {
  hermes::ui::SetNativePref(QStringLiteral("keybind.command_palette"),
                            QStringLiteral("Ctrl+Shift+P"));

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  view.show();
  view.SetSnapshot(DataSnapshot());

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  auto show_page = [nav](const QString& id) {
    for (int i = 0; i < nav->count(); ++i) {
      if (nav->item(i)->data(Qt::UserRole).toString() == id) {
        nav->setCurrentRow(i);
        return;
      }
    }
    QFAIL(qUtf8Printable(QStringLiteral("missing nav row %1").arg(id)));
  };

  const QList<QPair<QString, QString>> expected{
      {QStringLiteral("providers"), QStringLiteral("providerRow_openai")},
      {QStringLiteral("providers"), QStringLiteral("providerSetKey_openai")},
      {QStringLiteral("keys"), QStringLiteral("envRow_OPENAI_API_KEY")},
      {QStringLiteral("vault"), QStringLiteral("vaultRow_anthropic_1")},
      {QStringLiteral("gateway"), QStringLiteral("gatewayStart")},
      {QStringLiteral("notifications"),
       QStringLiteral("notifyToggle_notifications.enabled")},
      {QStringLiteral("plugins"), QStringLiteral("pluginRow_core")},
      {QStringLiteral("plugins"), QStringLiteral("pluginToggle_core")},
      {QStringLiteral("plugins"), QStringLiteral("catalogInstall_0bull")},
      {QStringLiteral("billing"), QStringLiteral("portalOpen")},
      {QStringLiteral("sessions"), QStringLiteral("sessionDelete")},
      {QStringLiteral("keybinds"),
       QStringLiteral("keybindValue_command_palette")},
      {QStringLiteral("keybinds"), QStringLiteral("keybindEdit_profile_4")},
      {QStringLiteral("keybinds"), QStringLiteral("keybindReset_session_next")},
      {QStringLiteral("cron"), QStringLiteral("cronRow_job1")},
      {QStringLiteral("cron"), QStringLiteral("cronRuns_job1")},
      {QStringLiteral("cron"), QStringLiteral("cronCreate")},
      {QStringLiteral("cron"),
       QStringLiteral("cronBlueprintUse_morning-brief")},
      {QStringLiteral("localmodels"), QStringLiteral("localModelsServer")},
      {QStringLiteral("localmodels"),
       QStringLiteral("localModelActivate_lfm2.5-8b")},
      {QStringLiteral("delegation"), QStringLiteral("delegationBackend")},
      {QStringLiteral("delegation"),
       QStringLiteral("delegationRouteRemove_ceo")},
      {QStringLiteral("capabilities"), QStringLiteral("mcpRow_memory")},
      {QStringLiteral("capabilities"), QStringLiteral("mcpRemove_memory")},
      {QStringLiteral("capabilities"), QStringLiteral("skillNew")},
      {QStringLiteral("capabilities"), QStringLiteral("skillHubBrowse")},
      {QStringLiteral("capabilities"), QStringLiteral("skillHubUpdate")},
      {QStringLiteral("about"), QStringLiteral("updateApply")},
      {QStringLiteral("appearance"), QStringLiteral("appearanceFontScale")},
  };
  for (const auto& row : expected) {
    show_page(row.first);
    QVERIFY2(
        view.findChild<QWidget*>(row.second) != nullptr,
        qUtf8Printable(
            QStringLiteral("%1 missing on %2").arg(row.second, row.first)));
  }

  // The keybind override is rendered, not the default Ctrl+K.
  show_page(QStringLiteral("keybinds"));
  auto* value =
      view.findChild<QLabel*>(QStringLiteral("keybindValue_command_palette"));
  QVERIFY(value != nullptr);
  QCOMPARE(value->text(), QStringLiteral("Ctrl+Shift+P"));

  // Reset returns the row to its default and clears the stored override.
  auto* reset = view.findChild<QPushButton*>(
      QStringLiteral("keybindReset_command_palette"));
  QVERIFY(reset != nullptr);
  QVERIFY(reset->isEnabled());
  reset->click();
  QCOMPARE(value->text(), QStringLiteral("Ctrl+K"));
  QVERIFY(!reset->isEnabled());
  QVERIFY(hermes::ui::NativePref(QStringLiteral("keybind.command_palette"))
              .isEmpty());
}

void SettingsViewTest::pageActionsRouteThroughWorkerCallback() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QString page;
  QString method;
  QString path;
  QJsonObject body;
  view.on_page_action = [&page, &method, &path, &body](
                            const QString& page_id, const QString& verb,
                            const QString& route, const QJsonObject& payload) {
    page = page_id;
    method = verb;
    path = route;
    body = payload;
  };
  view.show();
  view.SetSnapshot(DataSnapshot());

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() == QStringLiteral("cron")) {
      nav->setCurrentRow(i);
      break;
    }
  }
  auto* pause = view.findChild<QPushButton*>(QStringLiteral("cronToggle_job1"));
  QVERIFY(pause != nullptr);
  pause->click();
  QCOMPARE(page, QStringLiteral("cron"));
  QCOMPARE(method, QStringLiteral("POST"));
  QCOMPARE(path, QStringLiteral("/api/cron/jobs/job1/pause"));

  auto* edit = view.findChild<QPushButton*>(QStringLiteral("cronEdit_job1"));
  QVERIFY(edit != nullptr);
  edit->click();
  auto* dialog =
      view.findChild<QDialog*>(QStringLiteral("cronEditDialog_job1"));
  QVERIFY(dialog != nullptr);
  auto* edit_name =
      dialog->findChild<QLineEdit*>(QStringLiteral("cronEditName"));
  auto* edit_schedule =
      dialog->findChild<QLineEdit*>(QStringLiteral("cronEditSchedule"));
  QVERIFY(edit_name != nullptr);
  QVERIFY(edit_schedule != nullptr);
  edit_name->setText(QStringLiteral("Morning digest"));
  edit_schedule->setText(QStringLiteral("0 4 * * *"));
  auto* submit =
      dialog->findChild<QPushButton*>(QStringLiteral("cronEditSubmit"));
  QVERIFY(submit != nullptr);
  submit->click();
  QCOMPARE(page, QStringLiteral("cron"));
  QCOMPARE(method, QStringLiteral("PUT"));
  QCOMPARE(path, QStringLiteral("/api/cron/jobs/job1"));
  const QJsonObject updates = body.value(QStringLiteral("updates")).toObject();
  QCOMPARE(updates.value(QStringLiteral("name")).toString(),
           QStringLiteral("Morning digest"));
  QCOMPARE(updates.value(QStringLiteral("schedule")).toString(),
           QStringLiteral("0 4 * * *"));
  // Unchanged fields stay out of the sparse update.
  QVERIFY(!updates.contains(QStringLiteral("prompt")));

  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("gateway")) {
      nav->setCurrentRow(i);
      break;
    }
  }
  auto* start = view.findChild<QPushButton*>(QStringLiteral("gatewayStart"));
  QVERIFY(start != nullptr);
  start->click();
  QCOMPARE(page, QStringLiteral("gateway"));
  QCOMPARE(path, QStringLiteral("/api/webapp-access/start"));
  QCOMPARE(body.value(QStringLiteral("mode")).toString(),
           QStringLiteral("local"));
}

void SettingsViewTest::scheduledTaskRowsWrapAndUseIconActions() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState cron;
  cron.loaded = true;
  const QString long_name =
      QStringLiteral("Nightly digest for every active workspace and team");
  const QString long_detail = QStringLiteral(
      "0 3 * * * · next 2026-11-11T03:00Z · enabled · "
      "→ Local (save only) · a very long delivery target name that wraps");
  cron.data = QJsonObject{
      {QStringLiteral("jobs"),
       QJsonArray{QJsonObject{
           {QStringLiteral("id"), QStringLiteral("job1")},
           {QStringLiteral("name"), long_name},
           {QStringLiteral("schedule"), QStringLiteral("0 3 * * *")},
           {QStringLiteral("enabled"), true}}}},
      {QStringLiteral("runs"),
       QJsonObject{{QStringLiteral("job1"),
                    QJsonArray{QJsonObject{
                        {QStringLiteral("status"), QStringLiteral("completed")},
                        {QStringLiteral("started_at"),
                         QStringLiteral("2026-11-10T03:00Z")},
                        {QStringLiteral("title"),
                         QStringLiteral("A long run summary that also has to "
                                        "wrap inside the card")}}}}}}};
  snapshot.pages.insert(QStringLiteral("cron"), cron);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Dark();
  context.snapshot = &snapshot;
  context.action = [](const QString&, const QString&, const QString&,
                      const QJsonObject&) {};

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("cron"), context, nullptr);
  QVERIFY(page != nullptr);
  page->resize(620, 720);
  page->show();
  QTest::qWait(20);

  auto* row = page->findChild<QWidget*>(QStringLiteral("cronRow_job1"));
  auto* text = page->findChild<QWidget*>(QStringLiteral("cronText_job1"));
  auto* actions = page->findChild<QWidget*>(QStringLiteral("cronActions_job1"));
  auto* name = page->findChild<QLabel*>(QStringLiteral("cronName_job1"));
  auto* detail = page->findChild<QLabel*>(QStringLiteral("cronDetail_job1"));
  auto* runs = page->findChild<QLabel*>(QStringLiteral("cronRuns_job1"));
  QVERIFY(row != nullptr);
  QVERIFY(text != nullptr);
  QVERIFY(actions != nullptr);
  QVERIFY(name != nullptr);
  QVERIFY(detail != nullptr);
  QVERIFY(runs != nullptr);
  QVERIFY(name->wordWrap());
  QVERIFY(detail->wordWrap());
  QVERIFY(runs->wordWrap());
  QCOMPARE(name->text(), long_name);

  // The text column is capped at 80% of the card, and the actions sit below
  // it instead of sharing a horizontal row with the wrapping copy.
  QVERIFY2(text->width() <= row->width() * 8 / 10 + 2,
           qUtf8Printable(QStringLiteral("text %1 of row %2")
                              .arg(text->width())
                              .arg(row->width())));
  QVERIFY(actions->geometry().top() >= text->geometry().bottom());

  const QList<QPair<QString, QString>> expected{
      {QStringLiteral("cronToggle_job1"), QStringLiteral("Pause task")},
      {QStringLiteral("cronEdit_job1"), QStringLiteral("Edit task")},
      {QStringLiteral("cronTrigger_job1"), QStringLiteral("Run task now")},
      {QStringLiteral("cronDelete_job1"), QStringLiteral("Delete task")},
  };
  for (const auto& entry : expected) {
    auto* button = page->findChild<QPushButton*>(entry.first);
    QVERIFY2(button != nullptr, qUtf8Printable(entry.first));
    QCOMPARE(button->parentWidget(), actions);
    QCOMPARE(button->text().size(), 1);
    QCOMPARE(button->toolTip(), entry.second);
    QVERIFY(button->accessibleName() == entry.second);
    // The global QSS sets the UI font on QWidget, which overrides setFont();
    // without this the codicon codepoint renders as the fallback .notdef
    // striped box instead of an icon.
    QVERIFY2(
        button->styleSheet().contains(QStringLiteral("font-family: 'codicon'")),
        qUtf8Printable(button->styleSheet()));
  }
  delete page;
}

void SettingsViewTest::capabilityToolsetsAreCheckboxes() {
  hermes::ui::ProfileSnapshot snapshot;
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  capabilities.data.insert(
      QStringLiteral("toolsets"),
      QJsonArray{
          QJsonObject{{QStringLiteral("name"), QStringLiteral("terminal")},
                      {QStringLiteral("label"), QStringLiteral("Terminal")},
                      {QStringLiteral("enabled"), true},
                      {QStringLiteral("tools"), QJsonArray{1, 2}}},
          QJsonObject{{QStringLiteral("name"), QStringLiteral("video")},
                      {QStringLiteral("label"), QStringLiteral("Video")},
                      {QStringLiteral("enabled"), false},
                      {QStringLiteral("tools"), QJsonArray{1}}}});
  capabilities.data.insert(
      QStringLiteral("skills"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("name"), QStringLiteral("docs")},
              {QStringLiteral("description"), QStringLiteral("Reference docs")},
              {QStringLiteral("enabled"), true},
              {QStringLiteral("locked"), false},
              {QStringLiteral("provenance"), QStringLiteral("bundled")},
              {QStringLiteral("usage"), 3}},
          QJsonObject{
              {QStringLiteral("name"), QStringLiteral("release-notes")},
              {QStringLiteral("description"), QStringLiteral("Hub skill")},
              {QStringLiteral("enabled"), true},
              {QStringLiteral("locked"), false},
              {QStringLiteral("provenance"), QStringLiteral("hub")}}});
  capabilities.data.insert(
      QStringLiteral("mcp"),
      QJsonObject{
          {QStringLiteral("servers"),
           QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("memory")},
               {QStringLiteral("transport"), QStringLiteral("http")},
               {QStringLiteral("url"), QStringLiteral("https://mcp.example")},
               {QStringLiteral("auth"), QStringLiteral("oauth")},
               {QStringLiteral("source"), QStringLiteral("config")},
               {QStringLiteral("enabled"), true}}}}});
  snapshot.name = QStringLiteral("work");
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Dark();
  context.snapshot = &snapshot;
  QString acted_path;
  QString acted_method;
  QJsonObject acted_body;
  context.action = [&acted_path, &acted_method, &acted_body](
                       const QString&, const QString& method,
                       const QString& path, const QJsonObject& body) {
    acted_method = method;
    acted_path = path;
    acted_body = body;
  };
  QString tested_server;
  QString oauth_server;
  QString oauth_flow;
  QString opened_url;
  QString cancelled_flow;
  context.start_mcp_oauth = [&oauth_server](const QString& name,
                                            hermes::ui::PageJsonCallback done) {
    oauth_server = name;
    done(QJsonObject{{QStringLiteral("flow_id"), QStringLiteral("flow1")},
                     {QStringLiteral("status"),
                      QStringLiteral("authorization_required")},
                     {QStringLiteral("authorization_url"),
                      QStringLiteral("https://auth.example/start")}},
         QString());
  };
  context.poll_mcp_oauth = [&oauth_flow](const QString& flow_id,
                                         hermes::ui::PageJsonCallback done) {
    oauth_flow = flow_id;
    done(QJsonObject{{QStringLiteral("status"), QStringLiteral("approved")}},
         QString());
  };
  context.cancel_mcp_oauth = [&cancelled_flow](
                                 const QString& flow_id,
                                 hermes::ui::PageJsonCallback done) {
    cancelled_flow = flow_id;
    done(QJsonObject(), QString());
  };
  context.open_url = [&opened_url](const QString& url) { opened_url = url; };
  QJsonObject replaced_mcp_servers;
  context.get_mcp_config = [](hermes::ui::PageJsonCallback done) {
    done(
        QJsonObject{
            {QStringLiteral("servers"),
             QJsonObject{
                 {QStringLiteral("memory"),
                  QJsonObject{{QStringLiteral("url"),
                               QStringLiteral("https://mcp.example")},
                              {QStringLiteral("auth"), QStringLiteral("oauth")},
                              {QStringLiteral("enabled"), true}}},
                 {QStringLiteral("other"),
                  QJsonObject{
                      {QStringLiteral("command"), QStringLiteral("npx")},
                      {QStringLiteral("headers"),
                       QJsonObject{{QStringLiteral("Authorization"),
                                    QStringLiteral("Bearer keep-me")}}}}}}}},
        QString());
  };
  context.replace_mcp_config = [&replaced_mcp_servers](
                                   const QJsonObject& servers,
                                   hermes::ui::PageJsonCallback done) {
    replaced_mcp_servers = servers;
    done(QJsonObject(), QString());
  };
  context.list_mcp_catalog = [](hermes::ui::PageJsonCallback done) {
    done(
        QJsonObject{
            {QStringLiteral("entries"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("filesystem")},
                 {QStringLiteral("description"), QStringLiteral("Local files")},
                 {QStringLiteral("transport"), QStringLiteral("stdio")},
                 {QStringLiteral("command"), QStringLiteral("npx")},
                 {QStringLiteral("installed"), false},
                 {QStringLiteral("required_env"),
                  QJsonArray{QJsonObject{
                      {QStringLiteral("name"), QStringLiteral("ROOT")},
                      {QStringLiteral("prompt"),
                       QStringLiteral("Root directory")}}}}}}}},
        QString());
  };
  QString skill_content_path;
  context.get_skill_content = [&skill_content_path](
                                  const QString& name,
                                  hermes::ui::PageJsonCallback done) {
    Q_UNUSED(name);
    const QString path = QStringLiteral("/skills/docs/SKILL.md");
    skill_content_path = path;
    done(QJsonObject{{QStringLiteral("content"),
                      QStringLiteral("---\nname: docs\n---\nOriginal body.\n")},
                     {QStringLiteral("path"), path}},
         QString());
  };
  context.list_official_skills = [](hermes::ui::PageJsonCallback done) {
    done(
        QJsonObject{
            {QStringLiteral("skills"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("name"),
                      QStringLiteral("security-review")},
                     {QStringLiteral("identifier"),
                      QStringLiteral("official/security/security-review")},
                     {QStringLiteral("description"),
                      QStringLiteral("Review a change for security issues")},
                     {QStringLiteral("category"), QStringLiteral("security")},
                     {QStringLiteral("installed"), false}},
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("docs")},
                             {QStringLiteral("identifier"),
                              QStringLiteral("official/docs/docs")},
                             {QStringLiteral("description"),
                              QStringLiteral("Already here")},
                             {QStringLiteral("installed"), true}}}}},
        QString());
  };
  QString searched_query;
  QString searched_source;
  int searched_limit = 0;
  QString previewed_identifier;
  QString scanned_identifier;
  context.list_skill_hub_sources = [](hermes::ui::PageJsonCallback done) {
    done(
        QJsonObject{
            {QStringLiteral("sources"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("id"), QStringLiteral("github")},
                     {QStringLiteral("label"), QStringLiteral("GitHub")}},
                 QJsonObject{
                     {QStringLiteral("id"), QStringLiteral("hermes-index")},
                     {QStringLiteral("label"), QStringLiteral("Hermes index")},
                     {QStringLiteral("available"), true}}}}},
        QString());
  };
  context.search_skill_hub = [&searched_query, &searched_source,
                              &searched_limit](
                                 const QString& query, const QString& source,
                                 int limit, hermes::ui::PageJsonCallback done) {
    searched_query = query;
    searched_source = source;
    searched_limit = limit;
    done(
        QJsonObject{
            {QStringLiteral("results"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("gif-search")},
                 {QStringLiteral("identifier"),
                  QStringLiteral("github/acme/gif-search")},
                 {QStringLiteral("description"), QStringLiteral("Find a GIF")},
                 {QStringLiteral("source"), QStringLiteral("github")},
                 {QStringLiteral("trust_level"),
                  QStringLiteral("community")}}}},
            {QStringLiteral("installed"), QJsonObject()},
            {QStringLiteral("timed_out"), QJsonArray()}},
        QString());
  };
  context.preview_skill_hub = [&previewed_identifier](
                                  const QString& identifier,
                                  hermes::ui::PageJsonCallback done) {
    previewed_identifier = identifier;
    done(
        QJsonObject{
            {QStringLiteral("name"), QStringLiteral("gif-search")},
            {QStringLiteral("identifier"), identifier},
            {QStringLiteral("description"), QStringLiteral("Find a GIF")},
            {QStringLiteral("source"), QStringLiteral("github")},
            {QStringLiteral("trust_level"), QStringLiteral("community")},
            {QStringLiteral("skill_md"),
             QStringLiteral("# GIF search\nUse the API.")},
            {QStringLiteral("files"), QJsonArray{QStringLiteral("SKILL.md")}}},
        QString());
  };
  context.scan_skill_hub = [&scanned_identifier](
                               const QString& identifier,
                               hermes::ui::PageJsonCallback done) {
    scanned_identifier = identifier;
    done(QJsonObject{{QStringLiteral("name"), QStringLiteral("gif-search")},
                     {QStringLiteral("identifier"), identifier},
                     {QStringLiteral("verdict"), QStringLiteral("fail")},
                     {QStringLiteral("policy"), QStringLiteral("block")},
                     {QStringLiteral("summary"),
                      QStringLiteral("Blocked by policy")},
                     {QStringLiteral("findings"),
                      QJsonArray{QJsonObject{
                          {QStringLiteral("severity"), QStringLiteral("high")},
                          {QStringLiteral("description"),
                           QStringLiteral("Remote code execution")}}}}},
         QString());
  };
  QString configured_provider;
  QJsonObject configured_env;
  QString refreshed_page;
  context.refresh = [&refreshed_page](const QString& page_id) {
    refreshed_page = page_id;
  };
  context.get_toolset_config = [](const QString& name,
                                  hermes::ui::PageJsonCallback done) {
    Q_UNUSED(name);
    done(
        QJsonObject{
            {QStringLiteral("active_provider"), QStringLiteral("shell")},
            {QStringLiteral("providers"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("shell")},
                     {QStringLiteral("badge"), QStringLiteral("Shell")},
                     {QStringLiteral("status"), QStringLiteral("ready")},
                     {QStringLiteral("env_vars"),
                      QJsonArray{
                          QJsonObject{{QStringLiteral("key"),
                                       QStringLiteral("SHELL_TOKEN")},
                                      {QStringLiteral("prompt"),
                                       QStringLiteral("Shell token")},
                                      {QStringLiteral("is_set"), true}}}}},
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("docker")},
                     {QStringLiteral("badge"), QStringLiteral("Docker")},
                     {QStringLiteral("status"), QStringLiteral("needs_setup")},
                     {QStringLiteral("env_vars"),
                      QJsonArray{QJsonObject{
                          {QStringLiteral("key"), QStringLiteral("DOCKER_KEY")},
                          {QStringLiteral("prompt"),
                           QStringLiteral("Docker key")},
                          {QStringLiteral("is_set"), false}}}}}}}},
        QString());
  };
  context.save_toolset_config = [&configured_provider, &configured_env](
                                    const QString&, const QString& provider,
                                    const QJsonObject& env,
                                    hermes::ui::PageJsonCallback done) {
    configured_provider = provider;
    configured_env = env;
    done(QJsonObject(), QString());
  };
  QString models_provider;
  QString selected_model;
  QString selected_model_provider;
  context.get_toolset_models = [&models_provider](
                                   const QString&, const QString& provider,
                                   hermes::ui::PageJsonCallback done) {
    models_provider = provider;
    if (provider != QStringLiteral("shell")) {
      done(QJsonObject{{QStringLiteral("has_models"), false}}, QString());
      return;
    }
    done(
        QJsonObject{
            {QStringLiteral("has_models"), true},
            {QStringLiteral("provider"), QStringLiteral("shell")},
            {QStringLiteral("current"), QStringLiteral("fast")},
            {QStringLiteral("default"), QStringLiteral("fast")},
            {QStringLiteral("models"),
             QJsonArray{QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("fast")},
                            {QStringLiteral("display"), QStringLiteral("Fast")},
                            {QStringLiteral("price"), QStringLiteral("$1")}},
                        QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("slow")},
                            {QStringLiteral("display"), QStringLiteral("Slow")},
                            {QStringLiteral("price"), QStringLiteral("$4")}}}}},
        QString());
  };
  context.save_toolset_model = [&selected_model, &selected_model_provider](
                                   const QString&, const QString& model,
                                   const QString& provider,
                                   hermes::ui::PageJsonCallback done) {
    selected_model = model;
    selected_model_provider = provider;
    done(QJsonObject{{QStringLiteral("ok"), true}}, QString());
  };
  context.test_mcp_server = [&tested_server](
                                const QString& name,
                                hermes::ui::PageJsonCallback done) {
    tested_server = name;
    done(
        QJsonObject{
            {QStringLiteral("ok"), true},
            {QStringLiteral("tools"),
             QJsonArray{
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("a")}},
                 QJsonObject{{QStringLiteral("name"), QStringLiteral("b")}}}}},
        QString());
  };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  auto* terminal =
      page->findChild<QCheckBox*>(QStringLiteral("toolsetToggle_terminal"));
  auto* video =
      page->findChild<QCheckBox*>(QStringLiteral("toolsetToggle_video"));
  QVERIFY(terminal != nullptr);
  QVERIFY(video != nullptr);
  QVERIFY(terminal->isChecked());
  QVERIFY(!video->isChecked());

  // Disabling a capability is a real mutation, not a rendered "yes".
  terminal->setChecked(false);
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/tools/toolsets/terminal?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("enabled")).toBool(), false);

  // Per-toolset config reads the provider matrix, then saves the selected
  // provider plus only the non-blank credential fields.
  auto* configure =
      page->findChild<QPushButton*>(QStringLiteral("toolsetConfig_terminal"));
  QVERIFY(configure != nullptr);
  configure->click();
  auto* toolset_dialog =
      page->findChild<QDialog*>(QStringLiteral("toolsetConfigDialog_terminal"));
  QVERIFY(toolset_dialog != nullptr);
  auto* toolset_provider =
      toolset_dialog->findChild<QComboBox*>(QStringLiteral("toolsetProvider"));
  QVERIFY(toolset_provider != nullptr);
  QCOMPARE(toolset_provider->count(), 2);
  QCOMPARE(toolset_provider->currentData().toString(), QStringLiteral("shell"));
  auto* shell_token = toolset_dialog->findChild<QLineEdit*>(
      QStringLiteral("toolsetEnv_SHELL_TOKEN"));
  QVERIFY(shell_token != nullptr);
  QVERIFY(shell_token->text().isEmpty());
  QVERIFY(
      shell_token->placeholderText().contains(QStringLiteral("already set")));
  auto* toolset_model =
      toolset_dialog->findChild<QComboBox*>(QStringLiteral("toolsetModel"));
  QVERIFY(toolset_model != nullptr);
  QVERIFY(!toolset_model->isHidden());
  QCOMPARE(models_provider, QStringLiteral("shell"));
  QCOMPARE(toolset_model->count(), 2);
  QCOMPARE(toolset_model->currentData().toString(), QStringLiteral("fast"));
  toolset_model->setCurrentIndex(1);
  QCOMPARE(selected_model, QStringLiteral("slow"));
  QCOMPARE(selected_model_provider, QStringLiteral("shell"));
  toolset_provider->setCurrentIndex(1);
  auto* docker_key = toolset_dialog->findChild<QLineEdit*>(
      QStringLiteral("toolsetEnv_DOCKER_KEY"));
  QVERIFY(docker_key != nullptr);
  docker_key->setText(QStringLiteral("docker-secret"));
  toolset_dialog->findChild<QPushButton*>(QStringLiteral("toolsetConfigSubmit"))
      ->click();
  QCOMPARE(configured_provider, QStringLiteral("docker"));
  QCOMPARE(configured_env.value(QStringLiteral("DOCKER_KEY")).toString(),
           QStringLiteral("docker-secret"));
  QVERIFY(!configured_env.contains(QStringLiteral("SHELL_TOKEN")));
  QCOMPARE(refreshed_page, QStringLiteral("capabilities"));

  // Skills toggle and lock are profile-scoped config writes.
  auto* skill_toggle =
      page->findChild<QCheckBox*>(QStringLiteral("skillToggle_docs"));
  auto* skill_lock =
      page->findChild<QPushButton*>(QStringLiteral("skillLock_docs"));
  QVERIFY(skill_toggle != nullptr);
  QVERIFY(skill_lock != nullptr);
  QVERIFY(skill_toggle->isChecked());
  QCOMPARE(skill_lock->text(), QStringLiteral("Lock"));
  skill_toggle->setChecked(false);
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QCOMPARE(acted_path, QStringLiteral("/api/skills/toggle?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("docs"));
  QCOMPARE(acted_body.value(QStringLiteral("enabled")).toBool(), false);
  skill_lock->click();
  QCOMPARE(acted_path, QStringLiteral("/api/skills/lock?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("locked")).toBool(), true);

  // The skill editor loads SKILL.md through the injected read and saves a
  // full rewrite through the profile-scoped route.
  auto* skill_edit =
      page->findChild<QPushButton*>(QStringLiteral("skillEdit_docs"));
  QVERIFY(skill_edit != nullptr);
  QVERIFY(skill_edit->isEnabled());
  skill_edit->click();
  QCOMPARE(skill_content_path, QStringLiteral("/skills/docs/SKILL.md"));
  auto* skill_dialog =
      page->findChild<QDialog*>(QStringLiteral("skillEditDialog_docs"));
  QVERIFY(skill_dialog != nullptr);
  auto* skill_content = skill_dialog->findChild<QPlainTextEdit*>(
      QStringLiteral("skillEditContent"));
  QVERIFY(skill_content != nullptr);
  QVERIFY(
      skill_content->toPlainText().contains(QStringLiteral("Original body.")));
  skill_content->setPlainText(
      QStringLiteral("---\nname: docs\n---\nEdited.\n"));
  auto* skill_save =
      skill_dialog->findChild<QPushButton*>(QStringLiteral("skillEditSubmit"));
  QVERIFY(skill_save != nullptr);
  skill_save->click();
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QCOMPARE(acted_path, QStringLiteral("/api/skills/content?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("docs"));
  QVERIFY(acted_body.value(QStringLiteral("content"))
              .toString()
              .contains(QStringLiteral("Edited.")));

  // Hub provenance exposes uninstall; update-all is available from the
  // toolbar.
  auto* uninstall = page->findChild<QPushButton*>(
      QStringLiteral("skillUninstall_release-notes"));
  QVERIFY(uninstall != nullptr);
  uninstall->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/skills/hub/uninstall?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("release-notes"));
  auto* update_hub =
      page->findChild<QPushButton*>(QStringLiteral("skillHubUpdate"));
  QVERIFY(update_hub != nullptr);
  update_hub->click();
  QCOMPARE(acted_path, QStringLiteral("/api/skills/hub/update?profile=work"));

  // Creating a skill uses the agent's create path with an optional category.
  auto* new_skill = page->findChild<QPushButton*>(QStringLiteral("skillNew"));
  QVERIFY(new_skill != nullptr);
  new_skill->click();
  auto* new_dialog =
      page->findChild<QDialog*>(QStringLiteral("skillNewDialog"));
  QVERIFY(new_dialog != nullptr);
  new_dialog->findChild<QLineEdit*>(QStringLiteral("skillNewName"))
      ->setText(QStringLiteral("release-notes"));
  new_dialog->findChild<QLineEdit*>(QStringLiteral("skillNewCategory"))
      ->setText(QStringLiteral("docs"));
  new_dialog->findChild<QPlainTextEdit*>(QStringLiteral("skillNewContent"))
      ->setPlainText(QStringLiteral("# Release notes\n"));
  auto* new_submit =
      new_dialog->findChild<QPushButton*>(QStringLiteral("skillNewSubmit"));
  QVERIFY(new_submit != nullptr);
  new_submit->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/skills?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("release-notes"));
  QCOMPARE(acted_body.value(QStringLiteral("category")).toString(),
           QStringLiteral("docs"));

  // The official hub browser lists the catalog, disables installed rows and
  // dispatches install through the profile-scoped hub route.
  auto* browse =
      page->findChild<QPushButton*>(QStringLiteral("skillHubBrowse"));
  QVERIFY(browse != nullptr);
  QVERIFY(browse->isEnabled());
  browse->click();
  auto* hub = page->findChild<QDialog*>(QStringLiteral("skillHubDialog"));
  QVERIFY(hub != nullptr);
  auto* hub_list = hub->findChild<QListWidget*>(QStringLiteral("skillHubList"));
  auto* hub_install =
      hub->findChild<QPushButton*>(QStringLiteral("skillHubInstall"));
  auto* hub_status = hub->findChild<QLabel*>(QStringLiteral("skillHubStatus"));
  QVERIFY(hub_list != nullptr);
  QVERIFY(hub_install != nullptr);
  QVERIFY(hub_status != nullptr);
  QCOMPARE(hub_list->count(), 2);
  QVERIFY(hub_status->text().contains(QStringLiteral("2 official skills")));
  hub_list->setCurrentRow(1);
  QVERIFY(!hub_install->isEnabled());
  hub_list->setCurrentRow(0);
  QVERIFY(hub_install->isEnabled());
  hub_install->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/skills/hub/install?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("identifier")).toString(),
           QStringLiteral("official/security/security-review"));
  QVERIFY(hub_status->text().contains(QStringLiteral("Install started")));

  // Hub sources, remote search, preview and scan are async hooks; the dialog
  // stays responsive and renders each payload.
  auto* hub_source =
      hub->findChild<QComboBox*>(QStringLiteral("skillHubSource"));
  auto* hub_query = hub->findChild<QLineEdit*>(QStringLiteral("skillHubQuery"));
  auto* hub_search =
      hub->findChild<QPushButton*>(QStringLiteral("skillHubSearch"));
  auto* hub_preview =
      hub->findChild<QPushButton*>(QStringLiteral("skillHubPreview"));
  auto* hub_scan = hub->findChild<QPushButton*>(QStringLiteral("skillHubScan"));
  auto* hub_detail =
      hub->findChild<QPlainTextEdit*>(QStringLiteral("skillHubDetail"));
  QVERIFY(hub_source != nullptr);
  QVERIFY(hub_query != nullptr);
  QVERIFY(hub_search != nullptr);
  QVERIFY(hub_preview != nullptr);
  QVERIFY(hub_scan != nullptr);
  QVERIFY(hub_detail != nullptr);
  QCOMPARE(hub_source->count(), 3);
  QCOMPARE(hub_source->itemData(1).toString(), QStringLiteral("github"));
  hub_query->setText(QStringLiteral("gif"));
  hub_source->setCurrentIndex(1);
  hub_search->click();
  QCOMPARE(searched_query, QStringLiteral("gif"));
  QCOMPARE(searched_source, QStringLiteral("github"));
  QCOMPARE(searched_limit, 20);
  QCOMPARE(hub_list->count(), 1);
  QVERIFY(hub_status->text().contains(QStringLiteral("1 result")));
  hub_list->setCurrentRow(0);
  QVERIFY(hub_install->isEnabled());
  hub_preview->click();
  QCOMPARE(previewed_identifier, QStringLiteral("github/acme/gif-search"));
  QVERIFY(hub_detail->toPlainText().contains(QStringLiteral("# GIF search")));
  hub_scan->click();
  QCOMPARE(scanned_identifier, QStringLiteral("github/acme/gif-search"));
  QVERIFY(hub_detail->toPlainText().contains(QStringLiteral("Verdict: fail")));
  QVERIFY(hub_detail->toPlainText().contains(
      QStringLiteral("Remote code execution")));
  QVERIFY(hub_status->text().contains(QStringLiteral("Scan: fail")));
  QVERIFY2(!hub_install->isEnabled(),
           "a blocked scan must keep the skill from being installed");

  // MCP add form posts the gateway's MCPServerCreate shape.
  auto* add_name = page->findChild<QLineEdit*>(QStringLiteral("mcpNewName"));
  auto* add_url = page->findChild<QLineEdit*>(QStringLiteral("mcpNewUrl"));
  auto* add_args = page->findChild<QLineEdit*>(QStringLiteral("mcpNewArgs"));
  auto* add = page->findChild<QPushButton*>(QStringLiteral("mcpAdd"));
  QVERIFY(add_name != nullptr);
  QVERIFY(add_url != nullptr);
  QVERIFY(add != nullptr);
  add_name->setText(QStringLiteral("docs"));
  add_url->setText(QStringLiteral("https://docs.example/mcp"));
  add->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/mcp/servers?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("docs"));
  QCOMPARE(acted_body.value(QStringLiteral("url")).toString(),
           QStringLiteral("https://docs.example/mcp"));

  // Stdio entries carry command + split args.
  add_name->setText(QStringLiteral("local"));
  add_url->clear();
  auto* add_command =
      page->findChild<QLineEdit*>(QStringLiteral("mcpNewCommand"));
  QVERIFY(add_command != nullptr);
  add_command->setText(QStringLiteral("npx"));
  add_args->setText(QStringLiteral("-y pkg"));
  add->click();
  QCOMPARE(acted_body.value(QStringLiteral("command")).toString(),
           QStringLiteral("npx"));
  QCOMPARE(acted_body.value(QStringLiteral("args")).toArray().size(), 2);

  // Raw config editing preserves untouched servers and applies the tool
  // limit.
  auto* mcp_edit =
      page->findChild<QPushButton*>(QStringLiteral("mcpEdit_memory"));
  QVERIFY(mcp_edit != nullptr);
  mcp_edit->click();
  auto* edit_dialog =
      page->findChild<QDialog*>(QStringLiteral("mcpEditDialog_memory"));
  QVERIFY(edit_dialog != nullptr);
  auto* editor =
      edit_dialog->findChild<QPlainTextEdit*>(QStringLiteral("mcpEditJson"));
  QVERIFY(editor != nullptr);
  QJsonObject edited_config =
      QJsonDocument::fromJson(editor->toPlainText().toUtf8()).object();
  edited_config.insert(QStringLiteral("tools"),
                       QJsonArray{QStringLiteral("search")});
  editor->setPlainText(QString::fromUtf8(
      QJsonDocument(edited_config).toJson(QJsonDocument::Indented)));
  auto* edit_save =
      edit_dialog->findChild<QPushButton*>(QStringLiteral("mcpEditSubmit"));
  QVERIFY(edit_save != nullptr);
  edit_save->click();
  QCOMPARE(replaced_mcp_servers.value(QStringLiteral("memory"))
               .toObject()
               .value(QStringLiteral("tools"))
               .toArray()
               .at(0)
               .toString(),
           QStringLiteral("search"));
  QCOMPARE(replaced_mcp_servers.value(QStringLiteral("other"))
               .toObject()
               .value(QStringLiteral("headers"))
               .toObject()
               .value(QStringLiteral("Authorization"))
               .toString(),
           QStringLiteral("Bearer keep-me"));

  // Test probes through the injected supervisor callback and reports inline.
  auto* test = page->findChild<QPushButton*>(QStringLiteral("mcpTest_memory"));
  auto* status = page->findChild<QLabel*>(QStringLiteral("mcpStatus_memory"));
  QVERIFY(test != nullptr);
  QVERIFY(status != nullptr);
  test->click();
  QCOMPARE(tested_server, QStringLiteral("memory"));
  QCOMPARE(status->text(), QStringLiteral("2 tools"));

  // OAuth: start the dashboard flow, open the system browser, poll until the
  // gateway reports approval, then re-probe for the tool count.
  auto* oauth =
      page->findChild<QPushButton*>(QStringLiteral("mcpOAuth_memory"));
  QVERIFY(oauth != nullptr);
  oauth->click();
  QCOMPARE(oauth_server, QStringLiteral("memory"));
  QCOMPARE(opened_url, QStringLiteral("https://auth.example/start"));
  QTest::qWait(400);
  QCOMPARE(oauth_flow, QStringLiteral("flow1"));
  QCOMPARE(status->text(), QStringLiteral("OAuth complete · 2 tools"));

  // A second flow can be abandoned; Cancel settles it instead of leaving the
  // gateway-side flow squatting until its callback timeout.
  oauth->click();
  auto* oauth_cancel =
      page->findChild<QPushButton*>(QStringLiteral("mcpOAuthCancel_memory"));
  QVERIFY(oauth_cancel != nullptr);
  QVERIFY(!oauth_cancel->isHidden());
  oauth_cancel->click();
  QCOMPARE(cancelled_flow, QStringLiteral("flow1"));
  QCOMPARE(status->text(), QStringLiteral("OAuth cancelled."));

  // Catalog install carries only declared env vars and refreshes by action.
  auto* catalog_browse =
      page->findChild<QPushButton*>(QStringLiteral("mcpCatalogBrowse"));
  QVERIFY(catalog_browse != nullptr);
  catalog_browse->click();
  auto* catalog_dialog =
      page->findChild<QDialog*>(QStringLiteral("mcpCatalogDialog"));
  QVERIFY(catalog_dialog != nullptr);
  auto* catalog_env = catalog_dialog->findChild<QLineEdit*>(
      QStringLiteral("mcpCatalogEnv_filesystem_ROOT"));
  QVERIFY(catalog_env != nullptr);
  catalog_env->setText(QStringLiteral("/tmp/root"));
  auto* catalog_install = catalog_dialog->findChild<QPushButton*>(
      QStringLiteral("mcpCatalogInstall_filesystem"));
  QVERIFY(catalog_install != nullptr);
  catalog_install->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/mcp/catalog/install?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("filesystem"));
  QCOMPARE(acted_body.value(QStringLiteral("env"))
               .toObject()
               .value(QStringLiteral("ROOT"))
               .toString(),
           QStringLiteral("/tmp/root"));
  catalog_dialog->close();

  // Enable/disable is PUT on the /enabled route (not POST on the server
  // root).
  auto* mcp_toggle =
      page->findChild<QCheckBox*>(QStringLiteral("mcpToggle_memory"));
  QVERIFY(mcp_toggle != nullptr);
  mcp_toggle->setChecked(false);
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/mcp/servers/memory/enabled?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("enabled")).toBool(), false);

  // Removing a server is a real DELETE, scoped to this profile.
  auto* mcp_remove =
      page->findChild<QPushButton*>(QStringLiteral("mcpRemove_memory"));
  QVERIFY(mcp_remove != nullptr);
  mcp_remove->click();
  QCOMPARE(acted_method, QStringLiteral("DELETE"));
  QCOMPARE(acted_path, QStringLiteral("/api/mcp/servers/memory?profile=work"));
}

void SettingsViewTest::sessionsArchiveAndTransfer() {
  hermes::ui::ProfileSnapshot snapshot;
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Chat: one");
  session.archived = false;
  snapshot.sessions.push_back(session);
  hermes::ui::SessionSummary second;
  second.id = QStringLiteral("s2");
  second.title = QStringLiteral("Chat: two");
  snapshot.sessions.push_back(second);
  hermes::ui::SessionSummary branch;
  branch.id = QStringLiteral("s3");
  branch.title = QStringLiteral("Chat: branch");
  branch.branched_from = QStringLiteral("s1");
  branch.parent_session_id = QStringLiteral("s1");
  snapshot.sessions.push_back(branch);
  hermes::ui::SessionSummary reset;
  reset.id = QStringLiteral("s4");
  reset.title = QStringLiteral("Chat: reset");
  reset.reset_from = QStringLiteral("s2");
  reset.parent_session_id = QStringLiteral("s2");
  snapshot.sessions.push_back(reset);
  hermes::ui::SessionSummary compressed;
  compressed.id = QStringLiteral("s5");
  compressed.title = QStringLiteral("Chat: compressed");
  compressed.parent_session_id = QStringLiteral("s4");
  compressed.continuation_kind = QStringLiteral("compression");
  snapshot.sessions.push_back(compressed);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  QString acted_method;
  QString acted_path;
  QStringList acted_paths;
  QJsonObject acted_body;
  context.action = [&acted_method, &acted_path, &acted_paths, &acted_body](
                       const QString&, const QString& method,
                       const QString& path, const QJsonObject& body) {
    acted_method = method;
    acted_path = path;
    acted_paths.push_back(path);
    acted_body = body;
  };
  QString suggested_name;
  QString save_path = QStringLiteral("/tmp/s1.json");
  context.pick_save_file = [&suggested_name](const QString&,
                                             const QString& name) {
    suggested_name = name;
    return QStringLiteral("/tmp/s1.json");
  };
  QString open_path = QStringLiteral("/tmp/import.json");
  context.pick_open_file = [](const QString&, const QString&) {
    return QStringLiteral("/tmp/import.json");
  };
  QString exported_id;
  QString exported_path;
  context.export_session = [&exported_id, &exported_path](
                               const QString& id, const QString& path,
                               hermes::ui::PageJsonCallback done) {
    exported_id = id;
    exported_path = path;
    done(QJsonObject(), QString());
  };
  QString imported_path;
  context.import_sessions =
      [&imported_path](const QString& path, hermes::ui::PageJsonCallback done) {
        imported_path = path;
        done(QJsonObject{{QStringLiteral("imported"), 2}}, QString());
      };
  QString opened_session;
  context.open_session = [&opened_session](const QString& id) {
    opened_session = id;
  };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("sessions"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* list =
      page->findChild<QListWidget*>(QStringLiteral("settingsSessions"));
  QVERIFY(list != nullptr);
  QCOMPARE(list->count(), 5);
  QCOMPARE(list->item(2)->text(), QStringLiteral("↳ Chat: branch"));
  QVERIFY(
      list->item(2)->toolTip().contains(QStringLiteral("Branched from s1")));
  QCOMPARE(list->item(3)->text(), QStringLiteral("↻ Chat: reset"));
  QVERIFY(
      list->item(3)->toolTip().contains(QStringLiteral("Continued from s2")));
  QCOMPARE(list->item(4)->text(), QStringLiteral("⟳ Chat: compressed"));
  QVERIFY(
      list->item(4)->toolTip().contains(QStringLiteral("Compressed from s4")));
  list->setCurrentRow(0);
  list->item(1)->setSelected(true);

  auto* archive =
      page->findChild<QPushButton*>(QStringLiteral("sessionArchive"));
  auto* restore =
      page->findChild<QPushButton*>(QStringLiteral("sessionRestore"));
  auto* remove = page->findChild<QPushButton*>(QStringLiteral("sessionDelete"));
  auto* export_button =
      page->findChild<QPushButton*>(QStringLiteral("sessionExport"));
  auto* import_button =
      page->findChild<QPushButton*>(QStringLiteral("sessionImport"));
  QVERIFY(archive != nullptr);
  QVERIFY(restore != nullptr);
  QVERIFY(remove != nullptr);
  QVERIFY(export_button != nullptr);
  QVERIFY(import_button != nullptr);

  archive->click();
  QCOMPARE(acted_method, QStringLiteral("PATCH"));
  QCOMPARE(acted_paths, QStringList({QStringLiteral("/api/sessions/s1"),
                                     QStringLiteral("/api/sessions/s2")}));
  QCOMPARE(acted_body.value(QStringLiteral("archived")).toBool(), true);
  acted_paths.clear();
  restore->click();
  QCOMPARE(acted_paths, QStringList({QStringLiteral("/api/sessions/s1"),
                                     QStringLiteral("/api/sessions/s2")}));
  QCOMPARE(acted_body.value(QStringLiteral("archived")).toBool(), false);

  remove->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/sessions/bulk-delete"));
  QCOMPARE(acted_body.value(QStringLiteral("ids")).toArray(),
           QJsonArray({QStringLiteral("s1"), QStringLiteral("s2")}));

  auto* delete_empty =
      page->findChild<QPushButton*>(QStringLiteral("sessionDeleteEmpty"));
  QVERIFY(delete_empty != nullptr);
  delete_empty->click();
  QCOMPARE(acted_method, QStringLiteral("DELETE"));
  QCOMPARE(acted_path, QStringLiteral("/api/sessions/empty"));

  auto* prune_days =
      page->findChild<QSpinBox*>(QStringLiteral("sessionPruneDays"));
  auto* prune_archived =
      page->findChild<QCheckBox*>(QStringLiteral("sessionPruneArchived"));
  auto* prune = page->findChild<QPushButton*>(QStringLiteral("sessionPrune"));
  QVERIFY(prune_days != nullptr);
  QVERIFY(prune_archived != nullptr);
  QVERIFY(prune != nullptr);
  prune_days->setValue(14);
  prune_archived->setChecked(true);
  prune->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/sessions/prune"));
  QCOMPARE(acted_body.value(QStringLiteral("older_than_days")).toInt(), 14);
  QCOMPARE(acted_body.value(QStringLiteral("include_archived")).toBool(), true);

  list->clearSelection();
  list->setCurrentRow(0);
  export_button->click();
  QCOMPARE(exported_id, QStringLiteral("s1"));
  QCOMPARE(exported_path, save_path);
  QCOMPARE(suggested_name, QStringLiteral("Chat- one.json"));

  import_button->click();
  QCOMPARE(imported_path, open_path);

  auto* open_parent =
      page->findChild<QPushButton*>(QStringLiteral("sessionOpenParent"));
  QVERIFY(open_parent != nullptr);
  QVERIFY(!open_parent->isEnabled());
  list->clearSelection();
  list->setCurrentRow(2);
  QVERIFY(open_parent->isEnabled());
  open_parent->click();
  QCOMPARE(opened_session, QStringLiteral("s1"));
  list->setCurrentRow(0);
  QVERIFY(!open_parent->isEnabled());
}

void SettingsViewTest::themesPageAppliesPreset() {
  const std::vector<hermes::ui::ThemePreset> presets =
      hermes::ui::ThemePresets();
  QVERIFY(static_cast<int>(presets.size()) >= 12);
  QSet<QString> ids;
  for (const hermes::ui::ThemePreset& preset : presets) {
    QVERIFY(!preset.id.isEmpty());
    QVERIFY(!preset.name.isEmpty());
    QVERIFY(preset.theme.background.isValid());
    QVERIFY(!ids.contains(preset.id));
    ids.insert(preset.id);
  }
  QCOMPARE(hermes::ui::ThemePresetById(QStringLiteral("github_dark")).name,
           QStringLiteral("GitHub Dark"));
  QVERIFY(hermes::ui::ThemePresetById(QStringLiteral("nope")).id.isEmpty());

  // Theme code colours are painted on the code card, not the window, so the
  // contrast check must use code_background. Body text gets the stricter
  // reading threshold; comments and syntax tokens must stay legible at the
  // code font's small size.
  std::vector<std::pair<QString, hermes::ui::Theme>> code_themes;
  code_themes.emplace_back(QStringLiteral("builtin light"),
                           hermes::ui::Theme::Light());
  code_themes.emplace_back(QStringLiteral("builtin dark"),
                           hermes::ui::Theme::Dark());
  for (const hermes::ui::ThemePreset& preset : presets) {
    code_themes.emplace_back(preset.name, preset.theme);
  }
  for (const auto& [name, theme] : code_themes) {
    const QString label = name + QStringLiteral(" code contrast: ");
    QVERIFY2(hermes::ui::ContrastRatio(theme.code_text,
                                       theme.code_background) >= 7.0,
             qPrintable(label + theme.code_text.name()));
    for (const QColor& token : {theme.code_comment, theme.code_keyword,
                                theme.code_string, theme.code_number}) {
      QVERIFY2(hermes::ui::ContrastRatio(token, theme.code_background) >= 4.5,
               qPrintable(label + token.name()));
    }
    QVERIFY2(
        hermes::ui::ContrastRatio(theme.code_comment, theme.code_header) >= 4.5,
        qPrintable(label + QStringLiteral("header ") +
                   theme.code_comment.name()));
  }

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Dark();
  QString applied;
  context.set_theme_preset = [&applied](const QString& id) { applied = id; };
  context.theme_preset = QStringLiteral("github_dark");

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("themes"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* active =
      page->findChild<QPushButton*>(QStringLiteral("themeUse_github_dark"));
  QVERIFY(active != nullptr);
  QVERIFY(!active->isEnabled());
  QCOMPARE(active->text(), QStringLiteral("Active"));

  auto* nord = page->findChild<QPushButton*>(QStringLiteral("themeUse_nord"));
  QVERIFY(nord != nullptr);
  nord->click();
  QCOMPARE(applied, QStringLiteral("nord"));
}

void SettingsViewTest::rendersWorkspacePagesFromSnapshot() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");
  snapshot.artifacts_cwd = QStringLiteral("/tmp/work");

  QJsonArray platform_rows;
  platform_rows.append(QJsonObject{
      {QStringLiteral("id"), QStringLiteral("telegram")},
      {QStringLiteral("label"), QStringLiteral("Telegram")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("state"), QStringLiteral("connected")},
      {QStringLiteral("env_vars"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("key"), QStringLiteral("TELEGRAM_BOT_TOKEN")},
               {QStringLiteral("prompt"), QStringLiteral("Bot token")},
               {QStringLiteral("required"), true},
               {QStringLiteral("is_set"), true},
               {QStringLiteral("is_password"), true},
               {QStringLiteral("redacted_value"), QStringLiteral("••••1234")}},
           QJsonObject{
               {QStringLiteral("key"),
                QStringLiteral("TELEGRAM_ALLOWED_USERS")},
               {QStringLiteral("prompt"), QStringLiteral("Allowed users")},
               {QStringLiteral("is_set"), true},
               {QStringLiteral("is_list"), true},
               {QStringLiteral("redacted_value"), QStringLiteral("123")}},
           QJsonObject{
               {QStringLiteral("key"), QStringLiteral("TELEGRAM_WEBHOOK_URL")},
               {QStringLiteral("prompt"), QStringLiteral("Webhook URL")},
               {QStringLiteral("is_set"), false}}}}});
  platform_rows.append(
      QJsonObject{{QStringLiteral("id"), QStringLiteral("discord")},
                  {QStringLiteral("label"), QStringLiteral("Discord")},
                  {QStringLiteral("enabled"), false}});
  platform_rows.append(
      QJsonObject{{QStringLiteral("id"), QStringLiteral("whatsapp")},
                  {QStringLiteral("label"), QStringLiteral("WhatsApp")},
                  {QStringLiteral("enabled"), false},
                  {QStringLiteral("mode"), QStringLiteral("bot")}});
  hermes::ui::PageState messaging;
  messaging.loaded = true;
  messaging.data.insert(
      QStringLiteral("platforms"),
      QJsonObject{{QStringLiteral("platforms"), platform_rows}});
  snapshot.pages.insert(QStringLiteral("messaging"), messaging);

  QJsonArray profile_rows;
  profile_rows.append(
      QJsonObject{{QStringLiteral("name"), QStringLiteral("default")},
                  {QStringLiteral("model"), QStringLiteral("m1")}});
  profile_rows.append(
      QJsonObject{{QStringLiteral("name"), QStringLiteral("work")},
                  {QStringLiteral("model"), QStringLiteral("m2")}});
  hermes::ui::PageState profiles;
  profiles.loaded = true;
  profiles.data.insert(QStringLiteral("profiles"),
                       QJsonObject{{QStringLiteral("profiles"), profile_rows}});
  snapshot.pages.insert(QStringLiteral("profiles"), profiles);

  QJsonArray entry_rows;
  entry_rows.append(QJsonObject{
      {QStringLiteral("name"), QStringLiteral("notes.txt")},
      {QStringLiteral("path"), QStringLiteral("/tmp/work/notes.txt")},
      {QStringLiteral("isDirectory"), false}});
  entry_rows.append(
      QJsonObject{{QStringLiteral("name"), QStringLiteral("src")},
                  {QStringLiteral("path"), QStringLiteral("/tmp/work/src")},
                  {QStringLiteral("isDirectory"), true}});
  hermes::ui::PageState artifacts;
  artifacts.loaded = true;
  artifacts.data.insert(QStringLiteral("listing"),
                        QJsonObject{{QStringLiteral("entries"), entry_rows}});
  artifacts.data.insert(
      QStringLiteral("preview"),
      QJsonObject{
          {QStringLiteral("text"), QStringLiteral("hello from the preview")}});
  snapshot.pages.insert(QStringLiteral("artifacts"), artifacts);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString acted_page;
  QString acted_path;
  QJsonObject acted_body;
  QString opened_dir;
  QString previewed_file;
  bool added = false;
  context.action = [&acted_page, &acted_path, &acted_body](
                       const QString& page, const QString&, const QString& path,
                       const QJsonObject& body) {
    acted_page = page;
    acted_path = path;
    acted_body = body;
  };
  context.open_dir = [&opened_dir](const QString& path) { opened_dir = path; };
  context.preview_file = [&previewed_file](const QString& path) {
    previewed_file = path;
  };
  context.add_profile = [&added](const QString&,
                                 hermes::ui::PageJsonCallback done) {
    added = true;
    done(QJsonObject(), QString());
  };
  context.test_platform = [](const QString& id,
                             hermes::ui::PageJsonCallback done) {
    if (id != QStringLiteral("telegram")) {
      done(QJsonObject(), QStringLiteral("unknown platform"));
      return;
    }
    done(QJsonObject{{QStringLiteral("ok"), true},
                     {QStringLiteral("state"), QStringLiteral("connected")},
                     {QStringLiteral("message"),
                      QStringLiteral("Telegram is connected.")},
                     {QStringLiteral("auth_posture"),
                      QStringLiteral("degraded")}},
         QString());
  };
  QString telegram_bot_name;
  int telegram_start_count = 0;
  QString telegram_poll_id;
  QStringList telegram_applied_ids;
  QString telegram_cancelled_id;
  QString telegram_opened_url;
  context.start_telegram_onboarding = [&telegram_bot_name,
                                       &telegram_start_count](
                                          const QString& bot_name,
                                          hermes::ui::PageJsonCallback done) {
    telegram_bot_name = bot_name;
    ++telegram_start_count;
    done(
        QJsonObject{{QStringLiteral("pairing_id"), QStringLiteral("pair1")},
                    {QStringLiteral("suggested_username"),
                     QStringLiteral("hermes_setup_bot")},
                    {QStringLiteral("deep_link"),
                     QStringLiteral("https://t.me/hermes_setup_bot?start=abc")},
                    {QStringLiteral("qr_payload"),
                     QStringLiteral("https://t.me/hermes_setup_bot?start=abc")},
                    {QStringLiteral("expires_at"),
                     QStringLiteral("2026-11-10T12:10:00Z")}},
        QString());
  };
  context.poll_telegram_onboarding = [&telegram_poll_id](
                                         const QString& pairing_id,
                                         hermes::ui::PageJsonCallback done) {
    telegram_poll_id = pairing_id;
    done(QJsonObject{{QStringLiteral("status"), QStringLiteral("ready")},
                     {QStringLiteral("bot_username"),
                      QStringLiteral("hermes_setup_bot")},
                     {QStringLiteral("owner_user_id"), QStringLiteral("777")}},
         QString());
  };
  context.apply_telegram_onboarding =
      [&telegram_applied_ids](const QString&, const QStringList& ids,
                              hermes::ui::PageJsonCallback done) {
        telegram_applied_ids = ids;
        done(QJsonObject{{QStringLiteral("ok"), true},
                         {QStringLiteral("bot_username"),
                          QStringLiteral("hermes_setup_bot")},
                         {QStringLiteral("needs_restart"), false}},
             QString());
      };
  context.cancel_telegram_onboarding = [&telegram_cancelled_id](
                                           const QString& pairing_id,
                                           hermes::ui::PageJsonCallback done) {
    telegram_cancelled_id = pairing_id;
    done(QJsonObject(), QString());
  };
  QString whatsapp_start_mode;
  QString whatsapp_start_allowed;
  bool whatsapp_start_reset = true;
  int whatsapp_start_count = 0;
  QString whatsapp_poll_id;
  QString whatsapp_apply_id;
  QString whatsapp_apply_mode;
  QString whatsapp_apply_allowed;
  QString whatsapp_cancelled_id;
  context.start_whatsapp_onboarding =
      [&whatsapp_start_mode, &whatsapp_start_allowed, &whatsapp_start_reset,
       &whatsapp_start_count](const QString& mode, const QString& allowed,
                              bool reset, hermes::ui::PageJsonCallback done) {
        whatsapp_start_mode = mode;
        whatsapp_start_allowed = allowed;
        whatsapp_start_reset = reset;
        ++whatsapp_start_count;
        done(QJsonObject{{QStringLiteral("pairing_id"), QStringLiteral("wa1")},
                         {QStringLiteral("status"), QStringLiteral("waiting")},
                         {QStringLiteral("qr_payload"),
                          QStringLiteral("2@whatsapp-test-qr-payload")},
                         {QStringLiteral("mode"), QStringLiteral("bot")},
                         {QStringLiteral("expires_at"),
                          QStringLiteral("2026-11-10T12:10:00Z")}},
             QString());
      };
  context.poll_whatsapp_onboarding = [&whatsapp_poll_id](
                                         const QString& pairing_id,
                                         hermes::ui::PageJsonCallback done) {
    whatsapp_poll_id = pairing_id;
    done(QJsonObject{{QStringLiteral("status"), QStringLiteral("connected")},
                     {QStringLiteral("mode"), QStringLiteral("self-chat")},
                     {QStringLiteral("account_name"), QStringLiteral("Cyd")},
                     {QStringLiteral("account_phone"),
                      QStringLiteral("15551234567")},
                     {QStringLiteral("allowed_users"), QString()}},
         QString());
  };
  context.apply_whatsapp_onboarding =
      [&whatsapp_apply_id, &whatsapp_apply_mode, &whatsapp_apply_allowed](
          const QString& pairing_id, const QString& mode,
          const QString& allowed, hermes::ui::PageJsonCallback done) {
        whatsapp_apply_id = pairing_id;
        whatsapp_apply_mode = mode;
        whatsapp_apply_allowed = allowed;
        done(QJsonObject{{QStringLiteral("ok"), true},
                         {QStringLiteral("needs_restart"), false},
                         {QStringLiteral("restart_started"), true}},
             QString());
      };
  context.cancel_whatsapp_onboarding = [&whatsapp_cancelled_id](
                                           const QString& pairing_id,
                                           hermes::ui::PageJsonCallback done) {
    whatsapp_cancelled_id = pairing_id;
    done(QJsonObject(), QString());
  };
  context.open_url = [&telegram_opened_url](const QString& url) {
    telegram_opened_url = url;
  };
  QString uploaded_destination;
  QString uploaded_local;
  QString downloaded_remote;
  QString downloaded_local;
  bool refreshed = false;
  context.upload_file = [&uploaded_destination, &uploaded_local](
                            const QString& destination,
                            const QString& local_path,
                            hermes::ui::PageJsonCallback done) {
    uploaded_destination = destination;
    uploaded_local = local_path;
    done(QJsonObject{{QStringLiteral("uploaded_path"), destination}},
         QString());
  };
  context.download_file = [&downloaded_remote, &downloaded_local](
                              const QString& remote_path,
                              const QString& local_path,
                              hermes::ui::PageJsonCallback done) {
    downloaded_remote = remote_path;
    downloaded_local = local_path;
    done(QJsonObject(), QString());
  };
  context.pick_open_file = [](const QString&, const QString&) {
    return QStringLiteral("/tmp/local/photo.png");
  };
  context.pick_save_file = [](const QString&, const QString& name) {
    return QStringLiteral("/tmp/saved/") + name;
  };
  context.refresh = [&refreshed](const QString&) { refreshed = true; };

  QWidget* messaging_page =
      hermes::ui::BuildDataPage(QStringLiteral("messaging"), context, nullptr);
  QVERIFY(messaging_page != nullptr);
  auto* telegram = messaging_page->findChild<QCheckBox*>(
      QStringLiteral("messagingToggle_telegram"));
  QVERIFY(telegram != nullptr);
  QVERIFY(telegram->isChecked());
  auto* test_platform = messaging_page->findChild<QPushButton*>(
      QStringLiteral("messagingTest_telegram"));
  QVERIFY(test_platform != nullptr);
  test_platform->click();
  auto* test_status = messaging_page->findChild<QLabel*>(
      QStringLiteral("messagingTestStatus_telegram"));
  QVERIFY(test_status != nullptr);
  QCOMPARE(test_status->text(),
           QStringLiteral("Telegram is connected. · auth: degraded"));
  telegram->setChecked(false);
  QCOMPARE(acted_page, QStringLiteral("messaging"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/messaging/platforms/telegram?profile=default"));
  QCOMPARE(acted_body.value(QStringLiteral("enabled")).toBool(), false);

  // Credential setup writes only non-empty fields and uses clear_env for
  // existing values, never the redacted preview.
  auto* configure = messaging_page->findChild<QPushButton*>(
      QStringLiteral("messagingConfigure_telegram"));
  QVERIFY(configure != nullptr);
  configure->click();
  auto* config_dialog = messaging_page->findChild<QDialog*>(
      QStringLiteral("messagingConfigDialog_telegram"));
  QVERIFY(config_dialog != nullptr);
  auto* token = config_dialog->findChild<QLineEdit*>(
      QStringLiteral("messagingEnv_telegram_TELEGRAM_BOT_TOKEN"));
  auto* allowed = config_dialog->findChild<QLineEdit*>(
      QStringLiteral("messagingEnv_telegram_TELEGRAM_ALLOWED_USERS"));
  auto* clear_allowed = config_dialog->findChild<QCheckBox*>(
      QStringLiteral("messagingClear_telegram_TELEGRAM_ALLOWED_USERS"));
  QVERIFY(token != nullptr);
  QVERIFY(allowed != nullptr);
  QVERIFY(clear_allowed != nullptr);
  QVERIFY(clear_allowed->isVisibleTo(config_dialog));
  QCOMPARE(token->echoMode(), QLineEdit::Password);
  QVERIFY(token->text().isEmpty());
  token->setText(QStringLiteral("123:new-token"));
  clear_allowed->setChecked(true);
  auto* config_save = config_dialog->findChild<QPushButton*>(
      QStringLiteral("messagingConfigSubmit"));
  QVERIFY(config_save != nullptr);
  config_save->click();
  QCOMPARE(acted_page, QStringLiteral("messaging"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/messaging/platforms/telegram?profile=default"));
  const QJsonObject env = acted_body.value(QStringLiteral("env")).toObject();
  QCOMPARE(env.value(QStringLiteral("TELEGRAM_BOT_TOKEN")).toString(),
           QStringLiteral("123:new-token"));
  QVERIFY(!env.contains(QStringLiteral("TELEGRAM_ALLOWED_USERS")));
  QCOMPARE(acted_body.value(QStringLiteral("clear_env")).toArray().size(), 1);
  QCOMPARE(
      acted_body.value(QStringLiteral("clear_env")).toArray().at(0).toString(),
      QStringLiteral("TELEGRAM_ALLOWED_USERS"));

  // Telegram pairing: start -> deep link -> poll ready -> apply -> refresh.
  auto* onboard = messaging_page->findChild<QPushButton*>(
      QStringLiteral("messagingOnboard_telegram"));
  QVERIFY(onboard != nullptr);
  onboard->click();
  QCOMPARE(telegram_bot_name, QStringLiteral("Hermes Agent"));
  QCOMPARE(telegram_start_count, 1);
  auto* onboard_dialog = messaging_page->findChild<QDialog*>(
      QStringLiteral("messagingOnboardDialog_telegram"));
  QVERIFY(onboard_dialog != nullptr);
  auto* onboard_open = onboard_dialog->findChild<QPushButton*>(
      QStringLiteral("messagingOnboardOpenLink"));
  QVERIFY(onboard_open != nullptr);
  onboard_open->click();
  QCOMPARE(telegram_opened_url,
           QStringLiteral("https://t.me/hermes_setup_bot?start=abc"));
  QCoreApplication::processEvents();
  QCOMPARE(telegram_poll_id, QStringLiteral("pair1"));
  auto* onboard_status = onboard_dialog->findChild<QLabel*>(
      QStringLiteral("messagingOnboardStatus_telegram"));
  QVERIFY(onboard_status != nullptr);
  QVERIFY(onboard_status->text().contains(QStringLiteral("ready")));
  auto* onboard_allowed = onboard_dialog->findChild<QLineEdit*>(
      QStringLiteral("messagingOnboardAllowedUsers"));
  QVERIFY(onboard_allowed != nullptr);
  QCOMPARE(onboard_allowed->text(), QStringLiteral("777"));
  auto* onboard_apply = onboard_dialog->findChild<QPushButton*>(
      QStringLiteral("messagingOnboardApply"));
  QVERIFY(onboard_apply != nullptr);
  QVERIFY(onboard_apply->isEnabled());
  refreshed = false;
  onboard_apply->click();
  QCOMPARE(telegram_applied_ids, QStringList{QStringLiteral("777")});
  QVERIFY(refreshed);
  onboard_dialog->close();
  QCoreApplication::processEvents();

  // Abandoning a second setup cancels the gateway-side pairing.
  onboard->click();
  QCOMPARE(telegram_start_count, 2);
  auto* abandoned_dialog = messaging_page->findChild<QDialog*>(
      QStringLiteral("messagingOnboardDialog_telegram"));
  QVERIFY(abandoned_dialog != nullptr);
  abandoned_dialog->close();
  QCOMPARE(telegram_cancelled_id, QStringLiteral("pair1"));

  // WhatsApp links by QR instead of a deep link. The dialog keeps polling
  // until the gateway reports `connected`, then saves the mode and allowlist.
  auto* whatsapp = messaging_page->findChild<QPushButton*>(
      QStringLiteral("messagingOnboard_whatsapp"));
  QVERIFY(whatsapp != nullptr);
  whatsapp->click();
  QCOMPARE(whatsapp_start_count, 1);
  QCOMPARE(whatsapp_start_mode, QStringLiteral("bot"));
  QVERIFY(whatsapp_start_allowed.isEmpty());
  QVERIFY(!whatsapp_start_reset);
  auto* whatsapp_dialog = messaging_page->findChild<QDialog*>(
      QStringLiteral("messagingOnboardDialog_whatsapp"));
  QVERIFY(whatsapp_dialog != nullptr);
  auto* whatsapp_qr = whatsapp_dialog->findChild<QLabel*>(
      QStringLiteral("messagingOnboardQr_whatsapp"));
  QVERIFY(whatsapp_qr != nullptr);
  QVERIFY(!whatsapp_qr->pixmap().isNull());
  QVERIFY(whatsapp_qr->pixmap().width() >= 250);
  auto* whatsapp_mode = whatsapp_dialog->findChild<QComboBox*>(
      QStringLiteral("messagingOnboardMode"));
  QVERIFY(whatsapp_mode != nullptr);
  whatsapp_mode->setCurrentIndex(
      whatsapp_mode->findData(QStringLiteral("self-chat")));
  QCoreApplication::processEvents();
  QCOMPARE(whatsapp_poll_id, QStringLiteral("wa1"));
  auto* whatsapp_status = whatsapp_dialog->findChild<QLabel*>(
      QStringLiteral("messagingOnboardStatus_whatsapp"));
  QVERIFY(whatsapp_status != nullptr);
  QVERIFY(whatsapp_status->text().contains(QStringLiteral("linked")));
  auto* whatsapp_allowed = whatsapp_dialog->findChild<QLineEdit*>(
      QStringLiteral("messagingOnboardAllowedUsers"));
  QVERIFY(whatsapp_allowed != nullptr);
  QCOMPARE(whatsapp_allowed->text(), QStringLiteral("15551234567"));
  auto* whatsapp_apply = whatsapp_dialog->findChild<QPushButton*>(
      QStringLiteral("messagingOnboardApply"));
  QVERIFY(whatsapp_apply != nullptr);
  QVERIFY(whatsapp_apply->isEnabled());
  refreshed = false;
  whatsapp_apply->click();
  QCOMPARE(whatsapp_apply_id, QStringLiteral("wa1"));
  QCOMPARE(whatsapp_apply_mode, QStringLiteral("self-chat"));
  QCOMPARE(whatsapp_apply_allowed, QStringLiteral("15551234567"));
  QVERIFY(refreshed);
  QVERIFY(whatsapp_status->text().contains(QStringLiteral("restarting")));
  whatsapp_dialog->close();
  QCoreApplication::processEvents();
  QVERIFY(whatsapp_cancelled_id.isEmpty());

  // A second setup abandoned before pairing cancels the gateway session.
  whatsapp->click();
  QCOMPARE(whatsapp_start_count, 2);
  auto* second_whatsapp = messaging_page->findChild<QDialog*>(
      QStringLiteral("messagingOnboardDialog_whatsapp"));
  QVERIFY(second_whatsapp != nullptr);
  second_whatsapp->close();
  QCOMPARE(whatsapp_cancelled_id, QStringLiteral("wa1"));

  // The QR helper is pure and used by the dialog above; verify its quiet zone
  // and module pixels directly so a future encoder swap stays honest.
  const QImage qr_image = hermes::ui::RenderQrImage(
      QStringLiteral("whatsapp-test-qr-payload"), 256);
  QVERIFY(!qr_image.isNull());
  QVERIFY(qr_image.width() == qr_image.height());
  QCOMPARE(qr_image.pixelColor(0, 0), QColor(Qt::white));
  bool saw_black = false;
  for (int y = 0; y < qr_image.height() && !saw_black; ++y) {
    for (int x = 0; x < qr_image.width(); ++x) {
      if (qr_image.pixelColor(x, y) == QColor(Qt::black)) {
        saw_black = true;
        break;
      }
    }
  }
  QVERIFY(saw_black);

  QWidget* profiles_page =
      hermes::ui::BuildDataPage(QStringLiteral("profiles"), context, nullptr);
  QVERIFY(profiles_page != nullptr);
  QVERIFY(profiles_page->findChild<QWidget*>(
              QStringLiteral("profileRow_work")) != nullptr);
  auto* add =
      profiles_page->findChild<QPushButton*>(QStringLiteral("profilesAdd"));
  QVERIFY(add != nullptr);
  QWidget* artifacts_page =
      hermes::ui::BuildDataPage(QStringLiteral("artifacts"), context, nullptr);
  QVERIFY(artifacts_page != nullptr);
  auto* list =
      artifacts_page->findChild<QListWidget*>(QStringLiteral("artifactsList"));
  QVERIFY(list != nullptr);
  // "../" plus the two entries.
  QCOMPARE(list->count(), 3);
  QVERIFY(artifacts_page->findChild<QWidget*>(
              QStringLiteral("artifactsEditor")) != nullptr);
  QVERIFY(artifacts_page->findChild<QWidget*>(
              QStringLiteral("artifactsNewFolder")) != nullptr);
  QVERIFY(artifacts_page->findChild<QWidget*>(
              QStringLiteral("artifactsNewFile")) != nullptr);
  QVERIFY(artifacts_page->findChild<QWidget*>(
              QStringLiteral("artifactsDelete")) != nullptr);
  auto* upload = artifacts_page->findChild<QPushButton*>(
      QStringLiteral("artifactsUpload"));
  QVERIFY(upload != nullptr);
  upload->click();
  QCOMPARE(uploaded_local, QStringLiteral("/tmp/local/photo.png"));
  QCOMPARE(uploaded_destination, QStringLiteral("/tmp/work/photo.png"));
  QVERIFY(refreshed);

  list->setCurrentRow(1);
  auto* download = artifacts_page->findChild<QPushButton*>(
      QStringLiteral("artifactsDownload"));
  QVERIFY(download != nullptr);
  download->click();
  QCOMPARE(downloaded_remote, QStringLiteral("/tmp/work/notes.txt"));
  QCOMPARE(downloaded_local, QStringLiteral("/tmp/saved/notes.txt"));
  Q_UNUSED(added);
  Q_UNUSED(opened_dir);
  Q_UNUSED(previewed_file);
}

void SettingsViewTest::customEndpointEditorValidatesAndSaves() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("work");
  snapshot.label = QStringLiteral("work");
  hermes::ui::PageState page_state;
  page_state.loaded = true;
  page_state.data.insert(
      QStringLiteral("options"),
      QJsonObject{{QStringLiteral("providers"), QJsonArray()}});
  page_state.data.insert(
      QStringLiteral("endpoints"),
      QJsonObject{{QStringLiteral("endpoints"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("id"), QStringLiteral("local")},
                       {QStringLiteral("name"), QStringLiteral("Local")},
                       {QStringLiteral("base_url"),
                        QStringLiteral("http://localhost:1234/v1")},
                       {QStringLiteral("model"), QStringLiteral("local-model")},
                       {QStringLiteral("context_length"), 8192},
                       {QStringLiteral("discover_models"), false},
                       {QStringLiteral("is_current"), false}}}}});
  snapshot.pages.insert(QStringLiteral("providers"), page_state);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString acted_path;
  QJsonObject acted_body;
  context.action = [&acted_path, &acted_body](
                       const QString& page, const QString& method,
                       const QString& path, const QJsonObject& body) {
    QCOMPARE(page, QStringLiteral("providers"));
    QCOMPARE(method, QStringLiteral("POST"));
    acted_path = path;
    acted_body = body;
  };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("providers"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* add = page->findChild<QPushButton*>(QStringLiteral("endpointAdd"));
  QVERIFY(add != nullptr);
  add->click();
  auto* dialog = page->findChild<QDialog*>(QStringLiteral("endpointDialog"));
  QVERIFY(dialog != nullptr);
  auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("endpointName"));
  auto* base_url =
      dialog->findChild<QLineEdit*>(QStringLiteral("endpointBaseUrl"));
  auto* model = dialog->findChild<QLineEdit*>(QStringLiteral("endpointModel"));
  auto* api_key =
      dialog->findChild<QLineEdit*>(QStringLiteral("endpointApiKey"));
  auto* context_length =
      dialog->findChild<QSpinBox*>(QStringLiteral("endpointContext"));
  auto* discover =
      dialog->findChild<QCheckBox*>(QStringLiteral("endpointDiscover"));
  QVERIFY(name != nullptr);
  QVERIFY(base_url != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(api_key != nullptr);
  QVERIFY(context_length != nullptr);
  QVERIFY(discover != nullptr);
  name->setText(QStringLiteral("Remote"));
  base_url->setText(QStringLiteral("https://models.test/v1"));
  model->setText(QStringLiteral("remote-model"));
  api_key->setText(QStringLiteral("sk-test"));
  context_length->setValue(32768);
  discover->setChecked(true);

  auto* validate =
      dialog->findChild<QPushButton*>(QStringLiteral("endpointValidate"));
  QVERIFY(validate != nullptr);
  validate->click();
  QCOMPARE(acted_path,
           QStringLiteral("/api/providers/custom-endpoints/validate"));
  QCOMPARE(acted_body.value(QStringLiteral("base_url")).toString(),
           QStringLiteral("https://models.test/v1"));
  QCOMPARE(acted_body.value(QStringLiteral("api_key")).toString(),
           QStringLiteral("sk-test"));

  auto* submit =
      dialog->findChild<QPushButton*>(QStringLiteral("endpointSubmit"));
  QVERIFY(submit != nullptr);
  submit->click();
  QCOMPARE(acted_path,
           QStringLiteral("/api/providers/custom-endpoints?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("Remote"));
  QCOMPARE(acted_body.value(QStringLiteral("model")).toString(),
           QStringLiteral("remote-model"));
  QCOMPARE(acted_body.value(QStringLiteral("context_length")).toInt(), 32768);
  QCOMPARE(acted_body.value(QStringLiteral("discover_models")).toBool(), true);
  QVERIFY(!acted_body.contains(QStringLiteral("id")));
}

void SettingsViewTest::pluginSettingsSaveSchemaAndSecrets() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("work");
  hermes::ui::PageState plugins;
  plugins.loaded = true;
  QJsonArray schema;
  schema.append(QJsonObject{
      {QStringLiteral("key"), QStringLiteral("mode")},
      {QStringLiteral("type"), QStringLiteral("enum")},
      {QStringLiteral("label"), QStringLiteral("Mode")},
      {QStringLiteral("value"), QStringLiteral("fast")},
      {QStringLiteral("choices"),
       QJsonArray{QStringLiteral("fast"), QStringLiteral("slow")}}});
  schema.append(QJsonObject{{QStringLiteral("key"), QStringLiteral("limit")},
                            {QStringLiteral("type"), QStringLiteral("number")},
                            {QStringLiteral("label"), QStringLiteral("Limit")},
                            {QStringLiteral("value"), 3}});
  schema.append(
      QJsonObject{{QStringLiteral("key"), QStringLiteral("verbose")},
                  {QStringLiteral("type"), QStringLiteral("boolean")},
                  {QStringLiteral("label"), QStringLiteral("Verbose")},
                  {QStringLiteral("value"), false}});
  schema.append(QJsonObject{
      {QStringLiteral("key"), QStringLiteral("rules")},
      {QStringLiteral("type"), QStringLiteral("json")},
      {QStringLiteral("label"), QStringLiteral("Rules")},
      {QStringLiteral("value"), QJsonObject{{QStringLiteral("a"), 1}}}});
  schema.append(
      QJsonObject{{QStringLiteral("key"), QStringLiteral("API_KEY")},
                  {QStringLiteral("type"), QStringLiteral("secret")},
                  {QStringLiteral("label"), QStringLiteral("API key")},
                  {QStringLiteral("env"), QStringLiteral("PLUGIN_KEY")},
                  {QStringLiteral("has_value"), true}});
  plugins.data.insert(
      QStringLiteral("hub"),
      QJsonObject{
          {QStringLiteral("plugins"),
           QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("weather")},
               {QStringLiteral("description"), QStringLiteral("Forecasts")},
               {QStringLiteral("runtime_status"), QStringLiteral("enabled")},
               {QStringLiteral("settings_schema"), schema}}}}});
  snapshot.pages.insert(QStringLiteral("plugins"), plugins);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Dark();
  context.snapshot = &snapshot;
  QString env_path;
  QJsonObject env_body;
  context.action = [&env_path, &env_body](const QString&, const QString& method,
                                          const QString& path,
                                          const QJsonObject& body) {
    Q_UNUSED(method);
    env_path = path;
    env_body = body;
  };
  QString saved_key;
  QJsonObject saved_values;
  QString refreshed_page;
  context.refresh = [&refreshed_page](const QString& page_id) {
    refreshed_page = page_id;
  };
  context.save_plugin_settings =
      [&saved_key, &saved_values](const QString& key, const QJsonObject& values,
                                  hermes::ui::PageJsonCallback done) {
        saved_key = key;
        saved_values = values;
        done(QJsonObject{{QStringLiteral("ok"), true}}, QString());
      };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("plugins"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* settings =
      page->findChild<QPushButton*>(QStringLiteral("pluginSettings_weather"));
  QVERIFY(settings != nullptr);
  settings->click();
  auto* dialog =
      page->findChild<QDialog*>(QStringLiteral("pluginSettingsDialog_weather"));
  QVERIFY(dialog != nullptr);
  auto* mode =
      dialog->findChild<QComboBox*>(QStringLiteral("pluginSetting_mode"));
  auto* limit =
      dialog->findChild<QLineEdit*>(QStringLiteral("pluginSetting_limit"));
  auto* verbose =
      dialog->findChild<QCheckBox*>(QStringLiteral("pluginSetting_verbose"));
  auto* rules =
      dialog->findChild<QLineEdit*>(QStringLiteral("pluginSetting_rules"));
  auto* secret = dialog->findChild<QLineEdit*>(
      QStringLiteral("pluginSettingSecret_PLUGIN_KEY"));
  QVERIFY(mode != nullptr);
  QVERIFY(limit != nullptr);
  QVERIFY(verbose != nullptr);
  QVERIFY(rules != nullptr);
  QVERIFY(secret != nullptr);
  QCOMPARE(mode->currentText(), QStringLiteral("fast"));
  QCOMPARE(limit->text(), QStringLiteral("3"));
  QVERIFY(secret->text().isEmpty());
  QVERIFY(secret->placeholderText().contains(QStringLiteral("already set")));

  mode->setCurrentIndex(1);
  limit->setText(QStringLiteral("7"));
  verbose->setChecked(true);
  rules->setText(QStringLiteral("{\"b\":2}"));
  secret->setText(QStringLiteral("top-secret"));
  dialog->findChild<QPushButton*>(QStringLiteral("pluginSettingsSubmit"))
      ->click();
  QCOMPARE(saved_key, QStringLiteral("weather"));
  QCOMPARE(saved_values.value(QStringLiteral("mode")).toString(),
           QStringLiteral("slow"));
  QCOMPARE(saved_values.value(QStringLiteral("limit")).toDouble(), 7.0);
  QCOMPARE(saved_values.value(QStringLiteral("verbose")).toBool(), true);
  QCOMPARE(saved_values.value(QStringLiteral("rules"))
               .toObject()
               .value(QStringLiteral("b"))
               .toInt(),
           2);
  QCOMPARE(env_path, QStringLiteral("/api/env?profile=work"));
  QCOMPARE(env_body.value(QStringLiteral("key")).toString(),
           QStringLiteral("PLUGIN_KEY"));
  QCOMPARE(env_body.value(QStringLiteral("value")).toString(),
           QStringLiteral("top-secret"));
  QCOMPARE(refreshed_page, QStringLiteral("plugins"));
}

void SettingsViewTest::webhooksEnableCreateAndToggleSubscriptions() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("work");
  snapshot.label = QStringLiteral("work");
  hermes::ui::PageState page_state;
  page_state.loaded = true;
  page_state.data.insert(
      QStringLiteral("webhooks"),
      QJsonObject{{QStringLiteral("enabled"), false},
                  {QStringLiteral("base_url"), QString()},
                  {QStringLiteral("subscriptions"), QJsonArray()}});
  snapshot.pages.insert(QStringLiteral("webhooks"), page_state);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString acted_method;
  QString acted_path;
  QJsonObject acted_body;
  context.action = [&acted_method, &acted_path, &acted_body](
                       const QString& page, const QString& method,
                       const QString& path, const QJsonObject& body) {
    QCOMPARE(page, QStringLiteral("webhooks"));
    acted_method = method;
    acted_path = path;
    acted_body = body;
  };

  QWidget* disabled =
      hermes::ui::BuildDataPage(QStringLiteral("webhooks"), context, nullptr);
  QVERIFY(disabled != nullptr);
  auto* enable =
      disabled->findChild<QPushButton*>(QStringLiteral("webhooksEnable"));
  QVERIFY(enable != nullptr);
  enable->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/webhooks/enable?profile=work"));

  hermes::ui::PageState enabled_state;
  enabled_state.loaded = true;
  enabled_state.data.insert(
      QStringLiteral("webhooks"),
      QJsonObject{
          {QStringLiteral("enabled"), true},
          {QStringLiteral("base_url"), QStringLiteral("https://hooks.test")},
          {QStringLiteral("subscriptions"),
           QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("deploys")},
               {QStringLiteral("events"),
                QJsonArray{QStringLiteral("message")}},
               {QStringLiteral("deliver"), QStringLiteral("log")},
               {QStringLiteral("url"),
                QStringLiteral("https://hooks.test/webhooks/deploys")},
               {QStringLiteral("secret_set"), true},
               {QStringLiteral("enabled"), true}}}}});
  snapshot.pages.insert(QStringLiteral("webhooks"), enabled_state);

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("webhooks"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* toggle =
      page->findChild<QCheckBox*>(QStringLiteral("webhookEnabled_deploys"));
  QVERIFY(toggle != nullptr);
  QVERIFY(toggle->isChecked());
  toggle->setChecked(false);
  QCOMPARE(acted_method, QStringLiteral("PUT"));
  QCOMPARE(acted_path,
           QStringLiteral("/api/webhooks/deploys/enabled?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("enabled")).toBool(), false);

  auto* create =
      page->findChild<QPushButton*>(QStringLiteral("webhooksCreate"));
  QVERIFY(create != nullptr);
  create->click();
  auto* dialog = page->findChild<QDialog*>(QStringLiteral("webhookDialog"));
  QVERIFY(dialog != nullptr);
  auto* name = dialog->findChild<QLineEdit*>(QStringLiteral("webhookName"));
  auto* events = dialog->findChild<QLineEdit*>(QStringLiteral("webhookEvents"));
  auto* prompt =
      dialog->findChild<QPlainTextEdit*>(QStringLiteral("webhookPrompt"));
  auto* deliver =
      dialog->findChild<QLineEdit*>(QStringLiteral("webhookDeliver"));
  QVERIFY(name != nullptr);
  QVERIFY(events != nullptr);
  QVERIFY(prompt != nullptr);
  QVERIFY(deliver != nullptr);
  name->setText(QStringLiteral("alerts"));
  events->setText(QStringLiteral("message, notification"));
  prompt->setPlainText(QStringLiteral("Triage this event."));
  deliver->setText(QStringLiteral("telegram"));
  auto* submit =
      dialog->findChild<QPushButton*>(QStringLiteral("webhookSubmit"));
  QVERIFY(submit != nullptr);
  submit->click();
  QCOMPARE(acted_method, QStringLiteral("POST"));
  QCOMPARE(acted_path, QStringLiteral("/api/webhooks?profile=work"));
  QCOMPARE(acted_body.value(QStringLiteral("name")).toString(),
           QStringLiteral("alerts"));
  QCOMPARE(acted_body.value(QStringLiteral("events")).toArray().size(), 2);
  QCOMPARE(acted_body.value(QStringLiteral("deliver")).toString(),
           QStringLiteral("telegram"));
}

void SettingsViewTest::capabilitiesConnectorConnectPollsOperationAndReloads() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  bool notion_connected = false;
  int list_calls = 0;
  context.list_connectors = [&notion_connected,
                             &list_calls](hermes::ui::PageJsonCallback done) {
    ++list_calls;
    done(
        QJsonObject{
            {QStringLiteral("available"), true},
            {QStringLiteral("connectors"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("connector"), QStringLiteral("gmail")},
                     {QStringLiteral("connected"), true},
                     {QStringLiteral("connection_status"),
                      QStringLiteral("active")}},
                 QJsonObject{
                     {QStringLiteral("connector"), QStringLiteral("notion")},
                     {QStringLiteral("connected"), notion_connected},
                     {QStringLiteral("status_reason"),
                      QStringLiteral("authorization required")}}}}},
        QString());
  };
  QString connected_slug;
  bool reconnect = true;
  context.connect_connector = [&connected_slug, &reconnect](
                                  const QString& slug, bool is_reconnect,
                                  hermes::ui::PageJsonCallback done) {
    connected_slug = slug;
    reconnect = is_reconnect;
    done(
        QJsonObject{{QStringLiteral("op_id"), QStringLiteral("op-7")},
                    {QStringLiteral("user_code"), QStringLiteral("ABCD-1234")},
                    {QStringLiteral("targets"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("name"), slug},
                         {QStringLiteral("state"), QStringLiteral("initiated")},
                         {QStringLiteral("instructions"),
                          QStringLiteral("Open the link and enter the code")},
                         {QStringLiteral("connect_url"),
                          QStringLiteral("https://connect.example/notion")}}}}},
        QString());
  };
  QString opened_url;
  context.open_url = [&opened_url](const QString& url) { opened_url = url; };
  QStringList rpc_methods;
  QString renamed_alias;
  context.connector_rpc = [&rpc_methods, &renamed_alias](
                              const QString& method, const QJsonObject& params,
                              hermes::ui::PageJsonCallback done) {
    rpc_methods.append(method);
    if (method == QStringLiteral("connectors.accounts")) {
      done(
          QJsonObject{
              {QStringLiteral("accounts"),
               QJsonArray{QJsonObject{
                   {QStringLiteral("connection_id"), QStringLiteral("conn-1")},
                   {QStringLiteral("connector"), QStringLiteral("notion")},
                   {QStringLiteral("status"), QStringLiteral("active")},
                   {QStringLiteral("label"), QStringLiteral("Work Notion")},
                   {QStringLiteral("alias"), QStringLiteral("work")},
                   {QStringLiteral("active"), true}}}}},
          QString());
      return;
    }
    if (method == QStringLiteral("connectors.accounts.rename")) {
      renamed_alias = params.value(QStringLiteral("alias")).toString();
      done(QJsonObject{{QStringLiteral("connection_id"),
                        QStringLiteral("conn-1")}},
           QString());
      return;
    }
    if (method == QStringLiteral("connectors.catalog")) {
      done(
          QJsonObject{{QStringLiteral("connectors"),
                       QJsonArray{QJsonObject{
                           {QStringLiteral("slug"), QStringLiteral("notion")},
                           {QStringLiteral("name"), QStringLiteral("Notion")},
                           {QStringLiteral("category"), QStringLiteral("docs")},
                           {QStringLiteral("description"),
                            QStringLiteral("Pages and databases")}}}}},
          QString());
      return;
    }
    if (method == QStringLiteral("connectors.tools")) {
      done(
          QJsonObject{
              {QStringLiteral("source"), QStringLiteral("cache")},
              {QStringLiteral("tools"),
               QJsonArray{QJsonObject{
                   {QStringLiteral("name"), QStringLiteral("search_pages")},
                   {QStringLiteral("description"),
                    QStringLiteral("Search Notion pages")},
                   {QStringLiteral("facet"), QStringLiteral("read")}}}}},
          QString());
      return;
    }
    done(QJsonObject(), QStringLiteral("unexpected method"));
  };
  int status_calls = 0;
  context.connector_operation_status = [&notion_connected, &status_calls](
                                           const QString& op_id,
                                           hermes::ui::PageJsonCallback done) {
    ++status_calls;
    QCOMPARE(op_id, QStringLiteral("op-7"));
    notion_connected = true;
    done(
        QJsonObject{
            {QStringLiteral("op_id"), QStringLiteral("op-7")},
            {QStringLiteral("settled"), true},
            {QStringLiteral("targets"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("name"), QStringLiteral("notion")},
                 {QStringLiteral("state"), QStringLiteral("connected")}}}}},
        QString());
  };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  page->show();
  QTest::qWait(20);
  QCOMPARE(list_calls, 1);
  auto* gmail_row =
      page->findChild<QWidget*>(QStringLiteral("connectorRow_gmail"));
  auto* notion_row =
      page->findChild<QWidget*>(QStringLiteral("connectorRow_notion"));
  QVERIFY(gmail_row != nullptr);
  QVERIFY(notion_row != nullptr);
  auto* notion_status =
      page->findChild<QLabel*>(QStringLiteral("connectorStatus_notion"));
  QVERIFY(notion_status != nullptr);
  auto* connect = notion_row->findChild<QPushButton*>(
      QStringLiteral("connectorConnect_notion"));
  QVERIFY(connect != nullptr);
  QCOMPARE(connect->text(), QStringLiteral("Connect"));
  connect->click();
  QCOMPARE(connected_slug, QStringLiteral("notion"));
  QCOMPARE(reconnect, false);
  QCOMPARE(opened_url, QStringLiteral("https://connect.example/notion"));
  QVERIFY(notion_status->text().contains(QStringLiteral("initiated")));
  QVERIFY(notion_status->text().contains(QStringLiteral("Code: ABCD-1234")));
  QVERIFY(notion_status->text().contains(
      QStringLiteral("Open the link and enter the code")));
  QTRY_COMPARE_WITH_TIMEOUT(status_calls, 1, 1500);
  QTRY_COMPARE_WITH_TIMEOUT(list_calls, 2, 1000);
  QTest::qWait(20);
  auto* refreshed_row =
      page->findChild<QWidget*>(QStringLiteral("connectorRow_notion"));
  QVERIFY(refreshed_row != nullptr);
  auto* refreshed_button = refreshed_row->findChild<QPushButton*>(
      QStringLiteral("connectorConnect_notion"));
  QVERIFY(refreshed_button != nullptr);
  QCOMPARE(refreshed_button->text(), QStringLiteral("Reconnect"));
  auto* accounts_button = refreshed_row->findChild<QPushButton*>(
      QStringLiteral("connectorAccountsButton_notion"));
  QVERIFY(accounts_button != nullptr);
  accounts_button->click();
  QTRY_VERIFY(page->findChild<QDialog*>(
                  QStringLiteral("connectorAccountsDialog_notion")) != nullptr);
  auto* accounts_dialog = page->findChild<QDialog*>(
      QStringLiteral("connectorAccountsDialog_notion"));
  QVERIFY(accounts_dialog != nullptr);
  QTRY_VERIFY(accounts_dialog->findChild<QLineEdit*>(
                  QStringLiteral("connectorAlias_conn-1")) != nullptr);
  auto* alias = accounts_dialog->findChild<QLineEdit*>(
      QStringLiteral("connectorAlias_conn-1"));
  QCOMPARE(alias->text(), QStringLiteral("work"));
  alias->setText(QStringLiteral("studio"));
  auto* rename = accounts_dialog->findChild<QPushButton*>(
      QStringLiteral("connectorRename_conn-1"));
  QVERIFY(rename != nullptr);
  rename->click();
  QCOMPARE(renamed_alias, QStringLiteral("studio"));
  QVERIFY(rpc_methods.contains(QStringLiteral("connectors.accounts.rename")));
  accounts_dialog->accept();
  auto* tools_button = page->findChild<QPushButton*>(
      QStringLiteral("connectorToolsButton_notion"));
  QVERIFY(tools_button != nullptr);
  tools_button->click();
  QTRY_VERIFY(page->findChild<QDialog*>(
                  QStringLiteral("connectorToolsDialog_notion")) != nullptr);
  auto* tools_dialog =
      page->findChild<QDialog*>(QStringLiteral("connectorToolsDialog_notion"));
  QVERIFY(tools_dialog != nullptr);
  auto* tools_text = tools_dialog->findChild<QTextBrowser*>(
      QStringLiteral("connectorToolsText_notion"));
  QVERIFY(tools_text != nullptr);
  QTRY_VERIFY(
      tools_text->toPlainText().contains(QStringLiteral("search_pages")));
  QVERIFY(tools_text->toPlainText().contains(QStringLiteral("[read]")));
  tools_dialog->accept();
  auto* catalog_button =
      page->findChild<QPushButton*>(QStringLiteral("connectorsCatalogButton"));
  QVERIFY(catalog_button != nullptr);
  catalog_button->click();
  QTRY_VERIFY(page->findChild<QDialog*>(
                  QStringLiteral("connectorCatalogDialog")) != nullptr);
  auto* catalog_dialog =
      page->findChild<QDialog*>(QStringLiteral("connectorCatalogDialog"));
  QVERIFY(catalog_dialog != nullptr);
  auto* catalog_text = catalog_dialog->findChild<QTextBrowser*>(
      QStringLiteral("connectorCatalogText"));
  QVERIFY(catalog_text != nullptr);
  QTRY_VERIFY(catalog_text->toPlainText().contains(QStringLiteral("notion")));
  QVERIFY(catalog_text->toPlainText().contains(QStringLiteral("docs")));
  catalog_dialog->accept();
  delete page;
}

void SettingsViewTest::
    capabilitiesLateConnectorListIsIgnoredAfterPageDestroyed() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  hermes::ui::PageJsonCallback pending;
  context.list_connectors = [&pending](hermes::ui::PageJsonCallback done) {
    pending = std::move(done);
  };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  QVERIFY(pending != nullptr);
  delete page;

  // A page rebuild can race the connector list. The late completion must not
  // touch the destroyed panel or layout.
  pending(
      QJsonObject{{QStringLiteral("available"), true},
                  {QStringLiteral("connectors"),
                   QJsonArray{QJsonObject{
                       {QStringLiteral("connector"), QStringLiteral("gmail")},
                       {QStringLiteral("connected"), true}}}}},
      QString());
}

void SettingsViewTest::capabilitiesMergesAccountOperationIntoConnectorRow() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);
  hermes::ui::ConnectionOperation operation;
  operation.op_id = QStringLiteral("op-account");
  operation.owner_type = QStringLiteral("account");
  operation.seq = 4;
  hermes::ui::ConnectionTarget target;
  target.name = QStringLiteral("notion");
  target.kind = QStringLiteral("connector");
  target.action = QStringLiteral("connect");
  target.state = QStringLiteral("initiated");
  operation.targets.push_back(target);
  snapshot.account_operations.push_back(operation);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  context.list_connectors = [](hermes::ui::PageJsonCallback done) {
    done(
        QJsonObject{
            {QStringLiteral("available"), true},
            {QStringLiteral("connectors"),
             QJsonArray{QJsonObject{
                 {QStringLiteral("connector"), QStringLiteral("notion")},
                 {QStringLiteral("connected"), false}}}}},
        QString());
  };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  page->show();
  QTest::qWait(20);
  auto* row = page->findChild<QWidget*>(QStringLiteral("connectorRow_notion"));
  QVERIFY(row != nullptr);
  QStringList labels;
  for (QLabel* label : row->findChildren<QLabel*>()) {
    labels.push_back(label->text());
  }
  QVERIFY2(labels.join(QStringLiteral(" | "))
               .contains(QStringLiteral("operation initiated")),
           qPrintable(labels.join(QStringLiteral(" | "))));
  auto* status =
      page->findChild<QLabel*>(QStringLiteral("connectorStatus_notion"));
  QVERIFY(status != nullptr);
  QCOMPARE(status->text(), QStringLiteral("Connector operation: initiated"));

  // A settled broadcast keeps its target state visible but no longer claims
  // the operation is live. A rebuilt page reads the same snapshot store.
  snapshot.account_operations[0].seq = 5;
  snapshot.account_operations[0].settled = true;
  snapshot.account_operations[0].settled_by = QStringLiteral("all_resolved");
  snapshot.account_operations[0].targets[0].state = QStringLiteral("connected");
  QWidget* settled = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                               context, nullptr);
  QVERIFY(settled != nullptr);
  settled->show();
  QTest::qWait(20);
  auto* settled_row =
      settled->findChild<QWidget*>(QStringLiteral("connectorRow_notion"));
  QVERIFY(settled_row != nullptr);
  labels.clear();
  for (QLabel* label : settled_row->findChildren<QLabel*>()) {
    labels.push_back(label->text());
  }
  QVERIFY2(
      !labels.join(QStringLiteral(" | ")).contains(QStringLiteral("operation")),
      qPrintable(labels.join(QStringLiteral(" | "))));
  auto* settled_status =
      settled->findChild<QLabel*>(QStringLiteral("connectorStatus_notion"));
  QVERIFY(settled_status != nullptr);
  QVERIFY(settled_status->text().isEmpty());
  delete settled;
  delete page;
}

void SettingsViewTest::capabilitiesTerminalBackendSelectsAndReloads() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString active = QStringLiteral("local");
  int list_calls = 0;
  context.list_terminal_backends = [&active, &list_calls](
                                       hermes::ui::PageJsonCallback done) {
    ++list_calls;
    done(
        QJsonObject{
            {QStringLiteral("active"), active},
            {QStringLiteral("backends"),
             QJsonArray{
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("local")},
                     {QStringLiteral("label"), QStringLiteral("Local")},
                     {QStringLiteral("description"),
                      QStringLiteral("Run on this machine")},
                     {QStringLiteral("status"), QStringLiteral("ready")}},
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("docker")},
                     {QStringLiteral("label"), QStringLiteral("Docker")},
                     {QStringLiteral("description"),
                      QStringLiteral("Run in a container")},
                     {QStringLiteral("status"), QStringLiteral("needs_setup")},
                     {QStringLiteral("detail"),
                      QStringLiteral("Docker CLI not found")}},
                 QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("daytona")},
                     {QStringLiteral("label"), QStringLiteral("Daytona")},
                     {QStringLiteral("description"),
                      QStringLiteral("Run in the cloud")},
                     {QStringLiteral("status"), QStringLiteral("ready")}}}}},
        QString());
  };
  QString selected;
  context.select_terminal_backend = [&active, &selected](
                                        const QString& backend,
                                        hermes::ui::PageJsonCallback done) {
    selected = backend;
    active = backend;
    done(QJsonObject{{QStringLiteral("ok"), true},
                     {QStringLiteral("backend"), backend}},
         QString());
  };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  page->show();
  QTest::qWait(20);
  QCOMPARE(list_calls, 1);

  auto* combo =
      page->findChild<QComboBox*>(QStringLiteral("terminalBackendSelect"));
  QVERIFY(combo != nullptr);
  QCOMPARE(combo->count(), 3);
  QCOMPARE(combo->currentData().toString(), QStringLiteral("local"));
  QVERIFY(combo->itemText(0).contains(QStringLiteral("ready")));
  auto* detail =
      page->findChild<QLabel*>(QStringLiteral("terminalBackendDetail"));
  QVERIFY(detail != nullptr);
  QVERIFY(detail->text().contains(QStringLiteral("Run on this machine")));

  const int docker = combo->findData(QStringLiteral("docker"));
  QVERIFY(docker >= 0);
  QCOMPARE(combo->itemData(docker, Qt::UserRole + 1).toString(),
           QStringLiteral("Run in a container\nDocker CLI not found"));
  combo->setCurrentIndex(docker);
  QCOMPARE(selected, QStringLiteral("docker"));
  QCOMPARE(list_calls, 2);
  QCOMPARE(combo->currentData().toString(), QStringLiteral("docker"));
  auto* status =
      page->findChild<QLabel*>(QStringLiteral("terminalBackendStatus"));
  QVERIFY(status != nullptr);
  QCOMPARE(status->text(), QStringLiteral("Terminal backend set to docker."));
  QVERIFY(detail->text().contains(QStringLiteral("Docker CLI not found")));
  delete page;
}

void SettingsViewTest::capabilitiesComputerUseStatusAndGrant() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState capabilities;
  capabilities.loaded = true;
  snapshot.pages.insert(QStringLiteral("capabilities"), capabilities);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  int status_calls = 0;
  context.get_computer_use_status = [&status_calls](
                                        hermes::ui::PageJsonCallback done) {
    ++status_calls;
    const bool refreshed = status_calls > 1;
    done(
        QJsonObject{
            {QStringLiteral("platform"), QStringLiteral("darwin")},
            {QStringLiteral("platform_supported"), true},
            {QStringLiteral("installed"), true},
            {QStringLiteral("version"), QStringLiteral("cua-driver 1.2")},
            {QStringLiteral("ready"), refreshed},
            {QStringLiteral("can_grant"), true},
            {QStringLiteral("accessibility"), refreshed},
            {QStringLiteral("screen_recording"), true},
            {QStringLiteral("checks"),
             refreshed ? QJsonArray()
                       : QJsonArray{QJsonObject{
                             {QStringLiteral("label"), QStringLiteral("TCC")},
                             {QStringLiteral("status"), QStringLiteral("fail")},
                             {QStringLiteral("message"),
                              QStringLiteral("Accessibility missing")}}}}},
        QString());
  };
  int grants = 0;
  context.grant_computer_use_permissions =
      [&grants](hermes::ui::PageJsonCallback done) {
        ++grants;
        done(QJsonObject{{QStringLiteral("ok"), true},
                         {QStringLiteral("name"),
                          QStringLiteral("computer-use-grant")}},
             QString());
      };
  QString polled_action;
  context.get_action_status =
      [&polled_action](const QString& name, hermes::ui::PageJsonCallback done) {
        polled_action = name;
        done(QJsonObject{{QStringLiteral("name"), name},
                         {QStringLiteral("running"), false}},
             QString());
      };

  QWidget* page = hermes::ui::BuildDataPage(QStringLiteral("capabilities"),
                                            context, nullptr);
  QVERIFY(page != nullptr);
  page->show();
  QTest::qWait(20);
  QCOMPARE(status_calls, 1);

  auto* status = page->findChild<QLabel*>(QStringLiteral("computerUseStatus"));
  QVERIFY(status != nullptr);
  QCOMPARE(status->text(), QStringLiteral("Computer Use is not ready."));
  auto* detail = page->findChild<QLabel*>(QStringLiteral("computerUseDetail"));
  QVERIFY(detail != nullptr);
  QVERIFY(detail->text().contains(QStringLiteral("cua-driver 1.2")));
  auto* accessibility =
      page->findChild<QLabel*>(QStringLiteral("computerUseAccessibility"));
  QVERIFY(accessibility != nullptr);
  QCOMPARE(accessibility->text(),
           QStringLiteral("Accessibility · Not granted"));
  auto* checks = page->findChild<QLabel*>(QStringLiteral("computerUseChecks"));
  QVERIFY(checks != nullptr);
  QVERIFY(checks->text().contains(QStringLiteral("Accessibility missing")));

  auto* grant =
      page->findChild<QPushButton*>(QStringLiteral("computerUseGrant"));
  QVERIFY(grant != nullptr);
  QVERIFY(!grant->isHidden());
  grant->click();
  QCOMPARE(grants, 1);
  QVERIFY(grant->text().contains(QStringLiteral("Waiting for approval")));
  QTest::qWait(1700);
  QCOMPARE(polled_action, QStringLiteral("computer-use-grant"));
  QCOMPARE(status_calls, 2);
  QCOMPARE(status->text(), QStringLiteral("Computer Use is ready."));
  QCOMPARE(accessibility->text(), QStringLiteral("Accessibility · Granted"));
  QVERIFY(grant->isHidden());

  auto* refresh =
      page->findChild<QPushButton*>(QStringLiteral("computerUseRefresh"));
  QVERIFY(refresh != nullptr);
  refresh->click();
  QCOMPARE(status_calls, 3);
  delete page;
}

void SettingsViewTest::quickEntrySubmitsOnEnter() {
  hermes::ui::QuickEntryDialog dialog;
  QString submitted;
  int submits = 0;
  dialog.SetSubmitHandler([&submitted, &submits](const QString& text) {
    submitted = text;
    ++submits;
  });
  dialog.SetSessionLabel(QStringLiteral("default · quick entry"));
  dialog.SetText(QStringLiteral("  summarize the release notes\n"));
  dialog.show();
  QTest::qWait(20);

  auto* editor =
      dialog.findChild<QPlainTextEdit*>(QStringLiteral("quickEntryText"));
  QVERIFY(editor != nullptr);
  QTest::keyClick(editor, Qt::Key_Return);
  QCOMPARE(submits, 1);
  QCOMPARE(submitted, QStringLiteral("summarize the release notes"));
  QVERIFY(dialog.isHidden());
  QVERIFY(dialog.Text().isEmpty());

  dialog.show();
  dialog.SetText(QStringLiteral("   "));
  QTest::keyClick(editor, Qt::Key_Return);
  QCOMPARE(submits, 1);
  auto* hint = dialog.findChild<QLabel*>(QStringLiteral("quickEntryHint"));
  QVERIFY(hint != nullptr);
  QCOMPARE(hint->text(), QStringLiteral("Type a prompt first."));

  dialog.SetText(QStringLiteral("line one"));
  QTest::keyClick(editor, Qt::Key_Return, Qt::ShiftModifier);
  QCOMPARE(submits, 1);
  QVERIFY(dialog.Text().contains(QStringLiteral("\n")));
}

void SettingsViewTest::rendersStarmapGraphAndDeletesNode() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");

  const QJsonArray nodes{
      QJsonObject{{QStringLiteral("id"), QStringLiteral("skill-a")},
                  {QStringLiteral("label"), QStringLiteral("Pricing research")},
                  {QStringLiteral("kind"), QStringLiteral("skill")},
                  {QStringLiteral("category"), QStringLiteral("research")},
                  {QStringLiteral("useCount"), 4},
                  {QStringLiteral("state"), QStringLiteral("active")},
                  {QStringLiteral("pinned"), true}},
      QJsonObject{{QStringLiteral("id"), QStringLiteral("memory-1")},
                  {QStringLiteral("label"), QStringLiteral("Terse answers")},
                  {QStringLiteral("kind"), QStringLiteral("memory")},
                  {QStringLiteral("memorySource"), QStringLiteral("profile")},
                  {QStringLiteral("category"), QStringLiteral("memory")},
                  {QStringLiteral("useCount"), 0},
                  {QStringLiteral("state"), QStringLiteral("active")}}};
  hermes::ui::PageState starmap;
  starmap.loaded = true;
  starmap.data.insert(
      QStringLiteral("graph"),
      QJsonObject{
          {QStringLiteral("nodes"), nodes},
          {QStringLiteral("edges"),
           QJsonArray{QJsonObject{
               {QStringLiteral("source"), QStringLiteral("skill-a")},
               {QStringLiteral("target"), QStringLiteral("memory-1")}}}},
          {QStringLiteral("clusters"),
           QJsonArray{QJsonObject{
               {QStringLiteral("category"), QStringLiteral("research")},
               {QStringLiteral("count"), 1}}}},
          {QStringLiteral("memory"),
           QJsonArray{QJsonObject{
               {QStringLiteral("title"), QStringLiteral("User preference")},
               {QStringLiteral("body"), QStringLiteral("Prefer short replies")},
               {QStringLiteral("source"), QStringLiteral("profile")}}}},
          {QStringLiteral("stats"),
           QJsonObject{{QStringLiteral("learned_skills"), 1},
                       {QStringLiteral("memory_nodes"), 1},
                       {QStringLiteral("memory_skill_edges"), 1}}}});
  snapshot.pages.insert(QStringLiteral("starmap"), starmap);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString acted_page;
  QString acted_method;
  QString acted_path;
  QJsonObject acted_body;
  context.action = [&acted_page, &acted_method, &acted_path, &acted_body](
                       const QString& page, const QString& method,
                       const QString& path, const QJsonObject& body) {
    acted_page = page;
    acted_method = method;
    acted_path = path;
    acted_body = body;
  };
  QString requested_id;
  context.get_learning_node =
      [&requested_id](const QString& id, hermes::ui::PageJsonCallback done) {
        requested_id = id;
        done(
            QJsonObject{
                {QStringLiteral("ok"), true},
                {QStringLiteral("id"), id},
                {QStringLiteral("kind"), QStringLiteral("skill")},
                {QStringLiteral("label"), QStringLiteral("Pricing research")},
                {QStringLiteral("content"),
                 QStringLiteral("# Pricing research\nUse citations.")}},
            QString());
      };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("starmap"), context, nullptr);
  QVERIFY(page != nullptr);
  page->show();
  QTest::qWait(20);

  auto* title = page->findChild<QLabel*>(QStringLiteral("starmapTitle_0"));
  QVERIFY(title != nullptr);
  QVERIFY(title->text().contains(QStringLiteral("Pricing research")));
  auto* meta = page->findChild<QLabel*>(QStringLiteral("starmapMeta_0"));
  QVERIFY(meta != nullptr);
  QVERIFY(meta->text().contains(QStringLiteral("4 uses")));
  QVERIFY(meta->text().contains(QStringLiteral("pinned")));
  auto* edge = page->findChild<QLabel*>(QStringLiteral("starmapEdge_0"));
  QVERIFY(edge != nullptr);
  QCOMPARE(edge->text(), QStringLiteral("skill-a → memory-1"));
  auto* memory = page->findChild<QLabel*>(QStringLiteral("starmapMemory_0"));
  QVERIFY(memory != nullptr);
  QVERIFY(memory->text().contains(QStringLiteral("User preference")));
  QVERIFY(memory->text().contains(QStringLiteral("Prefer short replies")));

  auto* view = page->findChild<QPushButton*>(QStringLiteral("starmapView_0"));
  QVERIFY(view != nullptr);
  view->click();
  QTest::qWait(20);
  QCOMPARE(requested_id, QStringLiteral("skill-a"));
  auto* dialog =
      page->findChild<QDialog*>(QStringLiteral("starmapDetailDialog"));
  QVERIFY(dialog != nullptr);
  auto* content = dialog->findChild<QPlainTextEdit*>(
      QStringLiteral("starmapDetailContent"));
  QVERIFY(content != nullptr);
  QVERIFY(content->toPlainText().contains(QStringLiteral("Use citations.")));

  auto* remove =
      page->findChild<QPushButton*>(QStringLiteral("starmapDelete_0"));
  QVERIFY(remove != nullptr);
  remove->click();
  QCOMPARE(acted_page, QStringLiteral("starmap"));
  QCOMPARE(acted_method, QStringLiteral("DELETE"));
  QCOMPARE(acted_path, QStringLiteral("/api/learning/node?profile=default"));
  QCOMPARE(acted_body.value(QStringLiteral("id")).toString(),
           QStringLiteral("skill-a"));
  delete page;
}

void SettingsViewTest::botsEditModelDescriptionSoulAndExportArchive() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");

  hermes::ui::ModelProvider openai;
  openai.name = QStringLiteral("OpenAI");
  openai.slug = QStringLiteral("openai");
  openai.models =
      QStringList{QStringLiteral("gpt-5"), QStringLiteral("gpt-5-mini")};
  snapshot.model_providers.push_back(openai);

  hermes::ui::PageState profiles;
  profiles.loaded = true;
  profiles.data.insert(
      QStringLiteral("profiles"),
      QJsonObject{
          {QStringLiteral("profiles"),
           QJsonArray{
               QJsonObject{
                   {QStringLiteral("name"), QStringLiteral("default")},
                   {QStringLiteral("is_default"), true},
                   {QStringLiteral("gateway_running"), true},
                   {QStringLiteral("provider"), QStringLiteral("openai")},
                   {QStringLiteral("model"), QStringLiteral("gpt-5")},
                   {QStringLiteral("description"),
                    QStringLiteral("Main assistant")}},
               QJsonObject{
                   {QStringLiteral("name"), QStringLiteral("work")},
                   {QStringLiteral("gateway_running"), false},
                   {QStringLiteral("provider"), QStringLiteral("openai")},
                   {QStringLiteral("model"), QStringLiteral("gpt-5-mini")}}}}});
  snapshot.pages.insert(QStringLiteral("profiles"), profiles);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString acted_path;
  QJsonObject acted_body;
  context.action = [&acted_path, &acted_body](
                       const QString& page, const QString& method,
                       const QString& path, const QJsonObject& body) {
    QCOMPARE(page, QStringLiteral("profiles"));
    QCOMPARE(method, QStringLiteral("PUT"));
    acted_path = path;
    acted_body = body;
  };
  QString soul_name;
  QString soul_content;
  context.get_profile_soul = [](const QString& name,
                                hermes::ui::PageJsonCallback done) {
    done(QJsonObject{{QStringLiteral("content"),
                      QStringLiteral("You are %1.").arg(name)}},
         QString());
  };
  context.set_profile_soul = [&soul_name, &soul_content](
                                 const QString& name, const QString& content,
                                 hermes::ui::PageJsonCallback done) {
    soul_name = name;
    soul_content = content;
    done(QJsonObject(), QString());
  };
  QString exported_name;
  context.export_profile = [&exported_name](const QString& name,
                                            hermes::ui::PageJsonCallback done) {
    exported_name = name;
    done(QJsonObject{{QStringLiteral("archive"),
                      QStringLiteral("/tmp/%1.tar.gz").arg(name)}},
         QString());
  };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("profiles"), context, nullptr);
  QVERIFY(page != nullptr);
  QVERIFY(page->findChild<QWidget*>(QStringLiteral("profileRow_work")) !=
          nullptr);
  QVERIFY(page->findChild<QWidget*>(QStringLiteral("profilesImport")) !=
          nullptr);

  auto* edit_model =
      page->findChild<QPushButton*>(QStringLiteral("profileModel_work"));
  QVERIFY(edit_model != nullptr);
  edit_model->click();
  auto* model_dialog =
      page->findChild<QDialog*>(QStringLiteral("botModelDialog_work"));
  QVERIFY(model_dialog != nullptr);
  auto* provider =
      model_dialog->findChild<QComboBox*>(QStringLiteral("botModelProvider"));
  auto* model =
      model_dialog->findChild<QComboBox*>(QStringLiteral("botModelValue"));
  QVERIFY(provider != nullptr);
  QVERIFY(model != nullptr);
  provider->setCurrentText(QStringLiteral("openai"));
  model->setCurrentText(QStringLiteral("gpt-5"));
  auto* save_model =
      model_dialog->findChild<QPushButton*>(QStringLiteral("botModelSubmit"));
  QVERIFY(save_model != nullptr);
  save_model->click();
  QCOMPARE(acted_path, QStringLiteral("/api/profiles/work/model"));
  QCOMPARE(acted_body.value(QStringLiteral("provider")).toString(),
           QStringLiteral("openai"));
  QCOMPARE(acted_body.value(QStringLiteral("model")).toString(),
           QStringLiteral("gpt-5"));

  auto* edit_description =
      page->findChild<QPushButton*>(QStringLiteral("profileDescription_work"));
  QVERIFY(edit_description != nullptr);
  edit_description->click();
  auto* description_dialog =
      page->findChild<QDialog*>(QStringLiteral("botDescriptionDialog_work"));
  QVERIFY(description_dialog != nullptr);
  auto* description_text = description_dialog->findChild<QPlainTextEdit*>(
      QStringLiteral("botDescriptionText"));
  QVERIFY(description_text != nullptr);
  description_text->setPlainText(QStringLiteral("Research bot"));
  auto* save_description = description_dialog->findChild<QPushButton*>(
      QStringLiteral("botDescriptionSubmit"));
  QVERIFY(save_description != nullptr);
  save_description->click();
  QCOMPARE(acted_path, QStringLiteral("/api/profiles/work/description"));
  QCOMPARE(acted_body.value(QStringLiteral("description")).toString(),
           QStringLiteral("Research bot"));

  auto* edit_soul =
      page->findChild<QPushButton*>(QStringLiteral("profileSoul_work"));
  QVERIFY(edit_soul != nullptr);
  edit_soul->click();
  auto* soul_dialog =
      page->findChild<QDialog*>(QStringLiteral("botSoulDialog_work"));
  QVERIFY(soul_dialog != nullptr);
  auto* soul_text =
      soul_dialog->findChild<QPlainTextEdit*>(QStringLiteral("botSoulText"));
  QVERIFY(soul_text != nullptr);
  QCOMPARE(soul_text->toPlainText(), QStringLiteral("You are work."));
  soul_text->setPlainText(QStringLiteral("You are a careful researcher."));
  auto* save_soul =
      soul_dialog->findChild<QPushButton*>(QStringLiteral("botSoulSubmit"));
  QVERIFY(save_soul != nullptr);
  save_soul->click();
  QCOMPARE(soul_name, QStringLiteral("work"));
  QCOMPARE(soul_content, QStringLiteral("You are a careful researcher."));

  auto* export_bot =
      page->findChild<QPushButton*>(QStringLiteral("profileExport_work"));
  QVERIFY(export_bot != nullptr);
  export_bot->click();
  QCOMPARE(exported_name, QStringLiteral("work"));
}

void SettingsViewTest::artifactsSpotEditorSavesAndShowsImages() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.artifacts_cwd = QStringLiteral("/tmp/work");
  snapshot.artifacts_preview = QStringLiteral("/tmp/work/photo.png");

  hermes::ui::PageState artifacts;
  artifacts.loaded = true;
  artifacts.data.insert(
      QStringLiteral("listing"),
      QJsonObject{
          {QStringLiteral("entries"),
           QJsonArray{QJsonObject{
               {QStringLiteral("name"), QStringLiteral("photo.png")},
               {QStringLiteral("path"), QStringLiteral("/tmp/work/photo.png")},
               {QStringLiteral("isDirectory"), false}}}}});
  artifacts.data.insert(
      QStringLiteral("preview"),
      QJsonObject{{QStringLiteral("text"), QStringLiteral("hello")},
                  {QStringLiteral("mimeType"), QStringLiteral("image/png")},
                  {QStringLiteral("byteSize"), 12}});
  artifacts.data.insert(
      QStringLiteral("previewMedia"),
      QJsonObject{
          {QStringLiteral("dataUrl"),
           QStringLiteral(
               "data:image/png;base64,"
               "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8"
               "z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==")}});
  snapshot.pages.insert(QStringLiteral("artifacts"), artifacts);

  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  QString saved_path;
  QString saved_text;
  context.save_file = [&saved_path, &saved_text](const QString& path,
                                                 const QString& text) {
    saved_path = path;
    saved_text = text;
  };

  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("artifacts"), context, nullptr);
  QVERIFY(page != nullptr);
  auto* editor =
      page->findChild<QPlainTextEdit*>(QStringLiteral("artifactsEditor"));
  QVERIFY(editor != nullptr);
  // The spot editor is seeded with the bytes read from the gateway.
  QCOMPARE(editor->toPlainText(), QStringLiteral("hello"));
  QVERIFY(page->findChild<QWidget*>(QStringLiteral("artifactsImage")) !=
          nullptr);

  auto* save = page->findChild<QPushButton*>(QStringLiteral("artifactsSave"));
  QVERIFY(save != nullptr);
  QVERIFY(save->isEnabled());
  editor->setPlainText(QStringLiteral("hello edited"));
  save->click();
  QCOMPARE(saved_path, QStringLiteral("/tmp/work/photo.png"));
  QCOMPARE(saved_text, QStringLiteral("hello edited"));
}

void SettingsViewTest::artifactsPreviewRendersMarkdownHtmlAndCsv() {
  const auto build = [](const QString& path, const QString& mime,
                        const QString& text) {
    hermes::ui::ProfileSnapshot snapshot;
    snapshot.name = QStringLiteral("default");
    snapshot.artifacts_cwd = QStringLiteral("/tmp/work");
    snapshot.artifacts_preview = path;
    hermes::ui::PageState artifacts;
    artifacts.loaded = true;
    artifacts.data.insert(
        QStringLiteral("listing"),
        QJsonObject{{QStringLiteral("entries"), QJsonArray()}});
    artifacts.data.insert(QStringLiteral("preview"),
                          QJsonObject{{QStringLiteral("text"), text},
                                      {QStringLiteral("mimeType"), mime}});
    snapshot.pages.insert(QStringLiteral("artifacts"), artifacts);
    hermes::ui::PageContext context;
    context.theme = hermes::ui::Theme::Light();
    context.snapshot = &snapshot;
    context.suppress_page_heading = true;
    return hermes::ui::BuildDataPage(QStringLiteral("artifacts"), context,
                                     nullptr);
  };

  std::unique_ptr<QWidget> markdown(build(
      QStringLiteral("/tmp/work/readme.md"), QStringLiteral("text/markdown"),
      QStringLiteral("# Release notes\n\n> Context: ") +
          QString(220, QLatin1Char('q')) +
          QStringLiteral("\n\n**Important** change.\n\n"
                         "| Surface | Status |\n| --- | --- |\n"
                         "| Preview | fixed |")));
  QVERIFY(markdown != nullptr);
  auto* markdown_browser = markdown->findChild<QTextBrowser*>(
      QStringLiteral("artifactsPreviewBrowser"));
  QVERIFY(markdown_browser != nullptr);
  QVERIFY(markdown_browser->toPlainText().contains(
      QStringLiteral("Release notes")));
  QVERIFY(
      markdown_browser->toPlainText().contains(QStringLiteral("Important")));
  QVERIFY(markdown_browser->toPlainText().contains(QStringLiteral("fixed")));
  // The preview must not inherit Qt's 9 pt Markdown default or its grid-style
  // table borders: TODO.md and READMEs are docs, not pasted HTML.
  QCOMPARE(markdown_browser->document()->defaultFont().pixelSize(),
           hermes::ui::Theme::Light().UiFont(13, QFont::Normal).pixelSize());
  QVERIFY(markdown_browser->document()->documentMargin() >= 12);
  QTextTable* table = nullptr;
  for (QTextFrame::iterator it =
           markdown_browser->document()->rootFrame()->begin();
       !it.atEnd(); ++it) {
    table = qobject_cast<QTextTable*>(it.currentFrame());
    if (table != nullptr) {
      break;
    }
  }
  QVERIFY(table != nullptr);
  QCOMPARE(table->format().border(), 0.0);
  QCOMPARE(table->format().cellPadding(), 7.0);
  QCOMPARE(table->format().width().type(), QTextLength::PercentageLength);
  QCOMPARE(table->format().width().rawValue(), 100.0);
  markdown_browser->resize(320, 500);
  markdown_browser->document()->setTextWidth(300);
  bool saw_quote = false;
  for (QTextBlock block = markdown_browser->document()->begin();
       block.isValid(); block = block.next()) {
    const QTextBlockFormat format = block.blockFormat();
    if (format.intProperty(QTextFormat::BlockQuoteLevel) <= 0) {
      continue;
    }
    saw_quote = true;
    QCOMPARE(format.leftMargin(), 14.0);
    QCOMPARE(format.rightMargin(), 12.0);
    QVERIFY(format.background().style() != Qt::NoBrush);
    QVERIFY(block.layout() != nullptr);
    QVERIFY2(block.layout()->lineCount() > 1,
             "a long quote preview must wrap inside its card");
  }
  QVERIFY(saw_quote);
  QVERIFY(markdown->findChild<QPlainTextEdit*>(
              QStringLiteral("artifactsEditor")) != nullptr);
  QVERIFY(markdown->findChild<QPushButton*>(
              QStringLiteral("artifactsBrowse")) != nullptr);

  std::unique_ptr<QWidget> html(
      build(QStringLiteral("/tmp/work/page.html"), QStringLiteral("text/html"),
            QStringLiteral("<h1>Status</h1><p>All good.</p>")));
  QVERIFY(html != nullptr);
  auto* html_browser =
      html->findChild<QTextBrowser*>(QStringLiteral("artifactsPreviewBrowser"));
  QVERIFY(html_browser != nullptr);
  QVERIFY(html_browser->toPlainText().contains(QStringLiteral("Status")));
  QVERIFY(html_browser->toPlainText().contains(QStringLiteral("All good")));

  std::unique_ptr<QWidget> csv(
      build(QStringLiteral("/tmp/work/data.csv"), QStringLiteral("text/csv"),
            QStringLiteral("name,value\nalpha,1\nbeta,2")));
  QVERIFY(csv != nullptr);
  auto* csv_browser =
      csv->findChild<QTextBrowser*>(QStringLiteral("artifactsPreviewBrowser"));
  QVERIFY(csv_browser != nullptr);
  QVERIFY(csv_browser->toPlainText().contains(QStringLiteral("name")));
  QVERIFY(csv_browser->toPlainText().contains(QStringLiteral("beta")));
}

void SettingsViewTest::artifactsScrollSurvivesPageRebuild() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.config_loaded = true;
  snapshot.artifacts_cwd = QStringLiteral("/tmp/work");
  snapshot.artifacts_preview = QStringLiteral("/tmp/work/report.md");
  QJsonArray entries;
  for (int i = 0; i < 80; ++i) {
    entries.append(QJsonObject{
        {QStringLiteral("name"), QStringLiteral("file-%1.txt").arg(i)},
        {QStringLiteral("path"),
         QStringLiteral("/tmp/work/file-%1.txt").arg(i)},
        {QStringLiteral("isDirectory"), false},
    });
  }
  QString preview_text = QStringLiteral("# Release notes\n");
  for (int i = 0; i < 160; ++i) {
    preview_text += QStringLiteral("- row %1\n").arg(i);
  }
  hermes::ui::PageState artifacts;
  artifacts.loaded = true;
  artifacts.data.insert(QStringLiteral("listing"),
                        QJsonObject{{QStringLiteral("entries"), entries}});
  artifacts.data.insert(QStringLiteral("preview"),
                        QJsonObject{{QStringLiteral("text"), preview_text},
                                    {QStringLiteral("mimeType"),
                                     QStringLiteral("text/markdown")}});
  snapshot.pages.insert(QStringLiteral("artifacts"), artifacts);

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  view.resize(1100, 260);
  view.show();
  view.SetSnapshot(snapshot);
  view.OpenPage(QStringLiteral("artifacts"));
  QCoreApplication::processEvents();

  auto* outer = view.findChild<QScrollArea*>(QStringLiteral("settingsScroll"));
  auto* list = view.findChild<QListWidget*>(QStringLiteral("artifactsList"));
  auto* browser =
      view.findChild<QTextBrowser*>(QStringLiteral("artifactsPreviewBrowser"));
  QVERIFY(outer != nullptr);
  QVERIFY(list != nullptr);
  QVERIFY(browser != nullptr);
  outer->setFixedHeight(320);
  browser->setFixedHeight(180);
  QCoreApplication::processEvents();
  QVERIFY(outer->verticalScrollBar()->maximum() > 0);
  QVERIFY(browser->verticalScrollBar()->maximum() > 0);
  outer->verticalScrollBar()->setValue(8);
  browser->verticalScrollBar()->setValue(13);
  const int kept_outer = outer->verticalScrollBar()->value();
  const int kept_browser = browser->verticalScrollBar()->value();
  QCOMPARE(kept_outer, 8);
  QCOMPARE(kept_browser, 13);

  // A genuine data change forces a page rebuild. The artifact preview must
  // keep both the page and document scroll after the deferred layout pass.
  auto page_it = snapshot.pages.find(QStringLiteral("artifacts"));
  QVERIFY(page_it != snapshot.pages.end());
  QJsonObject listing =
      page_it->data.value(QStringLiteral("listing")).toObject();
  QJsonArray changed_entries =
      listing.value(QStringLiteral("entries")).toArray();
  changed_entries.append(QJsonObject{
      {QStringLiteral("name"), QStringLiteral("file-new.txt")},
      {QStringLiteral("path"), QStringLiteral("/tmp/work/file-new.txt")},
      {QStringLiteral("isDirectory"), false},
  });
  listing.insert(QStringLiteral("entries"), changed_entries);
  page_it->data.insert(QStringLiteral("listing"), listing);
  snapshot.revision = 99;
  view.SetSnapshot(snapshot);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  auto* next_outer =
      view.findChild<QScrollArea*>(QStringLiteral("settingsScroll"));
  auto* next_browser =
      view.findChild<QTextBrowser*>(QStringLiteral("artifactsPreviewBrowser"));
  QVERIFY(next_outer != nullptr);
  QVERIFY(next_browser != nullptr);
  // Constrain the replacement preview before the deferred scroll restore
  // runs, matching the viewport the old widget had.
  next_browser->setFixedHeight(180);
  QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
  QTest::qWait(50);
  QVERIFY(next_browser->verticalScrollBar()->maximum() > 0);
  QCOMPARE(next_outer->verticalScrollBar()->value(), kept_outer);
  QCOMPARE(next_browser->verticalScrollBar()->value(), kept_browser);

  // Periodic profile snapshots can change the global revision without any
  // artifact-page data change. They must not rebuild this page or move it.
  QPointer<QScrollArea> tracked_outer(next_outer);
  QPointer<QTextBrowser> tracked_browser(next_browser);
  for (int i = 0; i < 5; ++i) {
    snapshot.revision = 100 + i;
    view.SetSnapshot(snapshot);
    QCoreApplication::processEvents();
    QVERIFY(tracked_outer != nullptr);
    QVERIFY(tracked_browser != nullptr);
    QCOMPARE(tracked_outer->verticalScrollBar()->value(), kept_outer);
    QCOMPARE(tracked_browser->verticalScrollBar()->value(), kept_browser);
  }
}

void SettingsViewTest::notificationsPersistKindsAndSendTest() {
  hermes::ui::SetNativePref(QStringLiteral("notifications.enabled"),
                            QStringLiteral("1"));
  hermes::ui::SetNativePref(QStringLiteral("notifications.mute_sounds"),
                            QStringLiteral("0"));
  for (hermes::ui::NotificationKind kind : hermes::ui::kNotificationKinds) {
    hermes::ui::SetNativePref(hermes::ui::NotificationKindPrefKey(kind),
                              QStringLiteral("1"));
  }

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QString title;
  QString body;
  bool silent = false;
  bool deliver = true;
  view.on_send_notification = [&title, &body, &silent, &deliver](
                                  const QString& sent_title,
                                  const QString& sent_body, bool sent_silent,
                                  QString* error) {
    title = sent_title;
    body = sent_body;
    silent = sent_silent;
    if (!deliver) {
      if (error != nullptr) {
        *error = QStringLiteral("Notifications are not permitted.");
      }
      return false;
    }
    return true;
  };
  view.show();
  view.SetSnapshot(DataSnapshot());
  view.OpenPage(QStringLiteral("notifications"));
  QCoreApplication::processEvents();

  auto* master = view.findChild<QCheckBox*>(
      QStringLiteral("notifyToggle_notifications.enabled"));
  auto* approval = view.findChild<QCheckBox*>(
      QStringLiteral("notifyToggle_notifications.kind.approval"));
  auto* mute = view.findChild<QCheckBox*>(
      QStringLiteral("notifyToggle_notifications.mute_sounds"));
  auto* test = view.findChild<QPushButton*>(QStringLiteral("notifyTest"));
  auto* status = view.findChild<QLabel*>(QStringLiteral("notifyTestStatus"));
  QVERIFY(master != nullptr);
  QVERIFY(approval != nullptr);
  QVERIFY(mute != nullptr);
  QVERIFY(test != nullptr);
  QVERIFY(status != nullptr);
  QVERIFY(master->isChecked());
  QVERIFY(approval->isChecked());
  QVERIFY(!mute->isChecked());
  QVERIFY(test->isEnabled());

  master->setChecked(false);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("notifications.enabled")),
           QStringLiteral("0"));
  QVERIFY2(!approval->isEnabled(),
           "kind toggles follow the master notification switch");
  master->setChecked(true);
  QVERIFY(approval->isEnabled());

  approval->setChecked(false);
  QCOMPARE(hermes::ui::NativePref(hermes::ui::NotificationKindPrefKey(
               hermes::ui::NotificationKind::kApproval)),
           QStringLiteral("0"));
  approval->setChecked(true);

  mute->setChecked(true);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("notifications.mute_sounds")),
           QStringLiteral("1"));
  test->click();
  QCOMPARE(title, QStringLiteral("Hermes"));
  QCOMPARE(body, QStringLiteral("Test notification from Hermes."));
  QVERIFY(silent);
  QVERIFY(status->text().contains(QStringLiteral("Test notification sent")));

  deliver = false;
  test->click();
  QVERIFY(status->text().contains(QStringLiteral("not permitted")));
}

void SettingsViewTest::preferencesApplyKeybindsAndFontScale() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QHash<QString, QString> overrides;
  double scale = 0.0;
  view.on_keybinds = [&overrides](const QHash<QString, QString>& map) {
    overrides = map;
  };
  view.on_font_scale = [&scale](double value) { scale = value; };

  hermes::ui::SetNativePref(QStringLiteral("keybind.command_palette"),
                            QStringLiteral("Ctrl+Shift+P"));
  hermes::ui::SetNativePref(QStringLiteral("native.font_scale"),
                            QStringLiteral("1.10"));

  view.ApplyStoredPreferences();

  QCOMPARE(overrides.value(QStringLiteral("command_palette")),
           QStringLiteral("Ctrl+Shift+P"));
  QCOMPARE(scale, 1.10);

  hermes::ui::SetNativePref(QStringLiteral("keybind.command_palette"),
                            QString());
  hermes::ui::SetNativePref(QStringLiteral("native.font_scale"), QString());
}

void SettingsViewTest::appearancePagePersistsDisplayPreferences() {
  const QStringList keys{
      QStringLiteral("appearance.chat_text_scale"),
      QStringLiteral("appearance.message_width"),
      QStringLiteral("appearance.show_timestamps"),
      QStringLiteral("appearance.reasoning_collapsed"),
      QStringLiteral("appearance.session_density"),
      QStringLiteral("appearance.ui_font"),
      QStringLiteral("appearance.mono_font"),
      QStringLiteral("appearance.user_bubble_transparency"),
      QStringLiteral("appearance.window_opacity"),
      QStringLiteral("appearance.animations"),
      QStringLiteral("appearance.code_scale"),
  };
  for (const QString& key : keys) {
    hermes::ui::SetNativePref(key, QString());
  }
  hermes::ui::SetNativePref(QStringLiteral("native.font_scale"), QString());

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  view.show();
  view.SetSnapshot(DataSnapshot());
  int appearance_changes = 0;
  view.on_appearance = [&appearance_changes]() { ++appearance_changes; };
  view.OpenPage(QStringLiteral("appearance"));

  auto* density =
      view.findChild<QComboBox*>(QStringLiteral("appearanceSessionDensity"));
  auto* width =
      view.findChild<QComboBox*>(QStringLiteral("appearanceMessageWidth"));
  auto* timestamps =
      view.findChild<QCheckBox*>(QStringLiteral("appearanceShowTimestamps"));
  auto* reasoning = view.findChild<QCheckBox*>(
      QStringLiteral("appearanceReasoningCollapsed"));
  auto* animations =
      view.findChild<QCheckBox*>(QStringLiteral("appearanceAnimations"));
  auto* code =
      view.findChild<QDoubleSpinBox*>(QStringLiteral("appearanceCodeScale"));
  auto* ui_scale =
      view.findChild<QDoubleSpinBox*>(QStringLiteral("appearanceFontScale"));
  auto* chat_scale = view.findChild<QDoubleSpinBox*>(
      QStringLiteral("appearanceChatTextScale"));
  auto* ui_font =
      view.findChild<QComboBox*>(QStringLiteral("appearanceUiFont"));
  auto* mono_font =
      view.findChild<QComboBox*>(QStringLiteral("appearanceMonoFont"));
  auto* bubble =
      view.findChild<QSlider*>(QStringLiteral("appearanceUserBubble"));
  auto* opacity =
      view.findChild<QSlider*>(QStringLiteral("appearanceWindowOpacity"));
  QVERIFY(density != nullptr);
  QVERIFY(width != nullptr);
  QVERIFY(timestamps != nullptr);
  QVERIFY(reasoning != nullptr);
  QVERIFY(animations != nullptr);
  QVERIFY(code != nullptr);
  QVERIFY(ui_scale != nullptr);
  QVERIFY(chat_scale != nullptr);
  QVERIFY(ui_font != nullptr);
  QVERIFY(mono_font != nullptr);
  QVERIFY(bubble != nullptr);
  QVERIFY(opacity != nullptr);

  // Native defaults preserve the old behaviour.
  QCOMPARE(density->currentData().toString(), QStringLiteral("compact"));
  QCOMPARE(width->currentData().toInt(), 720);
  QVERIFY(timestamps->isChecked());
  QVERIFY(reasoning->isChecked());
  QVERIFY(animations->isChecked());
  QCOMPARE(code->value(), 1.0);
  QCOMPARE(ui_scale->value(), 1.0);
  QCOMPARE(chat_scale->value(), 1.0);
  QCOMPARE(ui_font->currentIndex(), 0);
  QCOMPARE(mono_font->currentIndex(), 0);
  QCOMPARE(bubble->value(), 0);
  QCOMPARE(opacity->value(), 100);

  density->setCurrentIndex(density->findData(QStringLiteral("detailed")));
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.session_density")),
           QStringLiteral("detailed"));
  width->setCurrentIndex(width->findData(860));
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.message_width")),
           QStringLiteral("860"));
  timestamps->setChecked(false);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.show_timestamps")),
           QStringLiteral("0"));
  reasoning->setChecked(false);
  QCOMPARE(
      hermes::ui::NativePref(QStringLiteral("appearance.reasoning_collapsed")),
      QStringLiteral("0"));
  animations->setChecked(false);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.animations")),
           QStringLiteral("0"));
  code->setValue(1.25);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.code_scale")),
           QStringLiteral("1.25"));
  ui_scale->setValue(1.15);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("native.font_scale")),
           QStringLiteral("1.15"));
  chat_scale->setValue(1.2);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.chat_text_scale")),
           QStringLiteral("1.20"));
  if (ui_font->count() > 1) {
    ui_font->setCurrentIndex(1);
    QVERIFY(!hermes::ui::NativePref(QStringLiteral("appearance.ui_font"))
                 .isEmpty());
  }
  if (mono_font->count() > 1) {
    mono_font->setCurrentIndex(1);
    QVERIFY(!hermes::ui::NativePref(QStringLiteral("appearance.mono_font"))
                 .isEmpty());
  }
  bubble->setValue(35);
  QCOMPARE(hermes::ui::NativePref(
               QStringLiteral("appearance.user_bubble_transparency")),
           QStringLiteral("35"));
  opacity->setValue(82);
  QCOMPARE(hermes::ui::NativePref(QStringLiteral("appearance.window_opacity")),
           QStringLiteral("82"));
  QVERIFY(appearance_changes >= 11);

  for (const QString& key : keys) {
    hermes::ui::SetNativePref(key, QString());
  }
  hermes::ui::SetNativePref(QStringLiteral("native.font_scale"), QString());
}

void SettingsViewTest::appearancePreferencesClampAndApplyTheme() {
  QHash<QString, QString> values{
      {QStringLiteral("native.font_scale"), QStringLiteral("1.30")},
      {QStringLiteral("appearance.chat_text_scale"), QStringLiteral("2.0")},
      {QStringLiteral("appearance.message_width"), QStringLiteral("400")},
      {QStringLiteral("appearance.session_density"),
       QStringLiteral("detailed")},
      {QStringLiteral("appearance.show_timestamps"), QStringLiteral("off")},
      {QStringLiteral("appearance.user_bubble_transparency"),
       QStringLiteral("50")},
      {QStringLiteral("appearance.window_opacity"), QStringLiteral("20")},
      {QStringLiteral("appearance.code_scale"), QStringLiteral("2.0")},
      {QStringLiteral("appearance.ui_font"), QStringLiteral("Avenir")},
      {QStringLiteral("appearance.mono_font"), QStringLiteral("Fira Code")},
  };
  const auto get = [&values](const QString& key) { return values.value(key); };
  const hermes::ui::AppearancePreferences prefs =
      hermes::ui::LoadAppearancePreferences(get);
  QCOMPARE(prefs.ui_scale, 1.30);
  QCOMPARE(prefs.chat_text_scale, 1.4);
  QCOMPARE(prefs.message_width, 520);
  QCOMPARE(prefs.session_density, hermes::ui::SessionListDensity::kDetailed);
  QVERIFY(!prefs.show_timestamps);
  QCOMPARE(prefs.user_bubble_transparency, 50);
  QCOMPARE(prefs.window_opacity, 60);
  QCOMPARE(prefs.code_scale, 1.5);

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  const QColor base = theme.user_bubble;
  hermes::ui::ApplyAppearanceToTheme(prefs, base, QStringLiteral("System"),
                                     QStringLiteral("Mono"), &theme);
  QCOMPARE(theme.font_scale, 1.30);
  QCOMPARE(theme.ui_family, QStringLiteral("Avenir"));
  QCOMPARE(theme.mono_family, QStringLiteral("Fira Code"));
  QCOMPARE(theme.user_bubble.alpha(), 128);

  values.remove(QStringLiteral("appearance.ui_font"));
  values.remove(QStringLiteral("appearance.mono_font"));
  values.remove(QStringLiteral("appearance.user_bubble_transparency"));
  const hermes::ui::AppearancePreferences defaults =
      hermes::ui::LoadAppearancePreferences(get);
  hermes::ui::ApplyAppearanceToTheme(defaults, base, QStringLiteral("System"),
                                     QStringLiteral("Mono"), &theme);
  QCOMPARE(theme.ui_family, QStringLiteral("System"));
  QCOMPARE(theme.mono_family, QStringLiteral("Mono"));
  QCOMPARE(theme.user_bubble.alpha(), base.alpha());
}

void SettingsViewTest::voiceSectionExposesAudioDevices() {
  hermes::ui::SetNativePref(QStringLiteral("audio.output_device"), QString());
  hermes::ui::SetNativePref(QStringLiteral("audio.input_device"), QString());
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  view.SetSnapshot(Snapshot());
  QString changed_key;
  QString changed_value;
  view.on_pref_changed = [&changed_key, &changed_value](const QString& key,
                                                        const QString& value) {
    changed_key = key;
    changed_value = value;
  };
  view.OpenPage(QStringLiteral("voice"));

  auto* output =
      view.findChild<QComboBox*>(QStringLiteral("audioOutputDevice"));
  auto* input = view.findChild<QComboBox*>(QStringLiteral("audioInputDevice"));
  QVERIFY(output != nullptr);
  QVERIFY(input != nullptr);
  QCOMPARE(output->currentIndex(), 0);
  QVERIFY(output->currentData().toString().isEmpty());
  QCOMPARE(input->currentIndex(), 0);
  QVERIFY(input->currentData().toString().isEmpty());

  if (output->count() > 1) {
    output->setCurrentIndex(1);
    const QString id = output->itemData(1).toString();
    QCOMPARE(changed_key, QStringLiteral("audio.output_device"));
    QCOMPARE(changed_value, id);
    QCOMPARE(hermes::ui::NativePref(QStringLiteral("audio.output_device")), id);
  }
  if (input->count() > 1) {
    input->setCurrentIndex(1);
    const QString id = input->itemData(1).toString();
    QCOMPARE(changed_key, QStringLiteral("audio.input_device"));
    QCOMPARE(changed_value, id);
    QCOMPARE(hermes::ui::NativePref(QStringLiteral("audio.input_device")), id);
  }

  hermes::ui::SetNativePref(QStringLiteral("audio.output_device"), QString());
  hermes::ui::SetNativePref(QStringLiteral("audio.input_device"), QString());
}

void SettingsViewTest::fontScaleScalesThemeFonts() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  QCOMPARE(theme.UiFont(13, QFont::Normal).pixelSize(), 13);
  theme.font_scale = 1.30;
  QCOMPARE(theme.UiFont(13, QFont::Normal).pixelSize(), 17);
  QCOMPARE(theme.MonoFont(10, QFont::Normal).pixelSize(), 13);
}

void SettingsViewTest::rendersAgentRosterFromSubagentEvents() {
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  snapshot.label = QStringLiteral("default");
  hermes::ui::SessionSummary session;
  session.id = QStringLiteral("s1");
  session.title = QStringLiteral("Pricing research");
  snapshot.sessions.push_back(session);

  using hermes::ui::ApplySubagentEvent;
  ApplySubagentEvent(
      &snapshot.subagents, QStringLiteral("subagent.start"),
      QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("goal"), QStringLiteral("Research pricing")},
                  {QStringLiteral("model"), QStringLiteral("haiku")},
                  {QStringLiteral("task_count"), 2},
                  {QStringLiteral("task_index"), 1}},
      1000);
  ApplySubagentEvent(
      &snapshot.subagents, QStringLiteral("subagent.progress"),
      QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("subagent_id"), QStringLiteral("a")},
                  {QStringLiteral("tool_name"), QStringLiteral("web_search")},
                  {QStringLiteral("tool_preview"),
                   QStringLiteral("competitor pricing")},
                  {QStringLiteral("tool_count"), 2},
                  {QStringLiteral("output_tokens"), 1200},
                  {QStringLiteral("files_written"),
                   QJsonArray{QStringLiteral("artifacts/pricing.md")}}},
      2000);
  ApplySubagentEvent(
      &snapshot.subagents, QStringLiteral("subagent.start"),
      QStringLiteral("s1"),
      QJsonObject{{QStringLiteral("parent_id"), QStringLiteral("a")},
                  {QStringLiteral("subagent_id"), QStringLiteral("a1")},
                  {QStringLiteral("goal"), QStringLiteral("Check citations")}},
      3000);
  ApplySubagentEvent(
      &snapshot.subagents, QStringLiteral("subagent.complete"),
      QStringLiteral("s1"),
      QJsonObject{
          {QStringLiteral("subagent_id"), QStringLiteral("a1")},
          {QStringLiteral("status"), QStringLiteral("completed")},
          {QStringLiteral("summary"), QStringLiteral("All citations verified")},
          {QStringLiteral("duration_seconds"), 12.0}},
      4000);

  QString steered_session;
  QString steered_agent;
  QString steered_text;
  QString interrupted_session;
  QString interrupted_agent;
  hermes::ui::PageContext context;
  context.theme = hermes::ui::Theme::Light();
  context.snapshot = &snapshot;
  context.suppress_page_heading = true;
  context.steer_subagent = [&steered_session, &steered_agent, &steered_text](
                               const QString& session_id,
                               const QString& subagent_id, const QString& text,
                               hermes::ui::PageJsonCallback done) {
    steered_session = session_id;
    steered_agent = subagent_id;
    steered_text = text;
    done(QJsonObject{{QStringLiteral("status"), QStringLiteral("queued")},
                     {QStringLiteral("subagent_id"), subagent_id}},
         QString());
  };
  context.interrupt_subagent = [&interrupted_session, &interrupted_agent](
                                   const QString& session_id,
                                   const QString& subagent_id,
                                   hermes::ui::PageJsonCallback done) {
    interrupted_session = session_id;
    interrupted_agent = subagent_id;
    done(QJsonObject{{QStringLiteral("found"), true},
                     {QStringLiteral("subagent_id"), subagent_id}},
         QString());
  };
  QWidget parent;
  QWidget* page =
      hermes::ui::BuildDataPage(QStringLiteral("agents"), context, &parent);
  QVERIFY(page != nullptr);
  page->show();

  auto* title = page->findChild<QLabel*>(QStringLiteral("agentTitle_a"));
  auto* parent_status =
      page->findChild<QLabel*>(QStringLiteral("agentStatus_a"));
  auto* child_status =
      page->findChild<QLabel*>(QStringLiteral("agentStatus_a1"));
  auto* meta = page->findChild<QLabel*>(QStringLiteral("agentMeta_a"));
  auto* tool = page->findChild<QLabel*>(QStringLiteral("agentStream_a_0"));
  auto* summary = page->findChild<QLabel*>(QStringLiteral("agentStream_a1_0"));
  auto* files = page->findChild<QLabel*>(QStringLiteral("agentFiles_a"));
  QVERIFY(title != nullptr);
  QVERIFY(parent_status != nullptr);
  QVERIFY(child_status != nullptr);
  QVERIFY(meta != nullptr);
  QVERIFY(tool != nullptr);
  QVERIFY(summary != nullptr);
  QVERIFY(files != nullptr);
  QCOMPARE(title->text(), QStringLiteral("Research pricing"));
  QCOMPARE(parent_status->text(), QStringLiteral("Running"));
  QCOMPARE(child_status->text(), QStringLiteral("Done"));
  QVERIFY(meta->text().contains(QStringLiteral("Task 2/2")));
  QVERIFY(meta->text().contains(QStringLiteral("haiku")));
  QVERIFY(meta->text().contains(QStringLiteral("Web Search")));
  QVERIFY(meta->text().contains(QStringLiteral("1.2k")));
  QVERIFY(meta->text().contains(QStringLiteral("2 tools")));
  QCOMPARE(tool->text(),
           QStringLiteral("▸ Web Search(\"competitor pricing\")"));
  QCOMPARE(summary->text(), QStringLiteral("✔ All citations verified"));
  QVERIFY(files->text().contains(QStringLiteral("wrote artifacts/pricing.md")));

  // Live children accept steering and a hard stop; finished children do not.
  auto* steer_input =
      page->findChild<QLineEdit*>(QStringLiteral("agentSteerInput_a"));
  auto* steer = page->findChild<QPushButton*>(QStringLiteral("agentSteer_a"));
  auto* stop = page->findChild<QPushButton*>(QStringLiteral("agentStop_a"));
  QVERIFY(steer_input != nullptr);
  QVERIFY(steer != nullptr);
  QVERIFY(stop != nullptr);
  QVERIFY(!steer->isEnabled());
  QVERIFY(page->findChild<QPushButton*>(QStringLiteral("agentStop_a1")) ==
          nullptr);
  steer_input->setText(QStringLiteral("focus on EU pricing"));
  QVERIFY(steer->isEnabled());
  steer->click();
  QCOMPARE(steered_session, QStringLiteral("s1"));
  QCOMPARE(steered_agent, QStringLiteral("a"));
  QCOMPARE(steered_text, QStringLiteral("focus on EU pricing"));
  QVERIFY(steer_input->text().isEmpty());
  auto* control_status =
      page->findChild<QLabel*>(QStringLiteral("agentControlStatus_a"));
  QVERIFY(control_status != nullptr);
  QCOMPARE(control_status->text(),
           QStringLiteral("Steering queued for this agent."));
  stop->click();
  QCOMPARE(interrupted_session, QStringLiteral("s1"));
  QCOMPARE(interrupted_agent, QStringLiteral("a"));
  QCOMPARE(control_status->text(), QStringLiteral("Stop requested."));

  // The roster says how many delegated children are live, not total rows.
  bool found_live = false;
  for (QLabel* label : page->findChildren<QLabel*>()) {
    if (label->text() == QStringLiteral("2 agents · 1 live")) {
      found_live = true;
    }
  }
  QVERIFY(found_live);
  delete page;
}

void SettingsViewTest::delegationEditsModelFallbacksAndProfileRoutes() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QString page;
  QString method;
  QString path;
  QJsonObject body;
  int actions = 0;
  view.on_page_action = [&](const QString& page_id, const QString& verb,
                            const QString& route, const QJsonObject& payload) {
    page = page_id;
    method = verb;
    path = route;
    body = payload;
    ++actions;
  };
  view.show();
  hermes::ui::ProfileSnapshot snapshot;
  snapshot.name = QStringLiteral("default");
  hermes::ui::PageState delegation;
  delegation.loaded = true;
  delegation.data = QJsonObject{
      {QStringLiteral("routing"),
       QJsonObject{
           {QStringLiteral("configured"), QStringLiteral("default")},
           {QStringLiteral("backends"),
            QJsonArray{QJsonObject{
                {QStringLiteral("name"), QStringLiteral("default")},
                {QStringLiteral("label"), QStringLiteral("Default")},
                {QStringLiteral("available"), true}}}},
           {QStringLiteral("model_map"),
            QJsonObject{
                {QStringLiteral("default"), QStringLiteral("planner")},
                {QStringLiteral("options"),
                 QJsonArray{QJsonObject{
                     {QStringLiteral("name"), QStringLiteral("planner")},
                     {QStringLiteral("description"), QStringLiteral("Plans")},
                     {QStringLiteral("provider"), QStringLiteral("openai")},
                     {QStringLiteral("model"), QStringLiteral("gpt-5")},
                     {QStringLiteral("api_key"), QStringLiteral("sk-…1234")},
                     {QStringLiteral("fallback_providers"),
                      QJsonArray{QJsonObject{
                          {QStringLiteral("provider"),
                           QStringLiteral("backup")},
                          {QStringLiteral("model"), QStringLiteral("model-a")},
                          {QStringLiteral("base_url"),
                           QStringLiteral("http://local:8080")}}}}}}}}},
           {QStringLiteral("profile_routes"),
            QJsonObject{{QStringLiteral("default"), QStringLiteral("ceo")},
                        {QStringLiteral("profiles"),
                         QJsonArray{QJsonObject{
                             {QStringLiteral("name"), QStringLiteral("ceo")},
                             {QStringLiteral("description"),
                              QStringLiteral("Board work")}}}}}},
           {QStringLiteral("profiles"),
            QJsonArray{QStringLiteral("ceo"), QStringLiteral("trader")}}}},
  };
  snapshot.pages.insert(QStringLiteral("delegation"), delegation);
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("delegation")) {
      nav->setCurrentRow(i);
      break;
    }
  }

  auto* edit = view.findChild<QPushButton*>(
      QStringLiteral("delegationModelEdit_planner"));
  QVERIFY(edit != nullptr);
  edit->click();
  auto* dialog =
      view.findChild<QDialog*>(QStringLiteral("delegationModelDialog"));
  QVERIFY(dialog != nullptr);
  auto* description = dialog->findChild<QLineEdit*>(
      QStringLiteral("delegationRouteDescription"));
  auto* fallbacks = dialog->findChild<QPlainTextEdit*>(
      QStringLiteral("delegationRouteFallbacks"));
  QVERIFY(description != nullptr);
  QVERIFY(fallbacks != nullptr);
  QCOMPARE(fallbacks->toPlainText(), QStringLiteral("backup/model-a"));
  description->setText(QStringLiteral("Planning route"));
  fallbacks->setPlainText(QStringLiteral("backup/model-a\nfallback/model-b"));
  auto* save =
      dialog->findChild<QPushButton*>(QStringLiteral("delegationRouteSave"));
  QVERIFY(save != nullptr);
  save->click();
  QCOMPARE(actions, 1);
  QCOMPARE(page, QStringLiteral("delegation"));
  QCOMPARE(method, QStringLiteral("PUT"));
  QCOMPARE(path, QStringLiteral(
                     "/api/delegation/backends/model_map?profile=default"));
  QCOMPARE(body.value(QStringLiteral("default")).toString(),
           QStringLiteral("planner"));
  const QJsonArray options = body.value(QStringLiteral("options")).toArray();
  QCOMPARE(options.size(), 1);
  const QJsonObject route = options[0].toObject();
  QCOMPARE(route.value(QStringLiteral("description")).toString(),
           QStringLiteral("Planning route"));
  QCOMPARE(route.value(QStringLiteral("api_key")).toString(),
           QStringLiteral("sk-…1234"));
  const QJsonArray chain =
      route.value(QStringLiteral("fallback_providers")).toArray();
  QCOMPARE(chain.size(), 2);
  QCOMPARE(chain[0].toObject().value(QStringLiteral("base_url")).toString(),
           QStringLiteral("http://local:8080"));
  QCOMPARE(chain[1].toObject().value(QStringLiteral("provider")).toString(),
           QStringLiteral("fallback"));
  QCOMPARE(chain[1].toObject().value(QStringLiteral("model")).toString(),
           QStringLiteral("model-b"));

  auto* edit_profile =
      view.findChild<QPushButton*>(QStringLiteral("delegationRouteEdit_ceo"));
  QVERIFY(edit_profile != nullptr);
  edit_profile->click();
  auto* profile_dialog =
      view.findChild<QDialog*>(QStringLiteral("delegationProfileDialog"));
  QVERIFY(profile_dialog != nullptr);
  auto* profile_description = profile_dialog->findChild<QLineEdit*>(
      QStringLiteral("delegationProfileDescription"));
  QVERIFY(profile_description != nullptr);
  profile_description->setText(QStringLiteral("Executive work"));
  auto* profile_save = profile_dialog->findChild<QPushButton*>(
      QStringLiteral("delegationProfileSave"));
  QVERIFY(profile_save != nullptr);
  profile_save->click();
  QCOMPARE(actions, 2);
  QCOMPARE(path,
           QStringLiteral(
               "/api/delegation/backends/profile_routes?profile=default"));
  QCOMPARE(body.value(QStringLiteral("default")).toString(),
           QStringLiteral("ceo"));
  QCOMPARE(body.value(QStringLiteral("profiles"))
               .toArray()[0]
               .toObject()
               .value(QStringLiteral("description"))
               .toString(),
           QStringLiteral("Executive work"));

  auto* default_apply = view.findChild<QPushButton*>(
      QStringLiteral("delegationRouteDefaultApply"));
  QVERIFY(default_apply != nullptr);
  default_apply->click();
  QCOMPARE(actions, 3);
  QCOMPARE(body.value(QStringLiteral("default")).toString(),
           QStringLiteral("ceo"));
}

void SettingsViewTest::modelSectionEditsProviderModelAndReasoning() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QJsonObject saved;
  view.on_save = [&saved](const QJsonObject& patch) { saved = patch; };
  view.show();
  auto snapshot = Snapshot();
  // `/api/config` normalizes a dict-form model section to the legacy bare
  // string; the editor must recover the provider from the catalog.
  snapshot.provider.clear();
  snapshot.model = QStringLiteral("m1");
  snapshot.config.insert(QStringLiteral("model"), QStringLiteral("m1"));
  snapshot.config.insert(QStringLiteral("agent"),
                         QJsonObject{{QStringLiteral("reasoning_effort"),
                                      QStringLiteral("medium")}});
  hermes::ui::ModelProvider first;
  first.name = QStringLiteral("Provider One");
  first.slug = QStringLiteral("p1");
  first.models = {QStringLiteral("m1"), QStringLiteral("m2")};
  hermes::ui::ModelProvider second;
  second.name = QStringLiteral("Provider Two");
  second.slug = QStringLiteral("p2");
  second.models = {QStringLiteral("m3"), QStringLiteral("m4")};
  snapshot.model_providers = {first, second};
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("model")) {
      nav->setCurrentRow(i);
      break;
    }
  }
  auto* provider =
      view.findChild<QComboBox*>(QStringLiteral("setting_model_provider"));
  auto* model =
      view.findChild<QComboBox*>(QStringLiteral("setting_model_default"));
  auto* effort = view.findChild<QComboBox*>(
      QStringLiteral("setting_agent_reasoning_effort"));
  QVERIFY(provider != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(effort != nullptr);
  QCOMPARE(provider->currentText(), QStringLiteral("p1"));
  QCOMPARE(model->currentText(), QStringLiteral("m1"));
  QCOMPARE(effort->currentData().toString(), QStringLiteral("medium"));

  provider->setCurrentIndex(provider->findData(QStringLiteral("p2")));
  QTest::qWait(800);
  QCOMPARE(saved.value(QStringLiteral("model"))
               .toObject()
               .value(QStringLiteral("provider"))
               .toString(),
           QStringLiteral("p2"));
  QCOMPARE(model->currentText(), QStringLiteral("m3"));
  model->setCurrentText(QStringLiteral("m4"));
  QTest::qWait(800);
  QCOMPARE(saved.value(QStringLiteral("model"))
               .toObject()
               .value(QStringLiteral("default"))
               .toString(),
           QStringLiteral("m4"));
  effort->setCurrentIndex(effort->findData(QStringLiteral("high")));
  QTest::qWait(800);
  QCOMPARE(saved.value(QStringLiteral("agent"))
               .toObject()
               .value(QStringLiteral("reasoning_effort"))
               .toString(),
           QStringLiteral("high"));
}

void SettingsViewTest::modelAuxiliaryAndMoaPagesRender() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  view.show();
  auto snapshot = Snapshot();
  snapshot.name = QStringLiteral("ceo");
  hermes::ui::ModelProvider first;
  first.name = QStringLiteral("Provider One");
  first.slug = QStringLiteral("p1");
  first.models = {QStringLiteral("m1"), QStringLiteral("m2")};
  hermes::ui::ModelProvider second;
  second.name = QStringLiteral("Provider Two");
  second.slug = QStringLiteral("p2");
  second.models = {QStringLiteral("m3")};
  snapshot.model_providers = {first, second};
  hermes::ui::PageState auxiliary;
  auxiliary.loaded = true;
  auxiliary.data.insert(
      QStringLiteral("auxiliary"),
      QJsonObject{
          {QStringLiteral("main"),
           QJsonObject{{QStringLiteral("provider"), QStringLiteral("p1")},
                       {QStringLiteral("model"), QStringLiteral("m1")}}},
          {QStringLiteral("tasks"),
           QJsonArray{QJsonObject{
               {QStringLiteral("task"), QStringLiteral("title_generation")},
               {QStringLiteral("provider"), QStringLiteral("p1")},
               {QStringLiteral("model"), QStringLiteral("m1")}}}}});
  snapshot.pages.insert(QStringLiteral("modelaux"), auxiliary);
  hermes::ui::PageState moa;
  moa.loaded = true;
  moa.data.insert(
      QStringLiteral("moa"),
      QJsonObject{
          {QStringLiteral("default_preset"), QStringLiteral("default")},
          {QStringLiteral("active_preset"), QStringLiteral("coding")},
          {QStringLiteral("presets"),
           QJsonObject{
               {QStringLiteral("coding"),
                QJsonObject{
                    {QStringLiteral("reference_models"),
                     QJsonArray{QJsonObject{
                         {QStringLiteral("provider"), QStringLiteral("p1")},
                         {QStringLiteral("model"), QStringLiteral("m1")}}}},
                    {QStringLiteral("aggregator"),
                     QJsonObject{
                         {QStringLiteral("provider"), QStringLiteral("p2")},
                         {QStringLiteral("model"),
                          QStringLiteral("m3")}}}}}}}});
  snapshot.pages.insert(QStringLiteral("modelmoa"), moa);
  view.SetSnapshot(snapshot);

  QString action_page;
  QString action_method;
  QString action_path;
  QJsonObject action_body;
  view.on_page_action = [&](const QString& page, const QString& method,
                            const QString& path, const QJsonObject& body) {
    action_page = page;
    action_method = method;
    action_path = path;
    action_body = body;
  };

  view.OpenPage(QStringLiteral("model"));
  auto* auxiliary_tab = view.findChild<QPushButton*>(
      QStringLiteral("settingsSubpage_model_auxiliary"));
  QVERIFY(auxiliary_tab != nullptr);
  auxiliary_tab->click();
  auto* card =
      view.findChild<QWidget*>(QStringLiteral("auxiliaryRow_title_generation"));
  QVERIFY(card != nullptr);
  auto* provider = view.findChild<QComboBox*>(
      QStringLiteral("auxiliaryProvider_title_generation"));
  auto* model = view.findChild<QComboBox*>(
      QStringLiteral("auxiliaryModel_title_generation"));
  auto* save = view.findChild<QPushButton*>(
      QStringLiteral("auxiliarySave_title_generation"));
  QVERIFY(provider != nullptr);
  QVERIFY(model != nullptr);
  QVERIFY(save != nullptr);
  provider->setCurrentIndex(provider->findData(QStringLiteral("p2")));
  model->setCurrentIndex(model->findData(QStringLiteral("m3")));
  save->click();
  QCOMPARE(action_page, QStringLiteral("modelaux"));
  QCOMPARE(action_method, QStringLiteral("POST"));
  QCOMPARE(action_path, QStringLiteral("/api/model/set?profile=ceo"));
  QCOMPARE(action_body.value(QStringLiteral("scope")).toString(),
           QStringLiteral("auxiliary"));
  QCOMPARE(action_body.value(QStringLiteral("task")).toString(),
           QStringLiteral("title_generation"));
  QCOMPARE(action_body.value(QStringLiteral("provider")).toString(),
           QStringLiteral("p2"));
  QCOMPARE(action_body.value(QStringLiteral("model")).toString(),
           QStringLiteral("m3"));

  auto* moa_tab =
      view.findChild<QPushButton*>(QStringLiteral("settingsSubpage_model_moa"));
  QVERIFY(moa_tab != nullptr);
  moa_tab->click();
  QVERIFY(view.findChild<QWidget*>(QStringLiteral("moaPreset_coding")) !=
          nullptr);
}

void SettingsViewTest::modelSectionEditsOrderedFallbackChain() {
  const QJsonArray previous{
      QJsonObject{{QStringLiteral("provider"), QStringLiteral("openrouter")},
                  {QStringLiteral("model"), QStringLiteral("glm-4.6")},
                  {QStringLiteral("base_url"),
                   QStringLiteral("http://gateway.local:8080")}},
      QJsonObject{{QStringLiteral("provider"), QStringLiteral("local")},
                  {QStringLiteral("model"), QStringLiteral("llama-3")}},
  };
  QCOMPARE(hermes::ui::FormatFallbackChain(previous),
           QStringLiteral("openrouter/glm-4.6, local/llama-3"));
  const QJsonArray parsed = hermes::ui::ParseFallbackChain(
      QStringLiteral("local/llama-3, anthropic/claude-sonnet"), previous);
  QCOMPARE(parsed.size(), 2);
  QCOMPARE(parsed.at(1).toObject().value(QStringLiteral("provider")).toString(),
           QStringLiteral("anthropic"));

  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  QJsonObject saved;
  view.on_save = [&saved](const QJsonObject& patch) { saved = patch; };
  view.show();
  auto snapshot = Snapshot();
  snapshot.config.insert(QStringLiteral("fallback_providers"), previous);
  view.SetSnapshot(snapshot);

  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  QVERIFY(nav != nullptr);
  int model_row = -1;
  for (int i = 0; i < nav->count(); ++i) {
    if (nav->item(i)->data(Qt::UserRole).toString() ==
        QStringLiteral("model")) {
      model_row = i;
      break;
    }
  }
  QVERIFY(model_row >= 0);
  nav->setCurrentRow(model_row);

  auto* fallback_chip = view.findChild<QPushButton*>(
      QStringLiteral("settingsSubpage_model_fallbacks"));
  QVERIFY(fallback_chip != nullptr);
  fallback_chip->click();

  auto* edit =
      view.findChild<QLineEdit*>(QStringLiteral("setting_fallback_providers"));
  QVERIFY(edit != nullptr);
  QCOMPARE(edit->text(), QStringLiteral("openrouter/glm-4.6, local/llama-3"));
  edit->setText(QStringLiteral("anthropic/claude-sonnet, openrouter/glm-4.6"));
  QTest::keyClick(edit, Qt::Key_Return);
  QTest::qWait(800);

  const QJsonArray saved_chain =
      saved.value(QStringLiteral("fallback_providers")).toArray();
  QCOMPARE(saved_chain.size(), 2);
  QCOMPARE(
      saved_chain.at(0).toObject().value(QStringLiteral("provider")).toString(),
      QStringLiteral("anthropic"));
  QCOMPARE(
      saved_chain.at(1).toObject().value(QStringLiteral("base_url")).toString(),
      QStringLiteral("http://gateway.local:8080"));
}

void SettingsViewTest::settingsSubpagesDeepLinkAndSearchFields() {
  hermes::ui::Theme theme = hermes::ui::Theme::Light();
  hermes::ui::SettingsView view(theme);
  auto snapshot = Snapshot();
  snapshot.config.insert(
      QStringLiteral("fallback_providers"),
      QJsonArray{QJsonObject{
          {QStringLiteral("provider"), QStringLiteral("openrouter")},
          {QStringLiteral("model"), QStringLiteral("glm-4.6")}}});
  snapshot.config.insert(
      QStringLiteral("tts"),
      QJsonObject{{QStringLiteral("provider"), QStringLiteral("edge")},
                  {QStringLiteral("edge"),
                   QJsonObject{{QStringLiteral("voice"),
                                QStringLiteral("en-US-AriaNeural")}}}});
  view.SetSnapshot(snapshot);

  view.OpenPage(QStringLiteral("voice#tts.provider"));
  auto* speech = view.findChild<QPushButton*>(
      QStringLiteral("settingsSubpage_voice_speech"));
  QVERIFY(speech != nullptr);
  QVERIFY(speech->isChecked());
  QVERIFY(view.findChild<QWidget*>(QStringLiteral("setting_tts_provider")) !=
          nullptr);

  view.OpenPage(QStringLiteral("model?field=fallback_providers"));
  auto* fallbacks = view.findChild<QPushButton*>(
      QStringLiteral("settingsSubpage_model_fallbacks"));
  QVERIFY(fallbacks != nullptr);
  QVERIFY(fallbacks->isChecked());
  QVERIFY(view.findChild<QLineEdit*>(
              QStringLiteral("setting_fallback_providers")) != nullptr);

  auto nav_row = [&view](const QString& id) {
    auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
    if (nav == nullptr) {
      return -1;
    }
    for (int i = 0; i < nav->count(); ++i) {
      if (nav->item(i)->data(Qt::UserRole).toString() == id) {
        return i;
      }
    }
    return -1;
  };
  auto* nav = view.findChild<QListWidget*>(QStringLiteral("settingsNav"));
  auto* search = view.findChild<QLineEdit*>(QStringLiteral("settingsSearch"));
  QVERIFY(nav != nullptr);
  QVERIFY(search != nullptr);

  search->setText(QStringLiteral("tts.provider"));
  QVERIFY(nav->item(nav_row(QStringLiteral("model")))->isHidden());
  QVERIFY(!nav->item(nav_row(QStringLiteral("voice")))->isHidden());
  nav->setCurrentRow(nav_row(QStringLiteral("voice")));
  QVERIFY(view.findChild<QWidget*>(QStringLiteral("setting_tts_provider")) !=
          nullptr);
  QVERIFY(view.findChild<QWidget*>(
              QStringLiteral("setting_voice_voice_chat_mode")) == nullptr);

  search->setText(QStringLiteral("fallback_providers"));
  QVERIFY(nav->item(nav_row(QStringLiteral("voice")))->isHidden());
  QVERIFY(!nav->item(nav_row(QStringLiteral("model")))->isHidden());
  nav->setCurrentRow(nav_row(QStringLiteral("model")));
  QVERIFY(view.findChild<QLineEdit*>(
              QStringLiteral("setting_fallback_providers")) != nullptr);
  QVERIFY(view.findChild<QWidget*>(
              QStringLiteral("setting_model_context_length")) == nullptr);

  search->clear();
  QVERIFY(view.findChild<QLineEdit*>(
              QStringLiteral("setting_fallback_providers")) != nullptr);

  // Settings search completes page names from the current navigation catalog.
  const QString page_name = nav->item(0)->text();
  QTest::keyClicks(search, page_name);
  QTRY_VERIFY(search->completer() != nullptr);
  QTRY_VERIFY(search->completer()->model()->rowCount() > 0);
  bool has_page = false;
  for (int row = 0; row < search->completer()->model()->rowCount(); ++row) {
    if (search->completer()->model()->index(row, 0).data().toString() ==
        page_name) {
      has_page = true;
      break;
    }
  }
  QVERIFY(has_page);
}

QTEST_MAIN(SettingsViewTest)
#include "settings_view_test.moc"
