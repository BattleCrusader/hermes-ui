// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROJECT_APPEARANCE_H_
#define HERMES_UI_PROJECT_APPEARANCE_H_

#include <QColor>
#include <QDialog>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QWidget;

namespace hermes::ui {

struct ProjectAppearanceIcon {
  QString name;
  ushort codepoint = 0;
};

// Desktop's curated project-glyph set. The wire stores the codicon name, so the
// native sidebar maps it to the same font codepoint instead of guessing.
const std::vector<ProjectAppearanceIcon>& ProjectAppearanceIcons();
// Evenly spaced hues matching the desktop profile/project swatches.
const QStringList& ProjectAppearanceColors();
ushort ProjectIconCodepoint(const QString& name);
QColor ProjectColorValue(const QString& value);

// Compact color-swatch + icon-grid picker used from the project row menu.
// Selecting either field applies immediately (the dialog closes), matching the
// desktop popover's adopt-aware appearance write.
class ProjectAppearanceDialog : public QDialog {
 public:
  ProjectAppearanceDialog(const QString& color, const QString& icon,
                          QWidget* parent = nullptr);

  QString color() const { return color_; }
  QString icon() const { return icon_; }

  std::function<void(const QString& color, const QString& icon)> on_apply;

 private:
  void Apply();
  void RefreshSelection();

  QString color_;
  QString icon_;
  std::vector<QWidget*> color_buttons_;
  std::vector<QWidget*> icon_buttons_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PROJECT_APPEARANCE_H_
