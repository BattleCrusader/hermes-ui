// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SRC_COMPOSER_HISTORY_H_
#define HERMES_UI_SRC_COMPOSER_HISTORY_H_

#include <QString>
#include <QStringList>

namespace hermes::ui {

namespace composer_history_detail {

// Hermes persists several machine-authored rows as role:"user" so strict
// providers accept them. They are not words the user typed, so browsing up
// into one would look like the composer had pasted a wall of scaffolding.
// Keep this list aligned with the agent's control-frame prefixes
// (agent/context_compressor.py, agent/title_generator.py).
inline const QStringList& MachinePrefixes() {
  static const QStringList prefixes = {
      QStringLiteral("[System:"),
      QStringLiteral("[SYSTEM]"),
      QStringLiteral("[System note:"),
      QStringLiteral("[Runtime note:"),
      QStringLiteral("[CONTEXT"),
      QStringLiteral("[PRIOR CONTEXT"),
      QStringLiteral("[IMPORTANT:"),
      QStringLiteral("[Your active task list"),
      QStringLiteral("[Planning state preserved"),
      QStringLiteral("[ASYNC DELEGATION"),
      QStringLiteral("Cronjob Response:"),
      QStringLiteral("Runtime note:"),
      QStringLiteral("System note:"),
      QStringLiteral("System:"),
      QStringLiteral("IMPORTANT:"),
      QStringLiteral("Planning state preserved"),
      QStringLiteral("CONTEXT COMPACTION"),
      QStringLiteral("CONTEXT SUMMARY]"),
      QStringLiteral("PRIOR CONTEXT"),
  };
  return prefixes;
}

// A steer is a real user message wrapped in a marker that teaches the model to
// trust it. The words inside are the user's, so unwrap them instead of
// dropping the row.
inline QString SteerBody(const QString& text) {
  const QString open = QStringLiteral("[OUT-OF-BAND USER MESSAGE");
  const QString close = QStringLiteral("[/OUT-OF-BAND USER MESSAGE]");
  if (!text.startsWith(open)) {
    return QString();
  }
  const int body_start = text.indexOf(QLatin1Char('\n'));
  if (body_start < 0) {
    return QString();
  }
  int body_end = text.lastIndexOf(close);
  if (body_end <= body_start) {
    body_end = text.size();
  }
  return text.mid(body_start + 1, body_end - body_start - 1).trimmed();
}

}  // namespace composer_history_detail

// Returns the text that belongs in composer history, or an empty string for a
// machine-authored control frame.
inline QString ComposerHistoryEntryFor(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.startsWith(
          QStringLiteral("[OUT-OF-BAND USER MESSAGE"))) {
    return composer_history_detail::SteerBody(trimmed);
  }
  for (const QString& prefix : composer_history_detail::MachinePrefixes()) {
    if (trimmed.startsWith(prefix)) {
      return QString();
    }
  }
  return trimmed;
}

// Old prompts from earlier sessions are appended after the current session's
// history. Shell-style recall should still work when a chat is brand new and
// the transcript has nothing to seed from.
inline QStringList MergeComposerHistories(const QStringList& session,
                                          const QStringList& profile) {
  QStringList merged = session;
  for (const QString& entry : profile) {
    if (!entry.isEmpty() && !merged.contains(entry)) {
      merged.append(entry);
    }
  }
  return merged;
}

// Builds the full browse list from three sources. The transcript is
// authoritative for gateway prompts; local submissions that never became rows
// (for example `/new`) are kept in front because they were the most recent
// things the user typed. Profile entries only fill in older sessions.
inline QStringList BuildComposerHistory(const QStringList& transcript,
                                        const QStringList& local,
                                        const QStringList& profile) {
  QStringList history;
  for (const QString& entry : local) {
    if (!entry.isEmpty() && !transcript.contains(entry) &&
        !history.contains(entry)) {
      history.append(entry);
    }
  }
  for (const QString& entry : transcript) {
    if (!entry.isEmpty() && !history.contains(entry)) {
      history.append(entry);
    }
  }
  for (const QString& entry : profile) {
    if (!entry.isEmpty() && !history.contains(entry)) {
      history.append(entry);
    }
  }
  return history;
}

}  // namespace hermes::ui

#endif  // HERMES_UI_SRC_COMPOSER_HISTORY_H_
