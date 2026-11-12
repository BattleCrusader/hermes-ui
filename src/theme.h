// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_THEME_H_
#define HERMES_UI_THEME_H_

#include <QColor>
#include <QFont>
#include <QString>

namespace hermes::ui {

// Light or dark appearance. The spike starts in the mode the OS reports.
enum class ThemeMode {
  kLight,
  kDark,
};

// Design tokens ported from apps/desktop/src/styles.css. The real app derives
// most surfaces from a seed color through color-mix(); the spike keeps the
// resolved values explicit so a port can be diffed against the CSS.
struct Theme {
  QColor background;
  QColor sidebar;
  QColor surface;
  QColor surface_raised;
  QColor foreground;
  QColor text_secondary;
  QColor text_tertiary;
  QColor stroke;
  QColor stroke_strong;
  QColor accent;
  QColor accent_soft;
  QColor accent_text;
  QColor user_bubble;
  QColor hover;
  QColor selected;
  QColor code_background;
  QColor code_header;
  QColor code_text;
  QColor code_comment;
  QColor code_keyword;
  QColor code_string;
  QColor code_number;
  QColor success;
  QColor warning;
  QColor danger;

  int radius_small = 4;
  int radius_medium = 6;
  int radius_large = 12;
  int title_bar_height = 34;
  int sidebar_width = 237;

  // Appearance → font scale. UiFont/MonoFont multiply their pixel size by
  // this so a preference change reflows every custom-painted surface.
  double font_scale = 1.0;

  QString ui_family;
  QString mono_family;
  QString icon_family;

  QFont UiFont(int pixel_size, QFont::Weight weight) const;
  QFont MonoFont(int pixel_size, QFont::Weight weight) const;
  QFont IconFont(int pixel_size) const;

  static Theme Light();
  static Theme Dark();
};

// Mixes two colors in sRGB, matching CSS color-mix(in srgb, ...).
QColor Mix(const QColor& first, const QColor& second, double amount);

// WCAG relative-luminance contrast ratio. Code is small text, so theme code
// colours are held to these ratios against the code card background.
double ContrastRatio(const QColor& first, const QColor& second);

// Moves `color` toward `fallback` (or pure white/black when the fallback is
// also too dim) until it reaches `min_ratio` against `background`. Returns the
// original colour when it already passes.
QColor EnsureContrast(const QColor& color, const QColor& background,
                      const QColor& fallback, double min_ratio);

// Applies the code minimums in place: 7:1 for body text and 4.5:1 for
// comments/syntax tokens. Presets reuse this so a low-contrast palette cannot
// make a code card unreadable.
void EnsureCodeContrast(Theme* theme);

// Returns up to two uppercase initials for a profile name.
QString Initials(const QString& name);

}  // namespace hermes::ui

#endif  // HERMES_UI_THEME_H_
