// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/native_prefs.h"

#include <QDir>
#include <QSettings>

namespace hermes::ui {
namespace {

// One QSettings for the window's preferences. HERMES_UI_PREFS points it at an
// explicit INI file so tests and portable runs never touch the user's real
// com.hermes.hermes-ui domain; without it the platform-native store is used.
QSettings &Settings() {
  static QSettings *settings = []() {
    const QString override = qEnvironmentVariable("HERMES_UI_PREFS");
    if (override.isEmpty() || !QDir().mkpath(override)) {
      return new QSettings(QStringLiteral("Hermes"),
                           QStringLiteral("hermes-ui"));
    }
    return new QSettings(
        QDir(override).filePath(QStringLiteral("hermes-ui.ini")),
        QSettings::IniFormat);
  }();
  return *settings;
}

} // namespace

QString NativePref(const QString &key) {
  return Settings().value(key).toString();
}

void SetNativePref(const QString &key, const QString &value) {
  Settings().setValue(key, value);
  Settings().sync();
}

QStringList NativePrefKeys() { return Settings().allKeys(); }

} // namespace hermes::ui
