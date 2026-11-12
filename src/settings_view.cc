// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/settings_view.h"

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#ifdef HERMES_HAS_MULTIMEDIA
#include <QAudioDevice>
#include <QMediaDevices>
#endif
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>
#include <vector>

#include "src/fallback_models.h"
#include "src/native_prefs.h"
#include "src/search_completion.h"
#include "src/settings_pages.h"

namespace hermes::ui {
namespace {

// The desktop stores sections in this order. Kept in sync with
// src/settings_sections.inc, which is generated from apps/desktop's
// SECTIONS table so the key list is not hand-maintained.
constexpr int kNavWidth = 218;
constexpr int kSaveDebounceMs = 600;

QString AsString(const QJsonValue& value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isBool()) {
    return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
  }
  if (value.isDouble()) {
    return QString::number(value.toDouble(), 'g', 12);
  }
  return QString();
}

QJsonValue ValueAtPath(const QJsonObject& root, const QString& key) {
  QJsonValue current = root;
  const QStringList parts = key.split(QLatin1Char('.'));
  for (const QString& part : parts) {
    if (!current.isObject()) {
      return QJsonValue();
    }
    const QJsonObject object = current.toObject();
    if (!object.contains(part)) {
      return QJsonValue();
    }
    current = object.value(part);
  }
  return current;
}

// QJsonObject is value-typed; walk a copy and write it back at each level.
void MergePath(QJsonObject* root, const QStringList& parts, int index,
               const QJsonValue& value) {
  if (index < 0 || index >= parts.size()) {
    return;
  }
  if (index == parts.size() - 1) {
    root->insert(parts.at(index), value);
    return;
  }
  QJsonObject child = root->value(parts.at(index)).toObject();
  MergePath(&child, parts, index + 1, value);
  root->insert(parts.at(index), child);
}

// The desktop keeps its enum suggestions in the renderer rather than the
// backend schema. Keep the same list here so a config key that exists in
// config.yaml but not yet in the generated schema still gets a picker instead
// of a raw text box.
const QHash<QString, QStringList>& EnumOptions() {
  static const QHash<QString, QStringList> options = {
      {QStringLiteral("agent.image_input_mode"),
       {QStringLiteral("auto"), QStringLiteral("native"),
        QStringLiteral("text")}},
      {QStringLiteral("approvals.mode"),
       {QStringLiteral("manual"), QStringLiteral("smart"),
        QStringLiteral("off")}},
      {QStringLiteral("code_execution.mode"),
       {QStringLiteral("project"), QStringLiteral("strict")}},
      {QStringLiteral("context.engine"),
       {QStringLiteral("compressor"), QStringLiteral("default"),
        QStringLiteral("custom")}},
      {QStringLiteral("terminal.backend"),
       {QStringLiteral("local"), QStringLiteral("docker"),
        QStringLiteral("singularity"), QStringLiteral("modal"),
        QStringLiteral("daytona"), QStringLiteral("ssh")}},
      {QStringLiteral("voice.voice_chat_mode"),
       {QStringLiteral("chained"), QStringLiteral("gpt-live")}},
      {QStringLiteral("voice.gpt_live.voice"),
       {QStringLiteral("marin"), QStringLiteral("cedar"),
        QStringLiteral("quartz"), QStringLiteral("ripple"),
        QStringLiteral("vesper"), QStringLiteral("willow"),
        QStringLiteral("stone"), QStringLiteral("gleam"),
        QStringLiteral("meridian"), QStringLiteral("bossa"),
        QStringLiteral("tempo"), QStringLiteral("beacon"),
        QStringLiteral("delta"), QStringLiteral("cinder")}},
      {QStringLiteral("tts.provider"),
       {QStringLiteral("edge"), QStringLiteral("elevenlabs"),
        QStringLiteral("openai"), QStringLiteral("xai"),
        QStringLiteral("minimax"), QStringLiteral("mistral"),
        QStringLiteral("gemini"), QStringLiteral("neutts"),
        QStringLiteral("kittentts"), QStringLiteral("piper")}},
      {QStringLiteral("stt.provider"),
       {QStringLiteral("local"), QStringLiteral("groq"),
        QStringLiteral("openai"), QStringLiteral("mistral"),
        QStringLiteral("xai"), QStringLiteral("elevenlabs"),
        QStringLiteral("deepinfra")}},
      {QStringLiteral("stt.local.model"),
       {QStringLiteral("tiny"), QStringLiteral("base"), QStringLiteral("small"),
        QStringLiteral("medium"), QStringLiteral("large-v3"),
        QStringLiteral("turbo")}},
      {QStringLiteral("stt.elevenlabs.model_id"),
       {QStringLiteral("scribe_v2"), QStringLiteral("scribe_v1")}},
      {QStringLiteral("tts.openai.model"),
       {QStringLiteral("gpt-4o-mini-tts"), QStringLiteral("tts-1"),
        QStringLiteral("tts-1-hd")}},
      {QStringLiteral("tts.openai.voice"),
       {QStringLiteral("alloy"), QStringLiteral("ash"),
        QStringLiteral("ballad"), QStringLiteral("cedar"),
        QStringLiteral("coral"), QStringLiteral("echo"),
        QStringLiteral("fable"), QStringLiteral("marin"),
        QStringLiteral("nova"), QStringLiteral("onyx"), QStringLiteral("sage"),
        QStringLiteral("shimmer"), QStringLiteral("verse")}},
      {QStringLiteral("tts.edge.voice"),
       {QStringLiteral("en-US-AriaNeural"), QStringLiteral("en-US-JennyNeural"),
        QStringLiteral("en-US-AndrewNeural"),
        QStringLiteral("en-US-BrianNeural"), QStringLiteral("en-US-GuyNeural"),
        QStringLiteral("en-GB-SoniaNeural")}},
      {QStringLiteral("tts.gemini.model"),
       {QStringLiteral("gemini-2.5-flash-preview-tts"),
        QStringLiteral("gemini-2.5-pro-preview-tts")}},
      {QStringLiteral("tts.gemini.voice"),
       {QStringLiteral("Zephyr"),       QStringLiteral("Puck"),
        QStringLiteral("Charon"),       QStringLiteral("Kore"),
        QStringLiteral("Fenrir"),       QStringLiteral("Leda"),
        QStringLiteral("Orus"),         QStringLiteral("Aoede"),
        QStringLiteral("Callirrhoe"),   QStringLiteral("Autonoe"),
        QStringLiteral("Enceladus"),    QStringLiteral("Iapetus"),
        QStringLiteral("Umbriel"),      QStringLiteral("Algieba"),
        QStringLiteral("Despina"),      QStringLiteral("Erinome"),
        QStringLiteral("Algenib"),      QStringLiteral("Rasalgethi"),
        QStringLiteral("Laomedeia"),    QStringLiteral("Achernar"),
        QStringLiteral("Alnilam"),      QStringLiteral("Schedar"),
        QStringLiteral("Gacrux"),       QStringLiteral("Pulcherrima"),
        QStringLiteral("Achird"),       QStringLiteral("Zubenelgenubi"),
        QStringLiteral("Vindemiatrix"), QStringLiteral("Sadachbia"),
        QStringLiteral("Sadaltager"),   QStringLiteral("Sulafat")}},
      {QStringLiteral("tts.xai.voice_id"), {QStringLiteral("eve")}},
      {QStringLiteral("tts.minimax.model"),
       {QStringLiteral("speech-02-hd"), QStringLiteral("speech-02-turbo")}},
      {QStringLiteral("tts.mistral.model"),
       {QStringLiteral("voxtral-mini-tts-2603")}},
      {QStringLiteral("tts.kittentts.model"),
       {QStringLiteral("KittenML/kitten-tts-nano-0.8-int8"),
        QStringLiteral("KittenML/kitten-tts-micro-0.8-int8"),
        QStringLiteral("KittenML/kitten-tts-mini-0.8-int8")}},
      {QStringLiteral("tts.kittentts.voice"), {QStringLiteral("Jasper")}},
      {QStringLiteral("tts.piper.voice"),
       {QStringLiteral("en_US-lessac-medium"),
        QStringLiteral("en_US-amy-medium"), QStringLiteral("en_US-ryan-high"),
        QStringLiteral("en_GB-alan-medium")}},
      {QStringLiteral("tts.neutts.model"),
       {QStringLiteral("neuphonic/neutts-air-q4-gguf"),
        QStringLiteral("neuphonic/neutts-air-q8-gguf"),
        QStringLiteral("neuphonic/neutts-air")}},
      {QStringLiteral("tts.neutts.device"),
       {QStringLiteral("cpu"), QStringLiteral("cuda"), QStringLiteral("mps")}},
      {QStringLiteral("stt.openai.model"),
       {QStringLiteral("whisper-1"), QStringLiteral("gpt-4o-mini-transcribe"),
        QStringLiteral("gpt-4o-transcribe"), QStringLiteral("gpt-transcribe")}},
      {QStringLiteral("stt.openai.streaming_model"),
       {QStringLiteral("gpt-live-transcribe"), QStringLiteral("gpt-transcribe"),
        QStringLiteral("gpt-4o-transcribe"),
        QStringLiteral("gpt-4o-mini-transcribe")}},
      {QStringLiteral("stt.groq.model"),
       {QStringLiteral("whisper-large-v3-turbo"),
        QStringLiteral("whisper-large-v3")}},
      {QStringLiteral("stt.mistral.model"),
       {QStringLiteral("voxtral-mini-latest"),
        QStringLiteral("voxtral-mini-2602")}},
      {QStringLiteral("stt.xai.model"),
       {QStringLiteral("grok-voice-transcribe-2.0"),
        QStringLiteral("grok-voice-transcribe-1.0")}},
      {QStringLiteral("tts.elevenlabs.model_id"),
       {QStringLiteral("eleven_v3"), QStringLiteral("eleven_ttv_v3"),
        QStringLiteral("eleven_multilingual_v2"),
        QStringLiteral("eleven_turbo_v2"), QStringLiteral("eleven_turbo_v2_5"),
        QStringLiteral("eleven_flash_v2"),
        QStringLiteral("eleven_flash_v2_5")}},
      {QStringLiteral("updates.non_interactive_local_changes"),
       {QStringLiteral("stash"), QStringLiteral("discard")}},
  };
  return options;
}

bool IsFreeInputKey(const QString& key) {
  static const QSet<QString> keys = {QStringLiteral("tts.edge.voice"),
                                     QStringLiteral("voice.gpt_live.voice"),
                                     QStringLiteral("tts.openai.model"),
                                     QStringLiteral("tts.openai.voice"),
                                     QStringLiteral("tts.elevenlabs.voice_id"),
                                     QStringLiteral("tts.elevenlabs.model_id"),
                                     QStringLiteral("stt.openai.model"),
                                     QStringLiteral("stt.groq.model"),
                                     QStringLiteral("stt.mistral.model"),
                                     QStringLiteral("stt.xai.model"),
                                     QStringLiteral("stt.deepinfra.model"),
                                     QStringLiteral("tts.gemini.model"),
                                     QStringLiteral("tts.gemini.voice"),
                                     QStringLiteral("tts.xai.voice_id"),
                                     QStringLiteral("tts.minimax.model"),
                                     QStringLiteral("tts.minimax.voice_id"),
                                     QStringLiteral("tts.mistral.model"),
                                     QStringLiteral("tts.mistral.voice_id"),
                                     QStringLiteral("tts.neutts.model"),
                                     QStringLiteral("tts.kittentts.model"),
                                     QStringLiteral("tts.kittentts.voice"),
                                     QStringLiteral("tts.piper.voice"),
                                     QStringLiteral("tts.deepinfra.model"),
                                     QStringLiteral("tts.deepinfra.voice")};
  return keys.contains(key);
}

// Voice renders only fields for the selected TTS/STT provider. An unset
// provider uses the backend defaults (edge/local), matching desktop
// voiceFieldVisible(); STT provider sub-fields disappear while STT is off.
bool VoiceFieldVisible(const QString& key, const QJsonObject& config) {
  const QStringList parts = key.split(QLatin1Char('.'));
  if (parts.size() < 3 || (parts.at(0) != QStringLiteral("tts") &&
                           parts.at(0) != QStringLiteral("stt"))) {
    return true;
  }
  const QString domain = parts.at(0);
  const QString provider = parts.at(1);
  if (domain == QStringLiteral("stt") &&
      !ValueAtPath(config, QStringLiteral("stt.enabled")).toBool()) {
    return false;
  }
  QString selected =
      ValueAtPath(config, domain + QStringLiteral(".provider")).toString();
  if (selected.isEmpty()) {
    selected = domain == QStringLiteral("tts") ? QStringLiteral("edge")
                                               : QStringLiteral("local");
  }
  return provider == selected;
}

struct ConfigSubpageSpec {
  const char* id;
  const char* label;
  QStringList fields;
  QStringList prefixes;
};

// Mirrors apps/desktop/src/app/settings/config-subpages.ts. The field lists are
// config keys rather than display labels so a field always routes to the same
// chip as the desktop, including keys inferred from the live config record.
const QHash<QString, std::vector<ConfigSubpageSpec>>& ConfigSubpages() {
  static const QHash<QString, std::vector<ConfigSubpageSpec>> pages = {
      {QStringLiteral("model"),
       {{"main",
         "Main",
         {QStringLiteral("model"), QStringLiteral("model_context_length"),
          QStringLiteral("agent.reasoning_effort"),
          QStringLiteral("agent.service_tier")},
         {QStringLiteral("model.")}},
        {"fallbacks", "Fallbacks", {QStringLiteral("fallback_providers")}, {}},
        {"auxiliary", "Auxiliary", {}, {QStringLiteral("auxiliary.")}},
        {"delegation", "Delegation", {}, {QStringLiteral("delegation.")}},
        {"moa", "MoA", {}, {QStringLiteral("moa.")}}}},
      {QStringLiteral("chat"),
       {{"behavior",
         "Behavior",
         {QStringLiteral("display.personality"), QStringLiteral("timezone"),
          QStringLiteral("display.show_reasoning")},
         {}},
        {"attachments",
         "Attachments",
         {QStringLiteral("agent.image_input_mode")},
         {}}}},
      {QStringLiteral("workspace"),
       {{"projects",
         "Projects",
         {QStringLiteral("terminal.cwd")},
         {QStringLiteral("desktop.repo_scan_")}},
        {"shell",
         "Shell",
         {QStringLiteral("terminal.persistent_shell"),
          QStringLiteral("terminal.env_passthrough")},
         {}},
        {"files",
         "Files",
         {QStringLiteral("code_execution.mode"),
          QStringLiteral("file_read_max_chars")},
         {}}}},
      {QStringLiteral("safety"),
       {{"approvals",
         "Approvals",
         {QStringLiteral("command_allowlist")},
         {QStringLiteral("approvals.")}},
        {"privacy", "Privacy", {}, {QStringLiteral("security.")}},
        {"checkpoints", "Checkpoints", {}, {QStringLiteral("checkpoints.")}}}},
      {QStringLiteral("browser"),
       {{"profile",
         "Profile",
         {QStringLiteral("browser.use_real_profile")},
         {}},
        {"network",
         "Network",
         {QStringLiteral("browser.allow_private_urls"),
          QStringLiteral("browser.auto_local_for_private_urls")},
         {}}}},
      {QStringLiteral("memory"),
       {{"persistent", "Persistent memory", {}, {QStringLiteral("memory.")}},
        {"context",
         "Context",
         {},
         {QStringLiteral("context."), QStringLiteral("compression."),
          QStringLiteral("auxiliary.compression.")}}}},
      {QStringLiteral("voice"),
       {{"conversation",
         "Conversation",
         {QStringLiteral("voice.voice_chat_mode"),
          QStringLiteral("voice.max_recording_seconds"),
          QStringLiteral("voice.client_direct")},
         {QStringLiteral("voice.gpt_live.")}},
        {"transcription", "Transcription", {}, {QStringLiteral("stt.")}},
        {"speech",
         "Speech",
         {QStringLiteral("voice.auto_tts")},
         {QStringLiteral("tts.")}}}},
      {QStringLiteral("advanced"),
       {{"desktop", "Desktop", {}, {QStringLiteral("updates.")}},
        {"runtime",
         "Runtime",
         {QStringLiteral("agent.max_turns"),
          QStringLiteral("agent.api_max_retries"),
          QStringLiteral("agent.service_tier")},
         {}},
        {"tools",
         "Tools",
         {QStringLiteral("toolsets"),
          QStringLiteral("agent.tool_use_enforcement")},
         {}},
        {"terminal", "Terminal", {}, {QStringLiteral("terminal.")}},
        {"output",
         "Output",
         {},
         {QStringLiteral("tool_output."), QStringLiteral("checkpoints.")}}}},
  };
  return pages;
}

QString ConfigSubpageForField(const QString& section, const QString& key) {
  const auto it = ConfigSubpages().constFind(section);
  if (it == ConfigSubpages().constEnd()) {
    return QString();
  }
  for (const ConfigSubpageSpec& page : it.value()) {
    if (page.fields.contains(key)) {
      return QString::fromLatin1(page.id);
    }
    for (const QString& prefix : page.prefixes) {
      if (key.startsWith(prefix)) {
        return QString::fromLatin1(page.id);
      }
    }
  }
  return QString();
}

}  // namespace

SettingsView::SettingsView(Theme theme, QWidget* parent)
    : QWidget(parent), theme_(std::move(theme)) {
  setObjectName(QStringLiteral("settingsView"));
  setAttribute(Qt::WA_StyledBackground, true);
  BuildUi();
  SetTheme(theme_);
}

std::vector<SettingsView::SectionSpec> SettingsView::Sections() {
  return {
#include "src/settings_sections.inc"
  };
}

void SettingsView::BuildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(24, 20, 24, 20);
  root->setSpacing(14);

  auto* header = new QHBoxLayout();
  title_ = new QLabel(QStringLiteral("Settings"), this);
  title_->setObjectName(QStringLiteral("settingsTitle"));
  header->addWidget(title_);
  header->addSpacing(16);

  search_ = new QLineEdit(this);
  search_->setObjectName(QStringLiteral("settingsSearch"));
  search_->setPlaceholderText(QStringLiteral("Search settings"));
  search_->setClearButtonEnabled(true);
  search_->setFixedWidth(280);
  AttachContextCompleter(search_, [this]() {
    QStringList labels;
    if (nav_ != nullptr) {
      for (int i = 0; i < nav_->count(); ++i) {
        labels.append(nav_->item(i)->text());
      }
    }
    return labels;
  });
  header->addWidget(search_);
  header->addStretch(1);

  status_ = new QLabel(this);
  status_->setObjectName(QStringLiteral("settingsStatus"));
  header->addWidget(status_);

  auto* close = new QPushButton(QStringLiteral("Close"), this);
  close->setObjectName(QStringLiteral("settingsClose"));
  connect(close, &QPushButton::clicked, this, [this]() {
    if (on_close) {
      on_close();
    }
  });
  header->addWidget(close);
  root->addLayout(header);

  auto* body = new QHBoxLayout();
  body->setSpacing(18);

  nav_ = new QListWidget(this);
  nav_->setObjectName(QStringLiteral("settingsNav"));
  nav_->setFixedWidth(kNavWidth);
  nav_->setFrameShape(QFrame::NoFrame);
  nav_->setSelectionMode(QAbstractItemView::SingleSelection);
  body->addWidget(nav_);

  scroll_ = new QScrollArea(this);
  scroll_->setObjectName(QStringLiteral("settingsScroll"));
  scroll_->setWidgetResizable(true);
  scroll_->setFrameShape(QFrame::NoFrame);
  scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  form_ = new QWidget(scroll_);
  form_->setObjectName(QStringLiteral("settingsForm"));
  form_layout_ = new QVBoxLayout(form_);
  form_layout_->setContentsMargins(4, 2, 12, 24);
  form_layout_->setSpacing(14);
  form_layout_->setAlignment(Qt::AlignTop);
  scroll_->setWidget(form_);
  body->addWidget(scroll_, 1);
  root->addLayout(body, 1);

  save_timer_ = new QTimer(this);
  save_timer_->setSingleShot(true);
  save_timer_->setInterval(kSaveDebounceMs);
  connect(save_timer_, &QTimer::timeout, this, &SettingsView::FlushSave);

  connect(nav_, &QListWidget::currentRowChanged, this, [this](int row) {
    if (row < 0) {
      return;
    }
    const QString id = nav_->item(row)->data(Qt::UserRole).toString();
    if (!id.isEmpty()) {
      ShowSection(id, true);
    }
  });
  connect(search_, &QLineEdit::textChanged, this, [this](const QString& text) {
    const QString query = text.trimmed().toLower();
    const std::vector<SectionSpec> sections = Sections();
    for (int i = 0; i < nav_->count(); ++i) {
      QListWidgetItem* item = nav_->item(i);
      const QString id = item->data(Qt::UserRole).toString();
      if (id.isEmpty()) {
        continue;
      }
      bool matches = item->text().toLower().contains(query);
      if (!matches && !query.isEmpty()) {
        for (const SectionSpec& section : sections) {
          if (section.id != id) {
            continue;
          }
          for (const QString& key : section.keys) {
            if (key.toLower().contains(query) ||
                PrettyKey(key).toLower().contains(query)) {
              matches = true;
              break;
            }
          }
          break;
        }
      }
      item->setHidden(!query.isEmpty() && !matches);
    }
    if (IsOpen()) {
      RebuildForm();
    }
  });

  BuildNav();
}

void SettingsView::BuildNav() {
  const QSignalBlocker blocker(nav_);
  nav_->clear();

  auto add = [this](const QString& label, const QString& id,
                    bool enabled = true) {
    auto* item = new QListWidgetItem(label, nav_);
    item->setData(Qt::UserRole, id);
    if (!enabled) {
      item->setFlags(Qt::NoItemFlags);
      item->setForeground(QColor(theme_.text_tertiary));
    }
    return item;
  };

  add(QStringLiteral("CONFIGURATION"), QString(), false);
  for (const SectionSpec& section : Sections()) {
    add(section.label, section.id);
  }

  add(QStringLiteral("APPLICATION"), QString(), false);
  add(QStringLiteral("Providers"), QStringLiteral("providers"));
  add(QStringLiteral("API keys"), QStringLiteral("keys"));
  add(QStringLiteral("Credential vault"), QStringLiteral("vault"));
  add(QStringLiteral("Gateway"), QStringLiteral("gateway"));
  add(QStringLiteral("Themes"), QStringLiteral("themes"));
  add(QStringLiteral("Notifications"), QStringLiteral("notifications"));
  add(QStringLiteral("Keybinds"), QStringLiteral("keybinds"));
  add(QStringLiteral("Sessions"), QStringLiteral("sessions"));
  add(QStringLiteral("Plugins"), QStringLiteral("plugins"));
  add(QStringLiteral("Billing"), QStringLiteral("billing"));
  add(QStringLiteral("Scheduled tasks"), QStringLiteral("cron"));
  add(QStringLiteral("Capabilities"), QStringLiteral("capabilities"));
  add(QStringLiteral("Local models"), QStringLiteral("localmodels"));
  add(QStringLiteral("Delegation"), QStringLiteral("delegation"));
  add(QStringLiteral("About"), QStringLiteral("about"));

  for (int i = 0; i < nav_->count(); ++i) {
    if (nav_->item(i)->data(Qt::UserRole).toString() == active_id_) {
      nav_->setCurrentRow(i);
      break;
    }
  }
}

void SettingsView::SetTheme(const Theme& theme) {
  theme_ = theme;
  setStyleSheet(
      QStringLiteral(
          "QWidget#settingsView { background: %1; }"
          "QLabel#settingsTitle { color: %2; font-size: 20px; "
          "font-weight: 600; }"
          "QLabel#settingsStatus { color: %3; font-size: 11px; }"
          "QLineEdit#settingsSearch { background: %4; color: %2; "
          "border: 1px solid %5; border-radius: 8px; padding: 6px 10px; }"
          "QPushButton#settingsClose { background: transparent; color: %3; "
          "border: 1px solid %5; border-radius: 8px; padding: 6px 12px; }"
          "QPushButton#settingsClose:hover { color: %2; background: %6; }"
          "QListWidget#settingsNav { background: transparent; border: none; "
          "color: %3; font-size: 13px; outline: none; }"
          "QListWidget#settingsNav::item { padding: 7px 10px; "
          "border-radius: 7px; }"
          "QListWidget#settingsNav::item:selected { background: %6; "
          "color: %2; }"
          "QListWidget#settingsNav::item:hover { background: %7; }"
          "QScrollArea#settingsScroll { background: transparent; "
          "border: none; }"
          "QWidget#settingsForm { background: transparent; }"
          "QLabel#settingTitle { color: %2; font-size: 13px; "
          "font-weight: 500; }"
          "QLabel#settingDescription { color: %3; font-size: 11px; }"
          "QCheckBox { color: %2; }"
          "QComboBox, QLineEdit, QDoubleSpinBox { background: %4; color: %2; "
          "border: 1px solid %5; border-radius: 7px; padding: 5px 8px; "
          "min-height: 22px; }"
          "QComboBox QAbstractItemView { background: %4; color: %2; "
          "selection-background-color: %6; }"
          "QPushButton[subpageChip=\"true\"] { background: transparent; "
          "color: %3; border: 1px solid %5; border-radius: 999px; "
          "padding: 4px 10px; font-size: 11px; }"
          "QPushButton[subpageChip=\"true\"]:hover { color: %2; "
          "background: %7; }"
          "QPushButton[subpageChip=\"true\"]:checked { color: %2; "
          "background: %6; border-color: %6; }")
          .arg(theme_.background.name(), theme_.foreground.name(),
               theme_.text_secondary.name(), theme_.surface.name(),
               theme_.stroke.name(), theme_.accent_soft.name(),
               Mix(theme_.surface, theme_.foreground, 0.06).name()));
}

void SettingsView::SetSnapshot(const ProfileSnapshot& snapshot) {
  snapshot_ = snapshot;
  PublishPreferences();
  const bool model_data_page =
      active_id_ == QStringLiteral("model") &&
      (subpage_by_section_.value(active_id_) == QStringLiteral("auxiliary") ||
       subpage_by_section_.value(active_id_) == QStringLiteral("moa"));
  const bool special_page = IsDataPage(active_id_) || model_data_page;
  const bool memory_page = active_id_ == QStringLiteral("memory");
  const bool revision_changed = snapshot_.revision != last_revision_;
  const bool artifacts_page_unchanged =
      active_id_ == QStringLiteral("artifacts") &&
      DataPageSignature(active_id_) == active_page_signature_;
  last_revision_ = snapshot_.revision;

  if (!snapshot_.config_loaded) {
    if (IsOpen() && (special_page || memory_page) && revision_changed &&
        !artifacts_page_unchanged) {
      RebuildForm();
    }
    if (!snapshot_.config_error.isEmpty()) {
      SetStatus(snapshot_.config_error, true);
    } else if (isVisible()) {
      SetStatus(QStringLiteral("Loading configuration..."));
    }
    return;
  }

  const QByteArray serialized =
      QJsonDocument(snapshot_.config).toJson(QJsonDocument::Compact);
  const QString hash = QString::fromUtf8(serialized);
  if (hash != config_hash_) {
    config_hash_ = hash;
    if (IsOpen()) {
      RebuildForm();
    }
  } else if (IsOpen() && (special_page || memory_page) && revision_changed &&
             !artifacts_page_unchanged) {
    // Session rows, gateway metadata and memory-provider status change without
    // a config edit. Artifacts is the exception: its page is large and its
    // scroll state belongs to the reader, so only rebuild when its page state
    // (or cwd / selected preview) actually changes.
    RebuildForm();
  }
  if (snapshot_.config_saving) {
    SetStatus(QStringLiteral("Saving..."));
  } else if (!snapshot_.config_error.isEmpty()) {
    SetStatus(snapshot_.config_error, true);
  } else {
    SetStatus(QStringLiteral("Changes save automatically"));
  }
}

void SettingsView::SetPref(const QString& key, const QString& value) {
  SetNativePref(key, value);
  if (on_pref_changed) {
    on_pref_changed(key, value);
  }
  if (key.startsWith(QStringLiteral("keybind."))) {
    PublishPreferences();
  } else if (key == QStringLiteral("native.font_scale")) {
    const double scale = value.toDouble();
    if (scale > 0.0 && on_font_scale) {
      last_font_scale_ = scale;
      on_font_scale(scale);
    }
    if (on_appearance) {
      on_appearance();
    }
  } else if (key.startsWith(QStringLiteral("appearance."))) {
    if (on_appearance) {
      on_appearance();
    }
  } else if (key == QStringLiteral("native.theme") && on_theme) {
    on_theme(value == QStringLiteral("dark") ? ThemeMode::kDark
                                             : ThemeMode::kLight);
  }
}

void SettingsView::ApplyStoredPreferences() { PublishPreferences(); }

void SettingsView::PublishPreferences() {
  QHash<QString, QString> overrides;
  QString fingerprint;
  QStringList keys = NativePrefKeys();
  keys.sort();
  for (const QString& key : keys) {
    if (!key.startsWith(QStringLiteral("keybind."))) {
      continue;
    }
    const QString sequence = NativePref(key);
    if (sequence.isEmpty()) {
      continue;
    }
    overrides.insert(key.mid(8), sequence);
    fingerprint += key + QLatin1Char('=') + sequence + QLatin1Char(';');
  }
  if (fingerprint != last_keybinds_hash_ && on_keybinds) {
    last_keybinds_hash_ = fingerprint;
    on_keybinds(overrides);
  }

  const double scale =
      NativePref(QStringLiteral("native.font_scale")).toDouble();
  if (scale > 0.0 && qAbs(scale - last_font_scale_) > 0.001 && on_font_scale) {
    last_font_scale_ = scale;
    on_font_scale(scale);
  }
  if (on_appearance) {
    on_appearance();
  }
}

void SettingsView::Open() {
  show();
  raise();
  if (on_refetch) {
    on_refetch();
  }
  BuildNav();
  if (IsDataPage(active_id_) && on_page_request &&
      !snapshot_.pages.contains(active_id_)) {
    on_page_request(active_id_);
  }
  if (active_id_ == QStringLiteral("memory") && on_page_request &&
      !snapshot_.pages.contains(QStringLiteral("memory"))) {
    on_page_request(QStringLiteral("memory"));
  }
  if (active_id_ == QStringLiteral("model") && on_page_request) {
    const QString subpage = subpage_by_section_.value(active_id_);
    if (subpage == QStringLiteral("auxiliary") &&
        !snapshot_.pages.contains(QStringLiteral("modelaux"))) {
      on_page_request(QStringLiteral("modelaux"));
    } else if (subpage == QStringLiteral("moa") &&
               !snapshot_.pages.contains(QStringLiteral("modelmoa"))) {
      on_page_request(QStringLiteral("modelmoa"));
    }
  }
  if (snapshot_.config_loaded) {
    RebuildForm();
  } else {
    ShowPlaceholder(QStringLiteral("Loading settings"),
                    QStringLiteral("Fetching /api/config from the gateway."));
    SetStatus(QStringLiteral("Loading configuration..."));
  }
  search_->setFocus();
}

void SettingsView::OpenPage(const QString& id) {
  QString page = id;
  pending_field_.clear();
  const int hash = id.indexOf(QLatin1Char('#'));
  if (hash >= 0) {
    page = id.left(hash);
    pending_field_ = id.mid(hash + 1).trimmed();
  } else {
    // Legacy links sometimes carry ?field=key; accept it without importing a
    // URL parser into the settings route.
    const int query = id.indexOf(QLatin1String("?field="));
    if (query >= 0) {
      page = id.left(query);
      pending_field_ = id.mid(query + 7).trimmed();
    }
  }
  if (!pending_field_.isEmpty()) {
    const QString subpage = ConfigSubpageForField(page, pending_field_);
    if (!subpage.isEmpty()) {
      subpage_by_section_[page] = subpage;
    }
  }
  active_id_ = page;
  Open();
  ShowSection(page, true);
  for (int i = 0; i < nav_->count(); ++i) {
    if (nav_->item(i)->data(Qt::UserRole).toString() == page) {
      const QSignalBlocker blocker(nav_);
      nav_->setCurrentRow(i);
      break;
    }
  }
  const QString field = pending_field_;
  pending_field_.clear();
  if (!field.isEmpty()) {
    QTimer::singleShot(0, this, [this, field]() {
      QString object_name = field;
      object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
      QWidget* row =
          form_->findChild<QWidget*>(QStringLiteral("setting_") + object_name);
      if (row == nullptr) {
        return;
      }
      scroll_->ensureWidgetVisible(row, 20, 20);
      if (QWidget* control = row->findChild<QWidget*>(
              QStringLiteral("settingControl_") + object_name)) {
        control->setFocus();
      } else if (QWidget* control = row->findChild<QLineEdit*>()) {
        control->setFocus();
      }
    });
  }
}

bool SettingsView::IsOpen() const { return isVisible(); }

void SettingsView::ShowSection(const QString& id, bool request_data) {
  active_id_ = id;
  if (id.startsWith(QStringLiteral("todo:"))) {
    ShowPlaceholder(
        QStringLiteral("Not ported yet"),
        QStringLiteral("This settings surface is tracked in TODO.md. The "
                       "desktop route exists; the native controls are not "
                       "wired yet."));
    return;
  }
  if (IsDataPage(id) || IsLocalPage(id)) {
    if (request_data && IsDataPage(id) && on_page_request) {
      on_page_request(id);
    }
    RebuildForm();
    return;
  }
  if (id == QStringLiteral("memory") && request_data && on_page_request &&
      !snapshot_.pages.contains(QStringLiteral("memory"))) {
    on_page_request(QStringLiteral("memory"));
  }
  RebuildForm();
}

void SettingsView::ClearForm() {
  if (form_layout_ == nullptr) {
    return;
  }
  while (QLayoutItem* item = form_layout_->takeAt(0)) {
    if (item->widget() != nullptr) {
      item->widget()->deleteLater();
    }
    delete item;
  }
}

void SettingsView::ScheduleFormScrollRestore(
    int outer_value, const std::vector<std::pair<QString, int>>& inner_values,
    quint64 generation) {
  const auto apply = [this, outer_value, inner_values](bool only_if_zero) {
    if (scroll_ == nullptr || form_ == nullptr) {
      return;
    }
    // Apply any pending layout requests before touching the scroll ranges; a
    // rebuilt page posts its geometry after the rebuild call returns.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    const auto restore = [only_if_zero](QScrollBar* bar, int value) {
      if (bar == nullptr) {
        return;
      }
      // Later passes only repair a stale zero range / reset-to-top. Once the
      // reader scrolls somewhere themselves, leave it alone.
      if (only_if_zero && (value <= 0 || bar->value() != 0)) {
        return;
      }
      bar->setValue(value);
    };
    restore(scroll_->verticalScrollBar(), outer_value);
    QSet<QWidget*> current_widgets;
    std::function<void(QLayout*)> collect = [&](QLayout* layout) {
      if (layout == nullptr) {
        return;
      }
      for (int index = 0; index < layout->count(); ++index) {
        QLayoutItem* item = layout->itemAt(index);
        if (item == nullptr) {
          continue;
        }
        if (QWidget* widget = item->widget()) {
          current_widgets.insert(widget);
          for (QWidget* child : widget->findChildren<QWidget*>()) {
            current_widgets.insert(child);
          }
        }
        collect(item->layout());
      }
    };
    collect(form_layout_);
    for (QAbstractScrollArea* area :
         form_->findChildren<QAbstractScrollArea*>()) {
      if (!current_widgets.contains(area) || area->objectName().isEmpty()) {
        continue;
      }
      for (const auto& [name, value] : inner_values) {
        if (name == area->objectName()) {
          restore(area->verticalScrollBar(), value);
          break;
        }
      }
    }
  };
  // The new page's layout request is often posted after this zero timer. Apply
  // immediately, then retry after layout has settled; a single tick can run
  // before the list/preview scrollbar ranges exist and clamp the value to zero.
  QTimer::singleShot(0, this, [this, generation, apply]() {
    if (generation == form_scroll_generation_) {
      apply(false);
    }
  });
  for (int delay : {16, 100, 300}) {
    QTimer::singleShot(delay, this, [this, generation, apply]() {
      if (generation == form_scroll_generation_) {
        apply(true);
      }
    });
  }
}

QString SettingsView::DataPageSignature(const QString& id) const {
  const auto it = snapshot_.pages.constFind(id);
  QJsonObject state;
  if (it == snapshot_.pages.constEnd()) {
    state.insert(QStringLiteral("missing"), true);
  } else {
    state.insert(QStringLiteral("loading"), it->loading);
    state.insert(QStringLiteral("loaded"), it->loaded);
    state.insert(QStringLiteral("error"), it->error);
    state.insert(QStringLiteral("notice"), it->notice);
    state.insert(QStringLiteral("data"), it->data);
  }
  if (id == QStringLiteral("artifacts")) {
    state.insert(QStringLiteral("cwd"), snapshot_.artifacts_cwd);
    state.insert(QStringLiteral("preview"), snapshot_.artifacts_preview);
  }
  const QByteArray compact =
      QJsonDocument(state).toJson(QJsonDocument::Compact);
  return QString::fromLatin1(
      QCryptographicHash::hash(compact, QCryptographicHash::Sha1).toHex());
}

void SettingsView::RebuildForm() {
  if (!form_layout_) {
    return;
  }
  active_page_signature_ = DataPageSignature(active_id_);
  // Form rebuilds are frequent: config saves, page revisions and subpage
  // switches all replace the whole page. Keep the reader's place when the
  // same section is rebuilt, but start a newly opened section at the top.
  const bool preserve_scroll = active_id_ == last_built_form_id_;
  last_built_form_id_ = active_id_;
  const quint64 scroll_generation = ++form_scroll_generation_;
  const int outer_scroll = preserve_scroll && scroll_ != nullptr
                               ? scroll_->verticalScrollBar()->value()
                               : 0;
  std::vector<std::pair<QString, int>> inner_scrolls;
  if (preserve_scroll && form_ != nullptr) {
    for (QAbstractScrollArea* area :
         form_->findChildren<QAbstractScrollArea*>()) {
      if (area->objectName().isEmpty()) {
        continue;
      }
      inner_scrolls.emplace_back(area->objectName(),
                                 area->verticalScrollBar()->value());
    }
  }
  ScheduleFormScrollRestore(outer_scroll, inner_scrolls, scroll_generation);
  if (IsDataPage(active_id_) || IsLocalPage(active_id_)) {
    PageContext context;
    context.theme = theme_;
    context.snapshot = &snapshot_;
    context.refresh = on_page_request;
    context.action = on_page_action;
    context.open_session = on_select_session;
    context.set_theme = on_theme;
    context.set_theme_preset = on_theme_preset;
    context.theme_preset = NativePref(QStringLiteral("native.theme.preset"));
    context.get_pref = [](const QString& key) { return NativePref(key); };
    context.set_pref = [this](const QString& key, const QString& value) {
      SetPref(key, value);
    };
    context.send_notification = on_send_notification;

    building_ = true;
    ClearForm();
    QWidget* page = BuildDataPage(active_id_, context, form_);
    if (page != nullptr) {
      form_layout_->addWidget(page);
    } else {
      ShowPlaceholder(QStringLiteral("Unknown section"), active_id_);
    }
    form_layout_->addStretch(1);
    building_ = false;
    return;
  }

  building_ = true;
  ClearForm();

  if (!snapshot_.config_loaded) {
    auto* loading =
        new QLabel(QStringLiteral("Loading configuration..."), form_);
    loading->setObjectName(QStringLiteral("settingDescription"));
    form_layout_->addWidget(loading);
    building_ = false;
    return;
  }

  const std::vector<SectionSpec> sections = Sections();
  const auto it = std::find_if(
      sections.begin(), sections.end(),
      [this](const SectionSpec& section) { return section.id == active_id_; });
  if (it == sections.end()) {
    ShowPlaceholder(QStringLiteral("Unknown section"), active_id_);
    building_ = false;
    return;
  }

  auto* heading = new QLabel(it->label, form_);
  heading->setObjectName(QStringLiteral("settingsTitle"));
  form_layout_->addWidget(heading);

  auto* count = new QLabel(
      QStringLiteral("%1 backend keys in this section").arg(it->keys.size()),
      form_);
  count->setObjectName(QStringLiteral("settingDescription"));
  form_layout_->addWidget(count);

  const QString query =
      search_ != nullptr ? search_->text().trimmed().toLower() : QString();
  const bool filter_fields =
      !query.isEmpty() &&
      std::any_of(it->keys.begin(), it->keys.end(),
                  [&query](const QString& key) {
                    return key.toLower().contains(query) ||
                           PrettyKey(key).toLower().contains(query);
                  });

  QString selected_subpage;
  const auto subpages = ConfigSubpages().constFind(active_id_);
  if (!filter_fields && subpages != ConfigSubpages().constEnd() &&
      !subpages.value().empty()) {
    QString& stored = subpage_by_section_[active_id_];
    const bool known =
        std::any_of(subpages.value().begin(), subpages.value().end(),
                    [&stored](const ConfigSubpageSpec& page) {
                      return stored == QLatin1String(page.id);
                    });
    if (!known) {
      stored = QLatin1String(subpages.value().front().id);
    }
    selected_subpage = stored;

    auto* chip_row = new QWidget(form_);
    chip_row->setObjectName(QStringLiteral("settingsSubpages"));
    auto* chips = new QHBoxLayout(chip_row);
    chips->setContentsMargins(0, 0, 0, 0);
    chips->setSpacing(6);
    for (const ConfigSubpageSpec& page : subpages.value()) {
      auto* chip = new QPushButton(QString::fromLatin1(page.label), chip_row);
      const QString id = QString::fromLatin1(page.id);
      chip->setObjectName(QStringLiteral("settingsSubpage_") + active_id_ +
                          QLatin1Char('_') + id);
      chip->setProperty("subpageChip", true);
      chip->setCheckable(true);
      chip->setChecked(id == selected_subpage);
      connect(chip, &QPushButton::clicked, this, [this, id]() {
        subpage_by_section_[active_id_] = id;
        RebuildForm();
      });
      chips->addWidget(chip);
    }
    chips->addStretch(1);
    form_layout_->addWidget(chip_row);
  }

  const bool model_main = active_id_ == QStringLiteral("model") &&
                          selected_subpage == QStringLiteral("main");
  const bool auxiliary_page = active_id_ == QStringLiteral("model") &&
                              selected_subpage == QStringLiteral("auxiliary");
  const bool moa_page = active_id_ == QStringLiteral("model") &&
                        selected_subpage == QStringLiteral("moa");
  const bool fallback_page = active_id_ == QStringLiteral("model") &&
                             selected_subpage == QStringLiteral("fallbacks");
  if (model_main) {
    AppendModelMainRows();
  } else if (auxiliary_page) {
    AppendAuxiliaryRows();
  } else if (moa_page) {
    AppendMoaRows();
  } else if (fallback_page) {
    AppendFallbackRows(ValueAt(QStringLiteral("fallback_providers")));
  }

  for (const QString& key : it->keys) {
    if ((model_main && (key == QStringLiteral("model") ||
                        key == QStringLiteral("agent.reasoning_effort"))) ||
        (fallback_page && key == QStringLiteral("fallback_providers"))) {
      continue;
    }
    if (filter_fields) {
      if (!key.toLower().contains(query) &&
          !PrettyKey(key).toLower().contains(query)) {
        continue;
      }
    } else if (!selected_subpage.isEmpty() &&
               ConfigSubpageForField(active_id_, key) != selected_subpage) {
      continue;
    }
    form_layout_->addWidget(BuildField(key, ValueAt(key)));
  }
  if (active_id_ == QStringLiteral("voice")) {
    UpdateVoiceFieldVisibility();
    // Device rows are native-only and span provider families, so they stay
    // visible on every voice subpage like the desktop's embedded controls.
    AppendAudioDeviceRows();
  } else if (active_id_ == QStringLiteral("memory")) {
    if (selected_subpage == QStringLiteral("persistent") ||
        selected_subpage.isEmpty()) {
      AppendMemoryRows();
    }
  }
  form_layout_->addStretch(1);
  building_ = false;
}

QWidget* SettingsView::BuildField(const QString& key, const QJsonValue& value) {
  const QJsonObject schema = snapshot_.config_schema.value(key).toObject();
  QString type = schema.value(QStringLiteral("type")).toString();
  if (type.isEmpty()) {
    if (value.isBool()) {
      type = QStringLiteral("boolean");
    } else if (value.isDouble()) {
      type = QStringLiteral("number");
    } else if (value.isArray()) {
      type = QStringLiteral("list");
    } else {
      type = QStringLiteral("string");
    }
  }

  auto* row = new QWidget(form_);
  QString object_name = key;
  object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
  row->setObjectName(QStringLiteral("setting_") + object_name);
  row->setProperty("configKey", key);
  auto* outer = new QHBoxLayout(row);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(16);

  auto* labels = new QVBoxLayout();
  labels->setSpacing(2);
  auto* title = new QLabel(PrettyKey(key), row);
  title->setObjectName(QStringLiteral("settingTitle"));
  auto* description =
      new QLabel(schema.value(QStringLiteral("description")).toString(), row);
  description->setObjectName(QStringLiteral("settingDescription"));
  description->setWordWrap(true);
  labels->addWidget(title);
  labels->addWidget(description);
  outer->addLayout(labels, 1);

  QWidget* control = nullptr;
  if (type == QStringLiteral("boolean")) {
    auto* box = new QCheckBox(row);
    box->setChecked(value.toBool());
    connect(box, &QCheckBox::toggled, this,
            [this, key](bool checked) { QueueSave(key, checked); });
    control = box;
  } else if (type == QStringLiteral("select") ||
             schema.value(QStringLiteral("options")).isArray() ||
             EnumOptions().contains(key)) {
    auto* combo = new QComboBox(row);
    combo->setObjectName(QStringLiteral("settingControl_") + object_name);
    const QJsonArray options =
        schema.value(QStringLiteral("options")).toArray();
    for (const QJsonValue& option : options) {
      combo->addItem(AsString(option), AsString(option));
    }
    if (options.isEmpty()) {
      for (const QString& option : EnumOptions().value(key)) {
        combo->addItem(option, option);
      }
    }
    const bool free_input = IsFreeInputKey(key);
    combo->setEditable(free_input);
    if (free_input) {
      combo->setInsertPolicy(QComboBox::NoInsert);
    }
    const QString current = AsString(value);
    if (!current.isEmpty() && combo->findData(current) < 0) {
      combo->addItem(current, current);
    }
    combo->setCurrentIndex(combo->findData(current));
    if (free_input) {
      combo->setCurrentText(current);
      connect(combo, &QComboBox::currentTextChanged, this,
              [this, key](const QString& text) { QueueSave(key, text); });
    } else {
      connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [this, key, combo](int index) {
                QueueSave(key, combo->itemData(index).toString());
              });
    }
    control = combo;
  } else if (type == QStringLiteral("number")) {
    auto* spin = new QDoubleSpinBox(row);
    spin->setDecimals(value.isDouble() && value.toDouble() !=
                                              static_cast<int>(value.toDouble())
                          ? 2
                          : 0);
    spin->setRange(-1000000.0, 1000000.0);
    spin->setValue(value.toDouble());
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, key](double number) { QueueSave(key, number); });
    control = spin;
  } else if (key == QStringLiteral("fallback_providers")) {
    auto* edit = new QLineEdit(row);
    edit->setObjectName(QStringLiteral("setting_fallback_providers"));
    edit->setPlaceholderText(QStringLiteral("provider/model, provider/model"));
    edit->setText(FormatFallbackChain(value.toArray()));
    const QJsonArray previous = value.toArray();
    connect(edit, &QLineEdit::editingFinished, this,
            [this, key, edit, previous]() {
              QueueSave(key, ParseFallbackChain(edit->text(), previous));
            });
    control = edit;
  } else if (type == QStringLiteral("list")) {
    auto* edit = new QLineEdit(row);
    QStringList parts;
    for (const QJsonValue& entry : value.toArray()) {
      parts.append(AsString(entry));
    }
    edit->setText(parts.join(QStringLiteral(", ")));
    connect(edit, &QLineEdit::editingFinished, this, [this, key, edit]() {
      QJsonArray list;
      for (const QString& part :
           edit->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        list.append(part.trimmed());
      }
      QueueSave(key, list);
    });
    control = edit;
  } else {
    auto* edit = new QLineEdit(row);
    edit->setText(AsString(value));
    edit->setReadOnly(key == QStringLiteral("fallback_providers"));
    connect(edit, &QLineEdit::editingFinished, this,
            [this, key, edit]() { QueueSave(key, edit->text()); });
    control = edit;
  }

  control->setFixedWidth(300);
  outer->addWidget(control, 0, Qt::AlignTop);
  return row;
}

QWidget* SettingsView::BuildAudioDeviceRow(const QString& key,
                                           const QString& label,
                                           const QString& description,
                                           bool input) {
  auto* row = new QWidget(form_);
  QString object_name = key;
  object_name.replace(QLatin1Char('.'), QLatin1Char('_'));
  row->setObjectName(QStringLiteral("setting_") + object_name);
  auto* outer = new QHBoxLayout(row);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(16);

  auto* labels = new QVBoxLayout();
  labels->setSpacing(2);
  auto* title = new QLabel(label, row);
  title->setObjectName(QStringLiteral("settingTitle"));
  auto* hint = new QLabel(description, row);
  hint->setObjectName(QStringLiteral("settingDescription"));
  hint->setWordWrap(true);
  labels->addWidget(title);
  labels->addWidget(hint);
  outer->addLayout(labels, 1);

  auto* combo = new QComboBox(row);
  combo->setObjectName(input ? QStringLiteral("audioInputDevice")
                             : QStringLiteral("audioOutputDevice"));
  combo->addItem(input ? QStringLiteral("System default microphone")
                       : QStringLiteral("System default output"),
                 QString());
#ifdef HERMES_HAS_MULTIMEDIA
  const QList<QAudioDevice> devices =
      input ? QMediaDevices::audioInputs() : QMediaDevices::audioOutputs();
  for (const QAudioDevice& device : devices) {
    if (device.isNull()) {
      continue;
    }
    combo->addItem(device.description(), QString::fromUtf8(device.id()));
  }
#endif
  const QString stored = NativePref(key);
  const int stored_index = combo->findData(stored);
  combo->setCurrentIndex(stored_index < 0 ? 0 : stored_index);
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this, key, combo](int index) {
            SetPref(key, combo->itemData(index).toString());
          });
  combo->setFixedWidth(300);
  outer->addWidget(combo, 0, Qt::AlignTop);
  return row;
}

void SettingsView::UpdateVoiceFieldVisibility() {
  if (form_ == nullptr) {
    return;
  }
  for (QWidget* row : form_->findChildren<QWidget*>()) {
    const QString key = row->property("configKey").toString();
    if (!key.isEmpty()) {
      row->setVisible(VoiceFieldVisible(key, snapshot_.config));
    }
  }
}

void SettingsView::AppendMemoryRows() {
  auto* heading = new QLabel(QStringLiteral("Memory layers"), form_);
  heading->setObjectName(QStringLiteral("memoryLayersHeading"));
  heading->setStyleSheet(QStringLiteral("font-weight: 600;"));
  form_layout_->addWidget(heading);

  const PageState status_page = snapshot_.pages.value(QStringLiteral("memory"));
  if (!status_page.loaded) {
    auto* loading = new QLabel(
        status_page.error.isEmpty()
            ? QStringLiteral("Loading memory provider status...")
            : QStringLiteral("Memory provider status failed to load: %1")
                  .arg(status_page.error),
        form_);
    loading->setObjectName(status_page.error.isEmpty()
                               ? QStringLiteral("memoryStatusLoading")
                               : QStringLiteral("memoryStatusError"));
    loading->setWordWrap(true);
    loading->setStyleSheet(QStringLiteral("font-size: 11px;"));
    form_layout_->addWidget(loading);
  } else {
    const QJsonObject status =
        status_page.data.value(QStringLiteral("status")).toObject();
    const QString active = status.value(QStringLiteral("active")).toString();
    QString provider_state;
    for (const QJsonValue& value :
         status.value(QStringLiteral("providers")).toArray()) {
      const QJsonObject provider = value.toObject();
      if (provider.value(QStringLiteral("name")).toString() != active) {
        continue;
      }
      const QString state = provider.value(QStringLiteral("status")).toString();
      if (state == QStringLiteral("ready")) {
        provider_state = QStringLiteral("Ready");
      } else if (state == QStringLiteral("needs_config")) {
        provider_state = QStringLiteral("Needs setup");
      } else if (state == QStringLiteral("missing")) {
        provider_state = QStringLiteral("Missing");
      } else if (state == QStringLiteral("unavailable")) {
        provider_state = QStringLiteral("Unavailable");
      }
      break;
    }

    auto add_layer = [this](const QString& object_name, const QString& title,
                            const QString& value, const QString& description) {
      auto* row = new QWidget(form_);
      row->setObjectName(object_name);
      auto* outer = new QHBoxLayout(row);
      outer->setContentsMargins(0, 0, 0, 0);
      outer->setSpacing(16);
      auto* labels = new QVBoxLayout();
      labels->setSpacing(2);
      auto* label = new QLabel(title, row);
      label->setObjectName(QStringLiteral("settingTitle"));
      auto* hint = new QLabel(description, row);
      hint->setObjectName(QStringLiteral("settingDescription"));
      hint->setWordWrap(true);
      labels->addWidget(label);
      labels->addWidget(hint);
      outer->addLayout(labels, 1);
      auto* state = new QLabel(value, row);
      state->setObjectName(QStringLiteral("settingTitle"));
      state->setAlignment(Qt::AlignRight | Qt::AlignTop);
      state->setWordWrap(true);
      outer->addWidget(state, 0, Qt::AlignTop);
      form_layout_->addWidget(row);
    };

    const QString active_value =
        active.isEmpty()
            ? QStringLiteral("Built-in memory only")
            : (provider_state.isEmpty()
                   ? active
                   : QStringLiteral("%1 · %2").arg(active, provider_state));
    add_layer(QStringLiteral("memoryLayerActive"),
              QStringLiteral("Active provider"), active_value,
              // This is the saved provider reported by the gateway, not the
              // unsaved `memory.provider` draft above; the draft becomes
              // active after /api/config autosaves.
              QStringLiteral("The provider the runtime is using for this "
                             "profile."));

    const bool builtin_memory =
        ValueAt(QStringLiteral("memory.memory_enabled")).toBool();
    const bool builtin_user =
        ValueAt(QStringLiteral("memory.user_profile_enabled")).toBool();
    QString builtin_value = QStringLiteral("Off");
    if (builtin_memory && builtin_user) {
      builtin_value = QStringLiteral("On · MEMORY.md + USER.md");
    } else if (builtin_memory) {
      builtin_value = QStringLiteral("On · MEMORY.md only");
    } else if (builtin_user) {
      builtin_value = QStringLiteral("On · USER.md only");
    }
    const QJsonObject files =
        status.value(QStringLiteral("builtin_files")).toObject();
    const auto format_bytes = [](const QJsonValue& value) {
      const qint64 bytes = static_cast<qint64>(value.toDouble());
      if (bytes <= 0) {
        return QStringLiteral("empty");
      }
      if (bytes < 1024) {
        return QStringLiteral("%1 B").arg(bytes);
      }
      return QStringLiteral("%1 KB").arg(
          QString::number(static_cast<double>(bytes) / 1024.0, 'f', 1));
    };
    add_layer(QStringLiteral("memoryLayerBuiltin"),
              QStringLiteral("Built-in layer"), builtin_value,
              QStringLiteral("MEMORY.md %1 · USER.md %2 · retained even when "
                             "an external provider is active.")
                  .arg(format_bytes(files.value(QStringLiteral("memory"))),
                       format_bytes(files.value(QStringLiteral("user")))));
  }

  const QString active = status_page.loaded
                             ? status_page.data.value(QStringLiteral("status"))
                                   .toObject()
                                   .value(QStringLiteral("active"))
                                   .toString()
                             : QString();
  if (active.isEmpty()) {
    return;
  }

  auto* provider_heading =
      new QLabel(QStringLiteral("%1 settings").arg(active), form_);
  provider_heading->setObjectName(QStringLiteral("memoryProviderHeading"));
  provider_heading->setStyleSheet(QStringLiteral("font-weight: 600;"));
  form_layout_->addWidget(provider_heading);

  const QString page_key = QStringLiteral("memory:") + active;
  const PageState config_page = snapshot_.pages.value(page_key);
  if (!snapshot_.pages.contains(page_key)) {
    if (memory_config_requested_ != page_key && on_page_request) {
      memory_config_requested_ = page_key;
      QTimer::singleShot(0, this, [this, page_key]() {
        if (on_page_request) {
          on_page_request(page_key);
        }
      });
    }
    auto* loading =
        new QLabel(QStringLiteral("Loading %1 settings...").arg(active), form_);
    loading->setObjectName(QStringLiteral("memoryProviderLoading"));
    loading->setStyleSheet(QStringLiteral("font-size: 11px;"));
    form_layout_->addWidget(loading);
    return;
  }
  if (config_page.loading) {
    auto* loading =
        new QLabel(QStringLiteral("Loading %1 settings...").arg(active), form_);
    loading->setObjectName(QStringLiteral("memoryProviderLoading"));
    loading->setStyleSheet(QStringLiteral("font-size: 11px;"));
    form_layout_->addWidget(loading);
    return;
  }
  if (!config_page.error.isEmpty()) {
    auto* error =
        new QLabel(QStringLiteral("Memory provider settings failed to load: %1")
                       .arg(config_page.error),
                   form_);
    error->setObjectName(QStringLiteral("memoryProviderError"));
    error->setWordWrap(true);
    error->setStyleSheet(QStringLiteral("font-size: 11px;"));
    form_layout_->addWidget(error);
    return;
  }

  const QJsonObject provider_config =
      config_page.data.value(QStringLiteral("config")).toObject();
  const QJsonArray fields =
      provider_config.value(QStringLiteral("fields")).toArray();
  if (fields.isEmpty()) {
    auto* empty = new QLabel(
        QStringLiteral("This provider has no configurable settings."), form_);
    empty->setObjectName(QStringLiteral("memoryProviderEmpty"));
    empty->setStyleSheet(QStringLiteral("font-size: 11px;"));
    form_layout_->addWidget(empty);
    return;
  }

  QString provider_path =
      QStringLiteral("/api/memory/providers/%1/config?surface=declared")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(active)));
  if (!snapshot_.name.isEmpty()) {
    provider_path +=
        QStringLiteral("&profile=%1")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(snapshot_.name)));
  }

  QString last_group;
  for (const QJsonValue& value : fields) {
    const QJsonObject field = value.toObject();
    const QString key = field.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) {
      continue;
    }
    QString object_key = key;
    object_key.replace(QLatin1Char('.'), QLatin1Char('_'));

    const QString group = field.value(QStringLiteral("group")).toString();
    if (!group.isEmpty() && group != last_group) {
      last_group = group;
      auto* group_label = new QLabel(group, form_);
      QString group_object = group.toLower();
      group_object.replace(QLatin1Char(' '), QLatin1Char('_'));
      group_label->setObjectName(QStringLiteral("memoryGroup_") + group_object);
      group_label->setStyleSheet(QStringLiteral("font-weight: 600;"));
      form_layout_->addWidget(group_label);
    }

    auto* row = new QWidget(form_);
    row->setObjectName(QStringLiteral("memoryField_") + object_key);
    auto* outer = new QHBoxLayout(row);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(16);
    auto* labels = new QVBoxLayout();
    labels->setSpacing(2);
    auto* title =
        new QLabel(field.value(QStringLiteral("label")).toString().isEmpty()
                       ? PrettyKey(key)
                       : field.value(QStringLiteral("label")).toString(),
                   row);
    title->setObjectName(QStringLiteral("settingTitle"));
    QString description = field.value(QStringLiteral("description")).toString();
    const QString info = field.value(QStringLiteral("info")).toString();
    if (!info.isEmpty()) {
      description +=
          (description.isEmpty() ? QString() : QStringLiteral("\n")) + info;
    }
    auto* hint = new QLabel(description, row);
    hint->setObjectName(QStringLiteral("settingDescription"));
    hint->setWordWrap(true);
    labels->addWidget(title);
    labels->addWidget(hint);
    outer->addLayout(labels, 1);

    const QString kind = field.value(QStringLiteral("kind")).toString();
    const QString current = AsString(field.value(QStringLiteral("value")));
    const QString placeholder =
        field.value(QStringLiteral("placeholder")).toString();
    const auto commit = [this, page_key, provider_path,
                         key](const QString& text) {
      if (on_page_action) {
        on_page_action(
            page_key, QStringLiteral("PUT"), provider_path,
            QJsonObject{{QStringLiteral("values"), QJsonObject{{key, text}}}});
      }
      SetStatus(QStringLiteral("Saving memory provider..."));
    };

    QWidget* control = nullptr;
    if (kind == QStringLiteral("bool")) {
      auto* box = new QCheckBox(row);
      box->setObjectName(QStringLiteral("memoryControl_") + object_key);
      box->setChecked(current == QStringLiteral("true"));
      connect(box, &QCheckBox::toggled, this, [commit](bool checked) {
        commit(checked ? QStringLiteral("true") : QStringLiteral("false"));
      });
      control = box;
    } else if (kind == QStringLiteral("select")) {
      auto* combo = new QComboBox(row);
      combo->setObjectName(QStringLiteral("memoryControl_") + object_key);
      for (const QJsonValue& option :
           field.value(QStringLiteral("options")).toArray()) {
        const QJsonObject item = option.toObject();
        const QString option_value =
            item.value(QStringLiteral("value")).toString();
        const QString option_label =
            item.value(QStringLiteral("label")).toString();
        combo->addItem(option_label.isEmpty() ? option_value : option_label,
                       option_value);
      }
      const int index = combo->findData(current);
      combo->setCurrentIndex(index < 0 ? 0 : index);
      connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
              [combo, commit](int selected) {
                commit(combo->itemData(selected).toString());
              });
      control = combo;
    } else if (kind == QStringLiteral("number")) {
      auto* spin = new QDoubleSpinBox(row);
      spin->setObjectName(QStringLiteral("memoryControl_") + object_key);
      spin->setDecimals(2);
      spin->setRange(
          field.value(QStringLiteral("minimum")).toDouble(-1000000.0),
          field.value(QStringLiteral("maximum")).toDouble(1000000.0));
      spin->setSingleStep(field.value(QStringLiteral("step")).toDouble(1.0));
      spin->setValue(current.toDouble());
      connect(spin, &QDoubleSpinBox::editingFinished, this, [spin, commit]() {
        commit(QString::number(spin->value(), 'g', 12));
      });
      control = spin;
    } else {
      auto* edit = new QLineEdit(row);
      edit->setObjectName(QStringLiteral("memoryControl_") + object_key);
      edit->setPlaceholderText(
          kind == QStringLiteral("secret") &&
                  field.value(QStringLiteral("is_set")).toBool()
              ? QStringLiteral("Leave blank to keep current value")
              : placeholder);
      if (kind == QStringLiteral("secret")) {
        edit->setEchoMode(QLineEdit::Password);
      }
      edit->setText(current);
      connect(edit, &QLineEdit::editingFinished, this, [edit, kind, commit]() {
        if (kind == QStringLiteral("secret") && edit->text().isEmpty()) {
          return;
        }
        commit(edit->text());
      });
      control = edit;
    }
    control->setFixedWidth(300);
    outer->addWidget(control, 0, Qt::AlignTop);
    form_layout_->addWidget(row);
  }
}

void SettingsView::AppendAudioDeviceRows() {
  auto* heading = new QLabel(QStringLiteral("Native audio devices"), form_);
  heading->setObjectName(QStringLiteral("settingTitle"));
  form_layout_->addWidget(heading);
  auto* intro = new QLabel(
      QStringLiteral(
          "Choose the speakers used for read-aloud replies. The microphone "
          "choice is stored for local capture surfaces; gateway voice capture "
          "uses the gateway host's audio device."),
      form_);
  intro->setObjectName(QStringLiteral("settingDescription"));
  intro->setWordWrap(true);
  form_layout_->addWidget(intro);
  form_layout_->addWidget(BuildAudioDeviceRow(
      QStringLiteral("audio.output_device"), QStringLiteral("Output device"),
      QStringLiteral("Speakers or headphones for read-aloud playback."),
      false));
  form_layout_->addWidget(BuildAudioDeviceRow(
      QStringLiteral("audio.input_device"), QStringLiteral("Microphone"),
      QStringLiteral("Preferred microphone for native capture surfaces."),
      true));
}

namespace {

QWidget* ModelSettingRow(QWidget* parent, QVBoxLayout* form_layout,
                         const QString& title_text, const QString& description,
                         QWidget* control) {
  auto* row = new QWidget(parent);
  auto* outer = new QHBoxLayout(row);
  outer->setContentsMargins(0, 0, 0, 0);
  outer->setSpacing(16);
  auto* labels = new QVBoxLayout();
  labels->setSpacing(2);
  auto* title = new QLabel(title_text, row);
  title->setObjectName(QStringLiteral("settingTitle"));
  auto* detail = new QLabel(description, row);
  detail->setObjectName(QStringLiteral("settingDescription"));
  detail->setWordWrap(true);
  labels->addWidget(title);
  labels->addWidget(detail);
  outer->addLayout(labels, 1);
  control->setFixedWidth(300);
  outer->addWidget(control, 0, Qt::AlignTop);
  form_layout->addWidget(row);
  return row;
}

}  // namespace

QString AuxiliaryTaskLabel(const QString& task_id) {
  QString text = task_id;
  text.replace(QLatin1Char('_'), QLatin1Char(' '));
  if (!text.isEmpty()) {
    text[0] = text[0].toUpper();
  }
  return text;
}

QString DefaultModelProvider(const ProfileSnapshot& snapshot,
                             const QString& model) {
  if (!snapshot.provider.isEmpty()) {
    return snapshot.provider;
  }
  // The raw config can carry an alias (deepseek-v4.1-flash) while the catalog
  // lists the provider-qualified id. Try both, case-insensitively, before
  // giving up and leaving the provider combo empty.
  for (const QString& candidate : {model, snapshot.model}) {
    if (candidate.isEmpty()) {
      continue;
    }
    for (const ModelProvider& provider : snapshot.model_providers) {
      for (const QString& entry : provider.models) {
        if (entry.compare(candidate, Qt::CaseInsensitive) == 0) {
          return provider.slug;
        }
      }
    }
  }
  return QString();
}

void SettingsView::AppendModelMainRows() {
  // /api/config normalizes a dict-form model section to its bare string so the
  // legacy schema can edit it. Read the model from that string and recover the
  // provider from the catalog, otherwise both combos render empty.
  const QString model =
      ValueAt(QStringLiteral("model")).toString(snapshot_.model);
  const QString provider_slug = DefaultModelProvider(snapshot_, model);
  auto* provider_row = new QWidget(form_);
  auto* provider = new QComboBox(provider_row);
  provider->setObjectName(QStringLiteral("setting_model_provider"));
  provider->setEditable(true);
  provider->setInsertPolicy(QComboBox::NoInsert);
  for (const ModelProvider& entry : snapshot_.model_providers) {
    provider->addItem(entry.slug, entry.slug);
  }
  if (!provider_slug.isEmpty() && provider->findData(provider_slug) < 0) {
    provider->addItem(provider_slug, provider_slug);
  }
  provider->setCurrentText(provider_slug);

  auto* model_row = new QWidget(form_);
  auto* model_combo = new QComboBox(model_row);
  model_combo->setObjectName(QStringLiteral("setting_model_default"));
  model_combo->setEditable(true);
  model_combo->setInsertPolicy(QComboBox::NoInsert);
  const auto fill_models = [this, model_combo](const QString& slug,
                                               const QString& selected) {
    model_combo->blockSignals(true);
    model_combo->clear();
    for (const ModelProvider& entry : snapshot_.model_providers) {
      if (entry.slug != slug) {
        continue;
      }
      for (const QString& item : entry.models) {
        model_combo->addItem(item, item);
      }
      break;
    }
    if (!selected.isEmpty() && model_combo->findData(selected) < 0) {
      model_combo->addItem(selected, selected);
    }
    const int index = model_combo->findData(selected);
    if (index >= 0) {
      model_combo->setCurrentIndex(index);
    } else if (model_combo->count() > 0) {
      model_combo->setCurrentIndex(0);
    } else {
      model_combo->setCurrentText(selected);
    }
    model_combo->blockSignals(false);
  };
  fill_models(provider_slug, model);

  QObject::connect(provider, &QComboBox::currentTextChanged, this,
                   [this, model_combo, fill_models](const QString& text) {
                     if (building_) {
                       return;
                     }
                     QueueSave(QStringLiteral("model.provider"), text);
                     // A known provider switch starts from that provider's
                     // first model; typing a custom model afterwards still
                     // overrides it.
                     fill_models(text, QString());
                     if (!model_combo->currentText().isEmpty()) {
                       QueueSave(QStringLiteral("model.default"),
                                 model_combo->currentText());
                     }
                   });
  QObject::connect(model_combo, &QComboBox::currentTextChanged, this,
                   [this](const QString& text) {
                     if (!building_) {
                       QueueSave(QStringLiteral("model.default"), text);
                     }
                   });

  provider_row->setObjectName(QStringLiteral("setting_model_provider_row"));
  ModelSettingRow(provider_row, form_layout_, QStringLiteral("Provider"),
                  QStringLiteral("Provider used for the default chat model."),
                  provider);
  model_row->setObjectName(QStringLiteral("setting_model_default_row"));
  ModelSettingRow(model_row, form_layout_, QStringLiteral("Default model"),
                  QStringLiteral("Model selected when a chat starts."),
                  model_combo);

  auto* effort_row = new QWidget(form_);
  auto* effort = new QComboBox(effort_row);
  effort->setObjectName(QStringLiteral("setting_agent_reasoning_effort"));
  for (const QString& option :
       {QStringLiteral("minimal"), QStringLiteral("low"),
        QStringLiteral("medium"), QStringLiteral("high"),
        QStringLiteral("xhigh"), QStringLiteral("max"),
        QStringLiteral("ultra")}) {
    effort->addItem(option, option);
  }
  const QString current_effort =
      ValueAt(QStringLiteral("agent.reasoning_effort")).toString();
  if (!current_effort.isEmpty() && effort->findData(current_effort) < 0) {
    effort->addItem(current_effort, current_effort);
  }
  effort->setCurrentIndex(effort->findData(current_effort));
  QObject::connect(effort, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   this, [this, effort](int index) {
                     if (!building_ && index >= 0) {
                       QueueSave(QStringLiteral("agent.reasoning_effort"),
                                 effort->itemData(index).toString());
                     }
                   });
  effort_row->setObjectName(
      QStringLiteral("setting_agent_reasoning_effort_row"));
  ModelSettingRow(effort_row, form_layout_, QStringLiteral("Reasoning effort"),
                  QStringLiteral("How much internal reasoning the agent does "
                                 "before answering."),
                  effort);
}

void SettingsView::AppendAuxiliaryRows() {
  auto* heading = new QLabel(QStringLiteral("Auxiliary models"), form_);
  heading->setObjectName(QStringLiteral("settingTitle"));
  form_layout_->addWidget(heading);
  auto* intro = new QLabel(
      QStringLiteral("Models used for background jobs such as compression, "
                     "titles and memory. Saving one task leaves the other "
                     "tasks untouched."),
      form_);
  intro->setObjectName(QStringLiteral("settingDescription"));
  intro->setWordWrap(true);
  form_layout_->addWidget(intro);

  if (on_page_request &&
      !snapshot_.pages.contains(QStringLiteral("modelaux"))) {
    QTimer::singleShot(0, this, [this]() {
      if (active_id_ == QStringLiteral("model") &&
          subpage_by_section_.value(active_id_) ==
              QStringLiteral("auxiliary") &&
          on_page_request &&
          !snapshot_.pages.contains(QStringLiteral("modelaux"))) {
        on_page_request(QStringLiteral("modelaux"));
      }
    });
  }
  const PageState state = snapshot_.pages.value(QStringLiteral("modelaux"));
  if (!state.error.isEmpty()) {
    auto* error = new QLabel(state.error, form_);
    error->setObjectName(QStringLiteral("settingDescription"));
    error->setWordWrap(true);
    form_layout_->addWidget(error);
  }
  if (!state.loaded && (state.loading || on_page_request)) {
    auto* loading =
        new QLabel(QStringLiteral("Loading auxiliary models…"), form_);
    loading->setObjectName(QStringLiteral("settingDescription"));
    form_layout_->addWidget(loading);
    return;
  }
  const QJsonObject root =
      state.data.value(QStringLiteral("auxiliary")).toObject();
  // The route returns {"tasks": [...], "main": {...}}; FetchPage stores that
  // response under the "auxiliary" source key, so do not unwrap a second
  // "auxiliary" object here.
  const QJsonArray tasks = root.value(QStringLiteral("tasks")).toArray();
  const QString profile_query =
      snapshot_.name.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(
                    QUrl::toPercentEncoding(snapshot_.name)));
  if (tasks.isEmpty()) {
    auto* empty =
        new QLabel(QStringLiteral("No auxiliary tasks were reported."), form_);
    empty->setObjectName(QStringLiteral("settingDescription"));
    form_layout_->addWidget(empty);
    return;
  }

  for (const QJsonValue& value : tasks) {
    const QJsonObject task = value.toObject();
    const QString task_id = task.value(QStringLiteral("task")).toString();
    QString label = task.value(QStringLiteral("label")).toString();
    if (label.isEmpty() || label == task_id) {
      label = AuxiliaryTaskLabel(task_id);
    }
    const QString hint = task.value(QStringLiteral("hint")).toString();
    auto* card = new QWidget(form_);
    card->setObjectName(QStringLiteral("auxiliaryRow_") + task_id);
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(0, 6, 0, 6);
    auto* title = new QLabel(label, card);
    title->setObjectName(QStringLiteral("settingTitle"));
    card_layout->addWidget(title);
    if (!hint.isEmpty()) {
      auto* detail = new QLabel(hint, card);
      detail->setObjectName(QStringLiteral("settingDescription"));
      detail->setWordWrap(true);
      card_layout->addWidget(detail);
    }
    auto* controls = new QHBoxLayout();
    auto* provider = new QComboBox(card);
    provider->setObjectName(QStringLiteral("auxiliaryProvider_") + task_id);
    for (const ModelProvider& entry : snapshot_.model_providers) {
      provider->addItem(entry.slug, entry.slug);
    }
    const QString current_provider =
        task.value(QStringLiteral("provider"))
            .toString(root.value(QStringLiteral("main"))
                          .toObject()
                          .value(QStringLiteral("provider"))
                          .toString());
    if (!current_provider.isEmpty() &&
        provider->findData(current_provider) < 0) {
      provider->addItem(current_provider, current_provider);
    }
    provider->setCurrentIndex(provider->findData(current_provider));
    controls->addWidget(provider);
    auto* model = new QComboBox(card);
    model->setObjectName(QStringLiteral("auxiliaryModel_") + task_id);
    const auto fill = [this, model](const QString& slug,
                                    const QString& selected) {
      model->clear();
      for (const ModelProvider& entry : snapshot_.model_providers) {
        if (entry.slug != slug) {
          continue;
        }
        for (const QString& item : entry.models) {
          model->addItem(item, item);
        }
        break;
      }
      if (!selected.isEmpty() && model->findData(selected) < 0) {
        model->addItem(selected, selected);
      }
      model->setCurrentIndex(model->findData(selected));
      if (model->currentIndex() < 0 && model->count() > 0) {
        model->setCurrentIndex(0);
      }
    };
    const QString current_model =
        task.value(QStringLiteral("model"))
            .toString(root.value(QStringLiteral("main"))
                          .toObject()
                          .value(QStringLiteral("model"))
                          .toString());
    fill(current_provider, current_model);
    QObject::connect(provider, &QComboBox::currentTextChanged, this,
                     [model, fill, provider](const QString&) {
                       fill(provider->currentData().toString(),
                            model->currentText());
                     });
    controls->addWidget(model);
    auto* save = new QPushButton(QStringLiteral("Save"), card);
    save->setObjectName(QStringLiteral("auxiliarySave_") + task_id);
    controls->addWidget(save);
    controls->addStretch(1);
    card_layout->addLayout(controls);
    form_layout_->addWidget(card);
    QObject::connect(save, &QPushButton::clicked, this,
                     [this, task_id, provider, model, profile_query]() {
                       if (on_page_action) {
                         on_page_action(
                             QStringLiteral("modelaux"), QStringLiteral("POST"),
                             QStringLiteral("/api/model/set") + profile_query,
                             QJsonObject{{QStringLiteral("scope"),
                                          QStringLiteral("auxiliary")},
                                         {QStringLiteral("task"), task_id},
                                         {QStringLiteral("provider"),
                                          provider->currentData().toString()},
                                         {QStringLiteral("model"),
                                          model->currentData().toString()}});
                       }
                     });
  }
}

void SettingsView::AppendFallbackRows(const QJsonValue& value) {
  auto* heading = new QLabel(QStringLiteral("Fallback models"), form_);
  heading->setObjectName(QStringLiteral("settingTitle"));
  form_layout_->addWidget(heading);
  auto* intro = new QLabel(
      QStringLiteral("Tried in order when the default model fails. Each row "
                     "keeps any extra backend fields, such as a custom base "
                     "URL. Leave reasoning blank to inherit the main effort."),
      form_);
  intro->setObjectName(QStringLiteral("settingDescription"));
  intro->setWordWrap(true);
  form_layout_->addWidget(intro);

  const QJsonArray previous = value.toArray();
  auto* legacy = new QLineEdit(form_);
  legacy->setObjectName(QStringLiteral("setting_fallback_providers"));
  legacy->setText(FormatFallbackChain(previous));
  legacy->hide();
  QObject::connect(legacy, &QLineEdit::editingFinished, this,
                   [this, legacy, previous]() {
                     QueueSave(QStringLiteral("fallback_providers"),
                               ParseFallbackChain(legacy->text(), previous));
                   });

  struct Row {
    QWidget* card = nullptr;
    QComboBox* provider = nullptr;
    QComboBox* model = nullptr;
    QComboBox* effort = nullptr;
    QJsonObject original;
  };
  auto rows = std::make_shared<std::vector<Row>>();
  const auto chain = [rows]() {
    QJsonArray result;
    for (const Row& row : *rows) {
      QJsonObject entry = row.original;
      entry.insert(QStringLiteral("provider"),
                   row.provider->currentText().trimmed());
      entry.insert(QStringLiteral("model"), row.model->currentText().trimmed());
      const QString effort = row.effort->currentData().toString();
      if (effort.isEmpty()) {
        entry.remove(QStringLiteral("reasoning_effort"));
      } else {
        entry.insert(QStringLiteral("reasoning_effort"), effort);
      }
      result.append(entry);
    }
    return result;
  };
  const auto save = [this, chain, legacy]() {
    if (building_) {
      return;
    }
    const QJsonArray next = chain();
    legacy->setText(FormatFallbackChain(next));
    QueueSave(QStringLiteral("fallback_providers"), next);
  };
  const auto fill_models = [this](QComboBox* model, const QString& slug,
                                  const QString& selected) {
    model->blockSignals(true);
    model->clear();
    for (const ModelProvider& entry : snapshot_.model_providers) {
      if (entry.slug != slug) {
        continue;
      }
      for (const QString& item : entry.models) {
        model->addItem(item, item);
      }
      break;
    }
    if (!selected.isEmpty() && model->findData(selected) < 0) {
      model->addItem(selected, selected);
    }
    model->setCurrentText(selected);
    model->blockSignals(false);
  };

  for (int index = 0; index < previous.size(); ++index) {
    const QJsonObject entry = previous.at(index).toObject();
    Row row;
    row.original = entry;
    row.card = new QWidget(form_);
    row.card->setObjectName(QStringLiteral("fallbackRow_%1").arg(index));
    auto* layout = new QHBoxLayout(row.card);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(6);
    row.provider = new QComboBox(row.card);
    row.provider->setObjectName(
        QStringLiteral("fallbackProvider_%1").arg(index));
    row.provider->setEditable(true);
    row.provider->setInsertPolicy(QComboBox::NoInsert);
    for (const ModelProvider& provider : snapshot_.model_providers) {
      row.provider->addItem(provider.slug, provider.slug);
    }
    row.provider->setCurrentText(
        entry.value(QStringLiteral("provider")).toString());
    layout->addWidget(row.provider);

    row.model = new QComboBox(row.card);
    row.model->setObjectName(QStringLiteral("fallbackModel_%1").arg(index));
    row.model->setEditable(true);
    row.model->setInsertPolicy(QComboBox::NoInsert);
    fill_models(row.model, row.provider->currentText(),
                entry.value(QStringLiteral("model")).toString());
    layout->addWidget(row.model);

    row.effort = new QComboBox(row.card);
    row.effort->setObjectName(QStringLiteral("fallbackEffort_%1").arg(index));
    row.effort->addItem(QStringLiteral("inherit"), QString());
    for (const QString& option :
         {QStringLiteral("minimal"), QStringLiteral("low"),
          QStringLiteral("medium"), QStringLiteral("high"),
          QStringLiteral("xhigh"), QStringLiteral("max"),
          QStringLiteral("ultra")}) {
      row.effort->addItem(option, option);
    }
    const QString effort =
        entry.value(QStringLiteral("reasoning_effort")).toString();
    const int effort_index = row.effort->findData(effort);
    row.effort->setCurrentIndex(effort_index >= 0 ? effort_index : 0);
    layout->addWidget(row.effort);

    auto* remove = new QPushButton(QStringLiteral("Remove"), row.card);
    remove->setObjectName(QStringLiteral("fallbackRemove_%1").arg(index));
    layout->addWidget(remove);
    form_layout_->addWidget(row.card);
    rows->push_back(row);

    QObject::connect(row.provider, &QComboBox::currentTextChanged, this,
                     [row, fill_models, save](const QString& text) {
                       fill_models(row.model, text, row.model->currentText());
                       save();
                     });
    QObject::connect(row.model, &QComboBox::currentTextChanged, this,
                     [save](const QString&) { save(); });
    QObject::connect(row.effort,
                     QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     [save](int) { save(); });
    QObject::connect(remove, &QPushButton::clicked, this,
                     [this, chain, index]() {
                       QJsonArray next = chain();
                       next.removeAt(index);
                       QueueSave(QStringLiteral("fallback_providers"), next);
                       QTimer::singleShot(0, this, [this]() {
                         if (active_id_ == QStringLiteral("model") &&
                             subpage_by_section_.value(active_id_) ==
                                 QStringLiteral("fallbacks")) {
                           RebuildForm();
                         }
                       });
                     });
  }

  auto* add = new QPushButton(QStringLiteral("Add fallback"), form_);
  add->setObjectName(QStringLiteral("fallbackAdd"));
  QObject::connect(add, &QPushButton::clicked, this, [this, chain]() {
    QJsonArray next = chain();
    QJsonObject entry{
        {QStringLiteral("provider"),
         DefaultModelProvider(snapshot_, snapshot_.model)},
        {QStringLiteral("model"),
         ValueAt(QStringLiteral("model")).toString(snapshot_.model)}};
    next.append(entry);
    QueueSave(QStringLiteral("fallback_providers"), next);
    QTimer::singleShot(0, this, [this]() {
      if (active_id_ == QStringLiteral("model") &&
          subpage_by_section_.value(active_id_) ==
              QStringLiteral("fallbacks")) {
        RebuildForm();
      }
    });
  });
  form_layout_->addWidget(add);
}

void SettingsView::AppendMoaRows() {
  auto* heading = new QLabel(QStringLiteral("Mixture of Agents"), form_);
  heading->setObjectName(QStringLiteral("settingTitle"));
  form_layout_->addWidget(heading);
  auto* intro = new QLabel(
      QStringLiteral("MoA presets combine several reference models and an "
                     "aggregator. Configure presets with `hermes moa`; this "
                     "page shows the presets the gateway currently serves."),
      form_);
  intro->setObjectName(QStringLiteral("settingDescription"));
  intro->setWordWrap(true);
  form_layout_->addWidget(intro);

  if (on_page_request &&
      !snapshot_.pages.contains(QStringLiteral("modelmoa"))) {
    QTimer::singleShot(0, this, [this]() {
      if (active_id_ == QStringLiteral("model") &&
          subpage_by_section_.value(active_id_) == QStringLiteral("moa") &&
          on_page_request &&
          !snapshot_.pages.contains(QStringLiteral("modelmoa"))) {
        on_page_request(QStringLiteral("modelmoa"));
      }
    });
  }
  const PageState state = snapshot_.pages.value(QStringLiteral("modelmoa"));
  if (!state.error.isEmpty()) {
    auto* error = new QLabel(state.error, form_);
    error->setObjectName(QStringLiteral("settingDescription"));
    error->setWordWrap(true);
    form_layout_->addWidget(error);
  }
  if (!state.loaded && (state.loading || on_page_request)) {
    auto* loading = new QLabel(QStringLiteral("Loading MoA presets…"), form_);
    loading->setObjectName(QStringLiteral("settingDescription"));
    form_layout_->addWidget(loading);
    return;
  }
  const QJsonObject root = state.data.value(QStringLiteral("moa")).toObject();
  const QString default_preset =
      root.value(QStringLiteral("default_preset")).toString();
  const QString active_preset =
      root.value(QStringLiteral("active_preset")).toString();
  const QJsonObject presets = root.value(QStringLiteral("presets")).toObject();
  auto* summary = new QLabel(
      QStringLiteral("Default: %1 · Active: %2")
          .arg(default_preset.isEmpty() ? QStringLiteral("none")
                                        : default_preset,
               active_preset.isEmpty() ? QStringLiteral("default preset")
                                       : active_preset),
      form_);
  summary->setObjectName(QStringLiteral("settingDescription"));
  form_layout_->addWidget(summary);
  if (presets.isEmpty()) {
    auto* empty =
        new QLabel(QStringLiteral("No MoA presets configured."), form_);
    empty->setObjectName(QStringLiteral("settingDescription"));
    form_layout_->addWidget(empty);
    return;
  }
  for (auto it = presets.begin(); it != presets.end(); ++it) {
    const QJsonObject preset = it.value().toObject();
    auto* card = new QWidget(form_);
    card->setObjectName(QStringLiteral("moaPreset_") + it.key());
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 6, 0, 6);
    auto* title = new QLabel(QStringLiteral("Preset: %1").arg(it.key()), card);
    title->setObjectName(QStringLiteral("settingTitle"));
    layout->addWidget(title);
    const QJsonArray references =
        preset.value(QStringLiteral("reference_models")).toArray();
    QStringList reference_labels;
    for (const QJsonValue& reference : references) {
      const QJsonObject slot = reference.toObject();
      reference_labels.append(QStringLiteral("%1/%2").arg(
          slot.value(QStringLiteral("provider")).toString(),
          slot.value(QStringLiteral("model")).toString()));
    }
    const QJsonObject aggregator =
        preset.value(QStringLiteral("aggregator")).toObject();
    auto* detail = new QLabel(
        QStringLiteral("References: %1\nAggregator: %2/%3")
            .arg(reference_labels.isEmpty()
                     ? QStringLiteral("none")
                     : reference_labels.join(QStringLiteral(", ")),
                 aggregator.value(QStringLiteral("provider")).toString(),
                 aggregator.value(QStringLiteral("model")).toString()),
        card);
    detail->setObjectName(QStringLiteral("settingDescription"));
    detail->setWordWrap(true);
    layout->addWidget(detail);
    form_layout_->addWidget(card);
  }
}

QJsonValue SettingsView::ValueAt(const QString& key) const {
  return ValueAtPath(snapshot_.config, key);
}

void SettingsView::QueueSave(const QString& key, const QJsonValue& value) {
  if (building_) {
    return;
  }
  const QStringList parts = key.split(QLatin1Char('.'));
  MergePath(&patch_, parts, 0, value);
  // Mirror the draft locally so dependent rows can update before the
  // debounced save round-trips through /api/config.
  MergePath(&snapshot_.config, parts, 0, value);
  if (active_id_ == QStringLiteral("voice")) {
    UpdateVoiceFieldVisibility();
  }
  SetStatus(QStringLiteral("Saving..."));
  save_timer_->start();
}

void SettingsView::FlushSave() {
  if (patch_.isEmpty() || !on_save) {
    return;
  }
  const QJsonObject patch = patch_;
  patch_ = QJsonObject();
  on_save(patch);
}

void SettingsView::ShowPlaceholder(const QString& title, const QString& body) {
  building_ = true;
  ClearForm();
  auto* heading = new QLabel(title, form_);
  heading->setObjectName(QStringLiteral("settingsTitle"));
  auto* text = new QLabel(body, form_);
  text->setObjectName(QStringLiteral("settingDescription"));
  text->setWordWrap(true);
  form_layout_->addWidget(heading);
  form_layout_->addWidget(text);
  form_layout_->addStretch(1);
  building_ = false;
}

void SettingsView::SetStatus(const QString& text, bool error) {
  if (status_ == nullptr) {
    return;
  }
  status_->setText(text);
  status_->setStyleSheet(QStringLiteral("color: %1;")
                             .arg(error ? QStringLiteral("#c0392b")
                                        : theme_.text_secondary.name()));
}

QString SettingsView::PrettyKey(const QString& key) {
  QString last = key.section(QLatin1Char('.'), -1);
  last.replace(QLatin1Char('_'), QLatin1Char(' '));
  if (!last.isEmpty()) {
    last[0] = last.at(0).toUpper();
  }
  return last;
}

void SettingsView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), theme_.background);
}

void SettingsView::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
}

}  // namespace hermes::ui
