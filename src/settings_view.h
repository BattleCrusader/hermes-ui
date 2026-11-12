// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SETTINGS_VIEW_H_
#define HERMES_UI_SETTINGS_VIEW_H_

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QWidget>
#include <functional>
#include <vector>

#include "src/model.h"
#include "src/theme.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace hermes::ui {

// Native Settings route. This is the first real port of the desktop
// SettingsView: a left rail of config sections and a schema-driven form on the
// right. The field set is generated from the desktop's own SECTIONS table
// (settings_sections.inc), so adding a backend config key does not require a
// new widget by hand. Saves are sparse PUT /api/config patches, matching the
// desktop's autosave contract.
class SettingsView : public QWidget {
  Q_OBJECT

 public:
  explicit SettingsView(Theme theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetSnapshot(const ProfileSnapshot& snapshot);
  void Open();
  // Opens the route on a named page, for deep links and probes.
  void OpenPage(const QString& id);
  bool IsOpen() const;
  // Re-applies keybinds and font scale from the window's own preferences, so
  // a restart behaves like the last session without waiting for Settings.
  void ApplyStoredPreferences();

  std::function<void(const QJsonObject&)> on_save;
  std::function<void()> on_refetch;
  std::function<void(ThemeMode)> on_theme;
  std::function<void(const QString& preset_id)> on_theme_preset;
  std::function<void()> on_close;
  std::function<void(const QString&)> on_select_session;
  // Data pages read REST routes the config form knows nothing about; the view
  // asks the worker for one page at a time and renders PageState from the
  // snapshot.
  std::function<void(const QString& page)> on_page_request;
  std::function<void(const QString& page, const QString& method,
                     const QString& path, const QJsonObject& body)>
      on_page_action;
  std::function<void(double scale)> on_font_scale;
  // Appearance has several coupled preferences (density, message width,
  // timestamps, fonts, transparency). One callback lets the shell reload them
  // together instead of repainting once per key.
  std::function<void()> on_appearance;
  // Native window preferences that need an immediate side effect outside
  // Settings (for example switching the read-aloud audio output device).
  std::function<void(const QString& key, const QString& value)> on_pref_changed;
  std::function<void(const QHash<QString, QString>& overrides)> on_keybinds;
  std::function<bool(const QString& title, const QString& body, bool silent,
                     QString* error)>
      on_send_notification;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  struct SectionSpec {
    QString id;
    QString label;
    std::vector<QString> keys;
  };

  void BuildUi();
  void BuildNav();
  void ShowSection(const QString& id, bool request_data);
  void RebuildForm();
  void ScheduleFormScrollRestore(
      int outer_value, const std::vector<std::pair<QString, int>>& inner_values,
      quint64 generation);
  QString DataPageSignature(const QString& id) const;
  void ShowPlaceholder(const QString& title, const QString& body);
  void ClearForm();
  QWidget* BuildField(const QString& key, const QJsonValue& value);
  void UpdateVoiceFieldVisibility();
  void AppendMemoryRows();
  void AppendAudioDeviceRows();
  QWidget* BuildAudioDeviceRow(const QString& key, const QString& label,
                               const QString& description, bool input);
  QJsonValue ValueAt(const QString& key) const;
  void QueueSave(const QString& key, const QJsonValue& value);
  void FlushSave();
  void SetStatus(const QString& text, bool error = false);
  void PublishPreferences();
  void SetPref(const QString& key, const QString& value);
  static std::vector<SectionSpec> Sections();
  static QString PrettyKey(const QString& key);

  Theme theme_;
  ProfileSnapshot snapshot_;
  QString config_hash_;
  QString active_id_ = QStringLiteral("model");
  int last_revision_ = -1;
  quint64 form_scroll_generation_ = 0;
  QString last_built_form_id_;
  QString active_page_signature_;
  bool building_ = false;

  QLabel* title_ = nullptr;
  QLineEdit* search_ = nullptr;
  QLabel* status_ = nullptr;
  QListWidget* nav_ = nullptr;
  QScrollArea* scroll_ = nullptr;
  QWidget* form_ = nullptr;
  QVBoxLayout* form_layout_ = nullptr;
  QTimer* save_timer_ = nullptr;
  QJsonObject patch_;
  double last_font_scale_ = 0.0;
  QString last_keybinds_hash_;
  QString memory_config_requested_;
  QHash<QString, QString> subpage_by_section_;
  QString pending_field_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SETTINGS_VIEW_H_
