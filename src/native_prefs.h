// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_NATIVE_PREFS_H_
#define HERMES_UI_NATIVE_PREFS_H_

#include <QString>
#include <QStringList>

namespace hermes::ui {

// Window preferences (theme, font scale, keybinds, notification toggles) live
// in QSettings, not the gateway config: the desktop keeps them in renderer
// local storage. HERMES_UI_PREFS redirects the store to an INI directory so
// tests and portable runs never touch the user's real domain.
QString NativePref(const QString& key);
void SetNativePref(const QString& key, const QString& value);
QStringList NativePrefKeys();

}  // namespace hermes::ui

#endif  // HERMES_UI_NATIVE_PREFS_H_
