// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/theme.h"

#include <QFontDatabase>
#include <algorithm>
#include <cmath>

namespace hermes::ui {
namespace {

QColor WithAlpha(const QColor& color, int alpha) {
  QColor result = color;
  result.setAlpha(alpha);
  return result;
}

}  // namespace

QColor Mix(const QColor& first, const QColor& second, double amount) {
  const double t = std::clamp(amount, 0.0, 1.0);
  return QColor::fromRgbF(first.redF() * (1.0 - t) + second.redF() * t,
                          first.greenF() * (1.0 - t) + second.greenF() * t,
                          first.blueF() * (1.0 - t) + second.blueF() * t,
                          first.alphaF() * (1.0 - t) + second.alphaF() * t);
}

double ContrastRatio(const QColor& first, const QColor& second) {
  const auto linear = [](double channel) {
    return channel <= 0.04045 ? channel / 12.92
                              : std::pow((channel + 0.055) / 1.055, 2.4);
  };
  const auto luminance = [&](const QColor& color) {
    return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) +
           0.0722 * linear(color.blueF());
  };
  const double a = luminance(first);
  const double b = luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

QColor EnsureContrast(const QColor& color, const QColor& background,
                      const QColor& fallback, double min_ratio) {
  if (ContrastRatio(color, background) >= min_ratio) {
    return color;
  }
  QColor target = fallback;
  if (ContrastRatio(target, background) < min_ratio) {
    target = ContrastRatio(QColor(Qt::white), background) >
                     ContrastRatio(QColor(Qt::black), background)
                 ? QColor(Qt::white)
                 : QColor(Qt::black);
  }
  double low = 0.0;
  double high = 1.0;
  for (int i = 0; i < 24; ++i) {
    const double mid = (low + high) / 2.0;
    if (ContrastRatio(Mix(color, target, mid), background) >= min_ratio) {
      high = mid;
    } else {
      low = mid;
    }
  }
  return Mix(color, target, high);
}

void EnsureCodeContrast(Theme* theme) {
  if (theme == nullptr || !theme->code_background.isValid()) {
    return;
  }
  const QColor background = theme->code_background;
  const QColor fallback = theme->foreground;
  theme->code_text =
      EnsureContrast(theme->code_text, background, fallback, 7.0);
  constexpr double kTokenContrast = 4.5;
  theme->code_comment =
      EnsureContrast(theme->code_comment, background, fallback, kTokenContrast);
  // The language label and Copy chip live on the header strip, which is a
  // different fill than the code body. Keep the comment tone readable on both.
  if (theme->code_header.isValid()) {
    theme->code_comment = EnsureContrast(
        theme->code_comment, theme->code_header, fallback, kTokenContrast);
  }
  theme->code_keyword =
      EnsureContrast(theme->code_keyword, background, fallback, kTokenContrast);
  theme->code_string =
      EnsureContrast(theme->code_string, background, fallback, kTokenContrast);
  theme->code_number =
      EnsureContrast(theme->code_number, background, fallback, kTokenContrast);
}

QString Initials(const QString& name) {
  const QStringList words = name.split(' ', Qt::SkipEmptyParts);
  QString result;
  for (const QString& word : words) {
    if (result.size() == 2) {
      break;
    }
    if (!word.isEmpty()) {
      result += word.front().toUpper();
    }
  }
  if (result.isEmpty()) {
    result = name.left(1).toUpper();
  }
  return result;
}

QFont Theme::UiFont(int pixel_size, QFont::Weight weight) const {
  QFont font(ui_family);
  font.setPixelSize(qMax(6, qRound(pixel_size * font_scale)));
  font.setWeight(weight);
  font.setStyleStrategy(QFont::PreferAntialias);
  return font;
}

QFont Theme::MonoFont(int pixel_size, QFont::Weight weight) const {
  QFont font(mono_family);
  font.setPixelSize(qMax(6, qRound(pixel_size * font_scale)));
  font.setWeight(weight);
  font.setStyleHint(QFont::Monospace);
  return font;
}

QFont Theme::IconFont(int pixel_size) const {
  QFont font(icon_family);
  font.setPixelSize(pixel_size);
  return font;
}

Theme Theme::Light() {
  Theme theme;
  theme.background = QColor("#F8FAFF");
  theme.sidebar = QColor("#F3F7FF");
  theme.surface = QColor("#FFFFFF");
  theme.surface_raised = QColor("#FFFFFF");
  theme.foreground = QColor("#17171A");
  theme.text_secondary = QColor("#5B5B6B");
  theme.text_tertiary = QColor("#8B8B9A");
  theme.stroke = WithAlpha(QColor("#17171A"), 26);
  theme.stroke_strong = WithAlpha(QColor("#17171A"), 41);
  theme.accent = QColor("#0053FD");
  theme.accent_soft = QColor("#E9F0FF");
  theme.accent_text = QColor("#0053FD");
  theme.user_bubble = QColor("#EDF3FF");
  theme.hover = WithAlpha(QColor("#17171A"), 10);
  theme.selected = WithAlpha(QColor("#0053FD"), 20);
  theme.code_background = QColor("#F6F8FA");
  theme.code_header = QColor("#EEF2F7");
  theme.code_text = QColor("#1F2328");
  theme.code_comment = QColor("#6E7781");
  theme.code_keyword = QColor("#CF222E");
  theme.code_string = QColor("#0A3069");
  theme.code_number = QColor("#0550AE");
  theme.success = QColor("#1F8A65");
  theme.warning = QColor("#C08532");
  theme.danger = QColor("#CF2D56");
  theme.ui_family =
      QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  theme.mono_family = QStringLiteral("JetBrains Mono");
  theme.icon_family = QStringLiteral("codicon");
  EnsureCodeContrast(&theme);
  return theme;
}

Theme Theme::Dark() {
  Theme theme;
  theme.background = QColor("#0D0D0E");
  theme.sidebar = QColor("#0A0A0B");
  theme.surface = QColor("#161618");
  theme.surface_raised = QColor("#1C1C1F");
  theme.foreground = QColor("#E6E6E8");
  theme.text_secondary = QColor("#A2A2AD");
  theme.text_tertiary = QColor("#6E6E78");
  theme.stroke = WithAlpha(QColor("#FFFFFF"), 20);
  theme.stroke_strong = WithAlpha(QColor("#FFFFFF"), 36);
  theme.accent = QColor("#4C8DFF");
  theme.accent_soft = WithAlpha(QColor("#4C8DFF"), 46);
  theme.accent_text = QColor("#8AB8FF");
  theme.user_bubble = QColor("#16233D");
  theme.hover = WithAlpha(QColor("#FFFFFF"), 16);
  theme.selected = WithAlpha(QColor("#4C8DFF"), 56);
  theme.code_background = QColor("#161B22");
  theme.code_header = QColor("#1C2128");
  theme.code_text = QColor("#E6EDF3");
  theme.code_comment = QColor("#8B949E");
  theme.code_keyword = QColor("#FF7B72");
  theme.code_string = QColor("#A5D6FF");
  theme.code_number = QColor("#79C0FF");
  theme.success = QColor("#55A583");
  theme.warning = QColor("#C08532");
  theme.danger = QColor("#E75E78");
  theme.ui_family =
      QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
  theme.mono_family = QStringLiteral("JetBrains Mono");
  theme.icon_family = QStringLiteral("codicon");
  EnsureCodeContrast(&theme);
  return theme;
}

}  // namespace hermes::ui
