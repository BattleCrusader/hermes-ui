// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/widget_theme.h"

#include <QApplication>

namespace hermes::ui {
namespace {

// QSS accepts rgba(r,g,b,a) but not QColor::name() when the token carries
// alpha (stroke/hover/selected are translucent by design).
QString Css(const QColor &color) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

} // namespace

QString GlobalStyleSheet(const Theme &theme) {
  const QString fg = Css(theme.foreground);
  const QString secondary = Css(theme.text_secondary);
  const QString tertiary = Css(theme.text_tertiary);
  const QString surface = Css(theme.surface);
  const QString background = Css(theme.background);
  const QString stroke = Css(theme.stroke);
  const QString stroke_strong = Css(theme.stroke_strong);
  const QString accent = Css(theme.accent);
  const QString selected = Css(theme.selected);
  const QString hover = Css(theme.hover);
  const QString radius = QString::number(theme.radius_large);
  const bool dark = theme.background.lightness() < 128;
  const QString arrow_up = dark ? QStringLiteral(":/arrow-up-dark.svg")
                                : QStringLiteral(":/arrow-up-light.svg");
  const QString arrow_down = dark ? QStringLiteral(":/arrow-down-dark.svg")
                                  : QStringLiteral(":/arrow-down-light.svg");
  const QString chevron = dark ? QStringLiteral(":/chevron-down-dark.svg")
                               : QStringLiteral(":/chevron-down-light.svg");

  return QStringLiteral(
             "QWidget { color: %1; font-family: '%2'; }"
             "QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox, QTextEdit, "
             "QPlainTextEdit {"
             " background: %3; color: %1; border: 1px solid %4;"
             " border-radius: 8px; padding: 5px 8px;"
             " selection-background-color: %5; selection-color: white; }"
             "QLineEdit:focus, QComboBox:focus, QSpinBox:focus, "
             "QDoubleSpinBox:focus, QTextEdit:focus, QPlainTextEdit:focus {"
             " border: 1px solid %5; }"
             "QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, "
             "QDoubleSpinBox:disabled { color: %6; background: %7; }"
             "QComboBox::drop-down {"
             " subcontrol-origin: padding; subcontrol-position: top right;"
             " width: 22px; border-left: 1px solid %4;"
             " border-top-right-radius: 8px; border-bottom-right-radius: 8px;"
             " background: transparent; }"
             "QComboBox::down-arrow {"
             " image: url(%12); width: 10px; height: 6px; }"
             "QSpinBox::up-button, QDoubleSpinBox::up-button {"
             " subcontrol-origin: border; subcontrol-position: top right;"
             " width: 18px; border-left: 1px solid %4;"
             " border-top-right-radius: 8px; background: transparent; }"
             "QSpinBox::down-button, QDoubleSpinBox::down-button {"
             " subcontrol-origin: border; subcontrol-position: bottom right;"
             " width: 18px; border-left: 1px solid %4;"
             " border-bottom-right-radius: 8px; background: transparent; }"
             "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
             " image: url(%13); width: 10px; height: 6px; }"
             "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
             " image: url(%14); width: 10px; height: 6px; }"
             "QComboBox QAbstractItemView {"
             " background: %3; color: %1; border: 1px solid %4;"
             " border-radius: 8px; padding: 4px;"
             " selection-background-color: %8; selection-color: %1; }"
             "QCheckBox { color: %1; spacing: 8px; }"
             "QCheckBox::indicator {"
             " width: 16px; height: 16px; border: 1px solid %9;"
             " border-radius: 4px; background: %3; }"
             "QCheckBox::indicator:hover { border-color: %5; }"
             "QCheckBox::indicator:checked {"
             " background: %5; border-color: %5; image: url(:/check.svg); }"
             "QPushButton {"
             " background: %3; color: %1; border: 1px solid %4;"
             " border-radius: 8px; padding: 6px 12px; }"
             "QPushButton:hover { background: %8; }"
             "QPushButton:pressed { background: %10; }"
             "QMenu {"
             " background: %3; color: %1; border: 1px solid %4;"
             " border-radius: 8px; padding: 4px; }"
             "QMenu::item { padding: 6px 20px; border-radius: 6px; }"
             "QMenu::item:selected { background: %8; color: %1; }"
             "QDialog { background: %3; color: %1; }"
             "QListWidget {"
             " background: transparent; color: %1; border: none;"
             " outline: none; }"
             "QListWidget::item {"
             " padding: 7px 10px; border-radius: 7px; }"
             "QListWidget::item:selected { background: %8; color: %1; }"
             "QListWidget::item:hover { background: %10; }"
             "QScrollBar:vertical {"
             " background: transparent; width: 10px; margin: 0; }"
             "QScrollBar::handle:vertical {"
             " background: %6; border-radius: 5px; min-height: 24px; }"
             "QScrollBar::handle:vertical:hover { background: %9; }"
             "QScrollBar::add-line:vertical, "
             "QScrollBar::sub-line:vertical { height: 0; }"
             "QScrollBar::add-page:vertical, "
             "QScrollBar::sub-page:vertical { background: transparent; }"
             "QScrollBar:horizontal { height: 10px; background: transparent; }"
             "QScrollBar::handle:horizontal {"
             " background: %6; border-radius: 5px; min-width: 24px; }"
             "QScrollBar::add-line:horizontal, "
             "QScrollBar::sub-line:horizontal { width: 0; }"
             "QToolTip {"
             " background: %1; color: %3; border: none; padding: 4px 7px;"
             " border-radius: 5px; }"
             "QTabWidget::pane { border: 1px solid %4; border-radius: %11px; }"
             "QTabBar::tab { padding: 6px 12px; color: %6; }"
             "QTabBar::tab:selected { color: %1; border-bottom: 2px solid %5; }"
             "QGroupBox { border: 1px solid %4; border-radius: 8px;"
             " margin-top: 10px; padding-top: 8px; }"
             "QGroupBox::title { subcontrol-origin: margin; left: 8px;"
             " padding: 0 4px; color: %6; }")
      // Chained arg() keeps the substitution unambiguous: a gap in the
      // placeholder numbering made the multi-arg overload leave `%1`… in
      // place (and Qt warn "1 argument(s) missing"), which silently dropped
      // the whole widget stylesheet.
      .arg(fg)
      .arg(theme.ui_family)
      .arg(surface)
      .arg(stroke)
      .arg(accent)
      .arg(secondary)
      .arg(background)
      .arg(selected)
      .arg(stroke_strong)
      .arg(hover)
      .arg(radius)
      .arg(chevron)
      .arg(arrow_up)
      .arg(arrow_down);
}

void ApplyGlobalTheme(const Theme &theme) {
  if (qApp == nullptr) {
    return;
  }
  qApp->setFont(theme.UiFont(13, QFont::Normal));
  qApp->setStyleSheet(GlobalStyleSheet(theme));
}

} // namespace hermes::ui
