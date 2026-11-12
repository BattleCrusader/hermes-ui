// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_APPEARANCE_PREFS_H_
#define HERMES_UI_APPEARANCE_PREFS_H_

#include <QColor>
#include <QString>
#include <algorithm>
#include <functional>

#include "src/theme.h"

namespace hermes::ui {

// Session rows are custom painted, so density changes their height and the
// amount of context under the title rather than a widget style.
enum class SessionListDensity {
  kCompact,
  kComfortable,
  kDetailed,
};

// Native mirror of the desktop Appearance page. Values live in QSettings
// through NativePref; this struct only parses and defaults them so the settings
// page and the shell agree on the same clamp rules.
struct AppearancePreferences {
  double ui_scale = 1.0;
  double chat_text_scale = 1.0;
  int message_width = 720;
  bool show_timestamps = true;
  bool reasoning_collapsed = true;
  SessionListDensity session_density = SessionListDensity::kCompact;
  QString ui_font;
  QString mono_font;
  int user_bubble_transparency = 0;
  int window_opacity = 100;
  bool animations = true;
  double code_scale = 1.0;
};

inline bool AppearanceBool(const QString& value, bool fallback) {
  if (value.isEmpty()) {
    return fallback;
  }
  const QString normalized = value.trimmed().toLower();
  if (normalized == QStringLiteral("1") ||
      normalized == QStringLiteral("true") ||
      normalized == QStringLiteral("on") ||
      normalized == QStringLiteral("yes")) {
    return true;
  }
  if (normalized == QStringLiteral("0") ||
      normalized == QStringLiteral("false") ||
      normalized == QStringLiteral("off") ||
      normalized == QStringLiteral("no")) {
    return false;
  }
  return fallback;
}

inline double AppearanceClamp(double value, double low, double high) {
  return std::clamp(value, low, high);
}

inline SessionListDensity ParseSessionListDensity(const QString& value) {
  if (value == QStringLiteral("comfortable")) {
    return SessionListDensity::kComfortable;
  }
  if (value == QStringLiteral("detailed")) {
    return SessionListDensity::kDetailed;
  }
  return SessionListDensity::kCompact;
}

inline QString SessionListDensityId(SessionListDensity density) {
  switch (density) {
    case SessionListDensity::kComfortable:
      return QStringLiteral("comfortable");
    case SessionListDensity::kDetailed:
      return QStringLiteral("detailed");
    case SessionListDensity::kCompact:
    default:
      return QStringLiteral("compact");
  }
}

inline AppearancePreferences LoadAppearancePreferences(
    const std::function<QString(const QString&)>& get_pref) {
  AppearancePreferences prefs;
  if (!get_pref) {
    return prefs;
  }
  const auto read = [&](const char* key) {
    return get_pref(QLatin1String(key));
  };

  // `native.font_scale` predates the Appearance page; keep reading it so an
  // existing install keeps the scale it saved.
  const double ui_scale = read("native.font_scale").toDouble();
  if (ui_scale > 0.0) {
    prefs.ui_scale = AppearanceClamp(ui_scale, 0.8, 1.6);
  }
  const double chat_scale = read("appearance.chat_text_scale").toDouble();
  if (chat_scale > 0.0) {
    prefs.chat_text_scale = AppearanceClamp(chat_scale, 0.8, 1.4);
  }
  const int message_width = read("appearance.message_width").toInt();
  if (message_width > 0) {
    prefs.message_width = std::clamp(message_width, 520, 1200);
  }
  prefs.show_timestamps =
      AppearanceBool(read("appearance.show_timestamps"), true);
  prefs.reasoning_collapsed =
      AppearanceBool(read("appearance.reasoning_collapsed"), true);
  prefs.session_density =
      ParseSessionListDensity(read("appearance.session_density"));
  prefs.ui_font = read("appearance.ui_font");
  prefs.mono_font = read("appearance.mono_font");
  const int bubble = read("appearance.user_bubble_transparency").toInt();
  prefs.user_bubble_transparency = std::clamp(bubble, 0, 80);
  const int opacity = read("appearance.window_opacity").toInt();
  if (opacity > 0) {
    prefs.window_opacity = std::clamp(opacity, 60, 100);
  }
  prefs.animations = AppearanceBool(read("appearance.animations"), true);
  const double code_scale = read("appearance.code_scale").toDouble();
  if (code_scale > 0.0) {
    prefs.code_scale = AppearanceClamp(code_scale, 0.8, 1.5);
  }
  return prefs;
}

// Applies the colour/font-only parts of the preference set to a theme. The
// caller owns the base user-bubble colour so changing transparency back to 0
// restores the exact preset colour instead of compounding alpha.
inline void ApplyAppearanceToTheme(const AppearancePreferences& prefs,
                                   const QColor& base_user_bubble,
                                   const QString& default_ui_font,
                                   const QString& default_mono_font,
                                   Theme* theme) {
  if (theme == nullptr) {
    return;
  }
  theme->font_scale = prefs.ui_scale;
  theme->ui_family = prefs.ui_font.trimmed().isEmpty()
                         ? default_ui_font
                         : prefs.ui_font.trimmed();
  theme->mono_family = prefs.mono_font.trimmed().isEmpty()
                           ? default_mono_font
                           : prefs.mono_font.trimmed();
  QColor bubble = base_user_bubble;
  // Preserve the preset's own alpha at 0% transparency; applying the
  // transparency to the base instead of replacing it keeps the restore exact.
  const double alpha = base_user_bubble.alphaF() *
                       (1.0 - prefs.user_bubble_transparency / 100.0);
  bubble.setAlphaF(std::clamp(alpha, 0.2, 1.0));
  theme->user_bubble = bubble;
}

}  // namespace hermes::ui

#endif  // HERMES_UI_APPEARANCE_PREFS_H_
