// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_NOTIFICATION_TEXT_H_
#define HERMES_UI_NOTIFICATION_TEXT_H_

#include <QChar>
#include <QString>

namespace hermes::ui {

// Completion notifications name the prompt that opened the turn, not the
// session title. A long prompt is folded to one readable line instead of
// dumping the whole (possibly multi-paragraph) submission into the banner.
inline QString NotificationPromptSummary(const QString& prompt,
                                         int max_chars = 140) {
  const QString text = prompt.simplified();
  if (text.isEmpty()) {
    return {};
  }
  if (max_chars <= 1) {
    return QStringLiteral("\u2026");
  }
  if (text.size() <= max_chars) {
    return text;
  }
  const int limit = max_chars - 1;
  int cut = text.lastIndexOf(QLatin1Char(' '), limit);
  if (cut < limit / 2) {
    cut = limit;
  }
  return text.left(cut).trimmed() + QChar(0x2026);
}

}  // namespace hermes::ui

#endif  // HERMES_UI_NOTIFICATION_TEXT_H_
