// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_WIDGET_THEME_H_
#define HERMES_UI_WIDGET_THEME_H_

#include <QString>

#include "src/theme.h"

namespace hermes::ui {

// QSS for stock Qt controls. Custom-painted surfaces (title bar, transcript,
// sidebar rows) still draw themselves; this closes the gap where QLineEdit,
// QComboBox, QCheckBox, scrollbars and menus fell back to the macOS palette.
QString GlobalStyleSheet(const Theme &theme);

// Applies the stylesheet and the UI font to QApplication. Call on every theme
// change so dialogs and popups created later inherit the same tokens.
void ApplyGlobalTheme(const Theme &theme);

} // namespace hermes::ui

#endif // HERMES_UI_WIDGET_THEME_H_
