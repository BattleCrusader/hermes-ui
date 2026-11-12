// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/project_appearance.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QGridLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace hermes::ui {
namespace {

constexpr ushort kDefaultProjectIcon = 0xEA83;  // folder

const std::vector<ProjectAppearanceIcon>& IconTable() {
  static const std::vector<ProjectAppearanceIcon> icons = {
      {QStringLiteral("folder-library"), 0xEBDF},
      {QStringLiteral("repo"), 0xEA62},
      {QStringLiteral("rocket"), 0xEB44},
      {QStringLiteral("beaker"), 0xEA79},
      {QStringLiteral("flame"), 0xEAF2},
      {QStringLiteral("star-full"), 0xEB59},
      {QStringLiteral("heart"), 0xEB05},
      {QStringLiteral("zap"), 0xEA86},
      {QStringLiteral("target"), 0xEBF8},
      {QStringLiteral("lightbulb"), 0xEA61},
      {QStringLiteral("tools"), 0xEB6D},
      {QStringLiteral("device-desktop"), 0xEA7A},
      {QStringLiteral("device-mobile"), 0xEADB},
      {QStringLiteral("terminal"), 0xEA85},
      {QStringLiteral("dashboard"), 0xEACD},
      {QStringLiteral("globe"), 0xEB01},
      {QStringLiteral("broadcast"), 0xEAAD},
      {QStringLiteral("cloud"), 0xEBAA},
      {QStringLiteral("database"), 0xEACE},
      {QStringLiteral("package"), 0xEB29},
      {QStringLiteral("book"), 0xEAA4},
      {QStringLiteral("organization"), 0xEA7E},
      {QStringLiteral("bug"), 0xEAAF},
      {QStringLiteral("shield"), 0xEB53},
      {QStringLiteral("key"), 0xEB11},
      {QStringLiteral("gift"), 0xEAF9},
      {QStringLiteral("telescope"), 0xEB68},
      {QStringLiteral("home"), 0xEB06},
  };
  return icons;
}

QString ColorSwatchStyle(const QColor& color, bool selected,
                         const QColor& border) {
  const QString fill = color.isValid() ? color.name(QColor::HexRgb)
                                       : QStringLiteral("#00000000");
  const QString outline =
      selected ? border.name(QColor::HexRgb) : QStringLiteral("transparent");
  return QStringLiteral(
             "QPushButton { background: %1; border: 2px solid %2; "
             "border-radius: 8px; min-width: 24px; min-height: 24px; "
             "max-width: 24px; max-height: 24px; }")
      .arg(fill, outline);
}

}  // namespace

const std::vector<ProjectAppearanceIcon>& ProjectAppearanceIcons() {
  return IconTable();
}

const QStringList& ProjectAppearanceColors() {
  static const QStringList colors = [] {
    QStringList rows;
    for (int index = 0; index < 12; ++index) {
      rows.push_back(
          QStringLiteral("hsl(%1 68% 58%)").arg(QString::number(index * 30)));
    }
    return rows;
  }();
  return colors;
}

ushort ProjectIconCodepoint(const QString& name) {
  const auto found = std::find_if(
      IconTable().begin(), IconTable().end(),
      [&name](const ProjectAppearanceIcon& icon) { return icon.name == name; });
  return found == IconTable().end() ? kDefaultProjectIcon : found->codepoint;
}

QColor ProjectColorValue(const QString& value) {
  const QString text = value.trimmed();
  if (text.isEmpty()) {
    return QColor();
  }
  static const QRegularExpression hsl(
      QStringLiteral(
          R"(^\s*hsla?\(\s*([0-9.]+)(?:deg)?\s*[, ]\s*([0-9.]+)%\s*[, ]\s*([0-9.]+)%\s*(?:[,/]\s*[0-9.]+%?\s*)?\)\s*$)"),
      QRegularExpression::CaseInsensitiveOption);
  const QRegularExpressionMatch match = hsl.match(text);
  if (match.hasMatch()) {
    qreal hue = std::fmod(match.captured(1).toDouble(), 360.0);
    if (hue < 0.0) {
      hue += 360.0;
    }
    const qreal saturation = match.captured(2).toDouble() / 100.0;
    const qreal lightness = match.captured(3).toDouble() / 100.0;
    return QColor::fromHslF(hue / 360.0, saturation, lightness);
  }
  const QColor parsed = QColor::fromString(text);
  return parsed.isValid() ? parsed : QColor();
}

ProjectAppearanceDialog::ProjectAppearanceDialog(const QString& color,
                                                 const QString& icon,
                                                 QWidget* parent)
    : QDialog(parent), color_(color.trimmed()), icon_(icon.trimmed()) {
  setWindowTitle(QStringLiteral("Project appearance"));
  setObjectName(QStringLiteral("projectAppearanceDialog"));
  setMinimumWidth(360);

  auto* layout = new QVBoxLayout(this);
  layout->addWidget(new QLabel(QStringLiteral("Color"), this));

  auto* color_grid = new QGridLayout();
  color_grid->setSpacing(6);
  auto* clear_color = new QPushButton(QString(QChar(0xEABD)), this);
  clear_color->setObjectName(QStringLiteral("projectColorClear"));
  clear_color->setToolTip(QStringLiteral("No color"));
  clear_color->setAccessibleName(QStringLiteral("No color"));
  clear_color->setCheckable(true);
  QFont glyph_font(QStringLiteral("codicon"));
  glyph_font.setPixelSize(13);
  clear_color->setFont(glyph_font);
  clear_color->setFixedSize(28, 28);
  color_buttons_.push_back(clear_color);
  color_grid->addWidget(clear_color, 0, 0);
  const QStringList& colors = ProjectAppearanceColors();
  for (int index = 0; index < colors.size(); ++index) {
    auto* button = new QPushButton(this);
    button->setObjectName(QStringLiteral("projectColor%1").arg(index));
    button->setToolTip(colors.at(index));
    button->setAccessibleName(colors.at(index));
    button->setCheckable(true);
    button->setFixedSize(28, 28);
    color_buttons_.push_back(button);
    color_grid->addWidget(button, index / 6, index % 6 + 1);
  }
  layout->addLayout(color_grid);

  layout->addSpacing(8);
  layout->addWidget(new QLabel(QStringLiteral("Icon"), this));
  auto* icon_grid = new QGridLayout();
  icon_grid->setSpacing(4);
  const auto& icons = ProjectAppearanceIcons();
  for (int index = 0; index < static_cast<int>(icons.size()); ++index) {
    const ProjectAppearanceIcon& icon_row =
        icons[static_cast<std::size_t>(index)];
    auto* button = new QPushButton(QString(QChar(icon_row.codepoint)), this);
    button->setObjectName(QStringLiteral("projectIcon_%1").arg(icon_row.name));
    button->setToolTip(icon_row.name);
    button->setAccessibleName(icon_row.name);
    button->setCheckable(true);
    button->setFont(glyph_font);
    button->setFixedSize(30, 30);
    icon_buttons_.push_back(button);
    icon_grid->addWidget(button, index / 6, index % 6);
  }
  layout->addLayout(icon_grid);
  layout->addStretch(1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  buttons->button(QDialogButtonBox::Close)
      ->setObjectName(QStringLiteral("projectAppearanceClose"));
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  connect(clear_color, &QPushButton::clicked, this, [this]() {
    color_.clear();
    RefreshSelection();
    Apply();
  });
  for (int index = 0; index < colors.size(); ++index) {
    const QString value = colors.at(index);
    connect(static_cast<QPushButton*>(
                color_buttons_[static_cast<std::size_t>(index + 1)]),
            &QPushButton::clicked, this, [this, value]() {
              color_ = value;
              RefreshSelection();
              Apply();
            });
  }
  for (int index = 0; index < static_cast<int>(icons.size()); ++index) {
    const QString name = icons[static_cast<std::size_t>(index)].name;
    connect(static_cast<QPushButton*>(
                icon_buttons_[static_cast<std::size_t>(index)]),
            &QPushButton::clicked, this, [this, name]() {
              icon_ = icon_ == name ? QString() : name;
              RefreshSelection();
              Apply();
            });
  }
  RefreshSelection();
}

void ProjectAppearanceDialog::Apply() {
  if (on_apply) {
    on_apply(color_, icon_);
  }
  accept();
}

void ProjectAppearanceDialog::RefreshSelection() {
  const bool dark = palette().color(QPalette::Window).lightness() < 128;
  const QColor selected_border = dark ? QColor(QStringLiteral("#FFFFFF"))
                                      : QColor(QStringLiteral("#202020"));
  const QStringList& colors = ProjectAppearanceColors();
  if (!color_buttons_.empty()) {
    auto* clear = static_cast<QPushButton*>(color_buttons_.front());
    clear->setChecked(color_.isEmpty());
    clear->setStyleSheet(ColorSwatchStyle(palette().color(QPalette::Window),
                                          color_.isEmpty(), selected_border));
  }
  for (int index = 0; index < colors.size(); ++index) {
    auto* button = static_cast<QPushButton*>(
        color_buttons_[static_cast<std::size_t>(index + 1)]);
    const QString value = colors.at(index);
    const bool selected = value == color_;
    button->setChecked(selected);
    button->setStyleSheet(
        ColorSwatchStyle(ProjectColorValue(value), selected, selected_border));
  }
  const auto& icons = ProjectAppearanceIcons();
  for (int index = 0; index < static_cast<int>(icon_buttons_.size()); ++index) {
    auto* button = static_cast<QPushButton*>(
        icon_buttons_[static_cast<std::size_t>(index)]);
    const bool selected = index < static_cast<int>(icons.size()) &&
                          icons[static_cast<std::size_t>(index)].name == icon_;
    button->setChecked(selected);
    button->setStyleSheet(
        selected
            ? QStringLiteral(
                  "QPushButton { border: 2px solid %1; border-radius: 7px; }")
                  .arg(selected_border.name(QColor::HexRgb))
            : QStringLiteral("QPushButton { border: 1px solid transparent; "
                             "border-radius: 7px; }"));
  }
}

}  // namespace hermes::ui
