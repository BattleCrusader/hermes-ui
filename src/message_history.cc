// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/message_history.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

QString TrimmedString(const QJsonValue& value) {
  return value.isString() ? value.toString().trimmed() : QString();
}

void PushUnique(QStringList* text, const QString& value) {
  const QString prose = value.trimmed();
  if (!prose.isEmpty() && !text->contains(prose)) {
    text->append(prose);
  }
}

void WalkReasoningNode(const QJsonValue& value, QStringList* text) {
  if (value.isArray()) {
    for (const QJsonValue& item : value.toArray()) {
      WalkReasoningNode(item, text);
    }
    return;
  }
  if (!value.isObject()) {
    return;
  }
  const QJsonObject record = value.toObject();
  const QString kind = record.value(QStringLiteral("type")).toString();
  if (kind == QStringLiteral("reasoning.summary")) {
    PushUnique(text, record.value(QStringLiteral("summary")).toString());
  } else if (kind == QStringLiteral("reasoning.text")) {
    PushUnique(text, record.value(QStringLiteral("text")).toString());
  } else if (record.value(QStringLiteral("thinking")).isString()) {
    PushUnique(text, record.value(QStringLiteral("thinking")).toString());
  }
  for (auto it = record.begin(); it != record.end(); ++it) {
    if (it.key() == QStringLiteral("signature") ||
        it.key() == QStringLiteral("projection") ||
        it.key() == QStringLiteral("data") ||
        it.key() == QStringLiteral("type")) {
      continue;
    }
    if (it.value().isArray() || it.value().isObject()) {
      WalkReasoningNode(it.value(), text);
    }
  }
}

bool SameAssistant(const ChatMessage& hydrated, const ChatMessage& live) {
  if (hydrated.role != MessageRole::kAssistant ||
      live.role != MessageRole::kAssistant) {
    return false;
  }
  if (hydrated.row_id > 0 && live.row_id > 0) {
    return hydrated.row_id == live.row_id;
  }
  const QString live_text = live.raw_text.trimmed();
  if (!live_text.isEmpty() && !hydrated.raw_text.trimmed().isEmpty()) {
    return hydrated.raw_text.trimmed() == live_text;
  }
  if (!hydrated.timestamp.isValid() || !live.timestamp.isValid()) {
    return false;
  }
  return std::abs(hydrated.timestamp.secsTo(live.timestamp)) <= 15 * 60;
}

int LineStartAt(const QString& text, const QString& marker) {
  int start = 0;
  while (start <= text.size()) {
    int end = text.indexOf(QLatin1Char('\n'), start);
    if (end < 0) {
      end = text.size();
    }
    if (text.mid(start, end - start).trimmed() == marker) {
      return start;
    }
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
  return -1;
}

bool IsLeadingAttachmentRef(const QString& line) {
  if (!line.startsWith(QLatin1Char('@'))) {
    return false;
  }
  const int colon = line.indexOf(QLatin1Char(':'));
  if (colon <= 1) {
    return false;
  }
  const QString kind = line.mid(1, colon - 1).toLower();
  if (kind != QStringLiteral("file") && kind != QStringLiteral("folder") &&
      kind != QStringLiteral("image")) {
    return false;
  }
  for (const QChar ch : kind) {
    if (!ch.isLetterOrNumber() && ch != QLatin1Char('-')) {
      return false;
    }
  }
  return true;
}

QString UserText(const ChatMessage& message) {
  if (!message.raw_text.trimmed().isEmpty()) {
    return message.raw_text.trimmed();
  }
  QStringList parts;
  for (const MessageBlock& block : message.blocks) {
    const QString text = block.text.trimmed();
    if (!text.isEmpty()) {
      parts.append(text);
    }
  }
  return parts.join(QLatin1Char('\n'));
}

}  // namespace

QString VisibleUserPromptText(const QString& text) {
  int end = text.size();
  bool cut = false;
  for (const char* marker : {"--- Attached Context ---",
                             "--- Context Warnings ---"}) {
    const int index = LineStartAt(text, QLatin1String(marker));
    if (index >= 0 && index < end) {
      end = index;
      cut = true;
    }
  }
  QString visible = text.left(end);
  QStringList lines = visible.split(QLatin1Char('\n'));
  bool stripped_refs = false;
  while (!lines.isEmpty() && IsLeadingAttachmentRef(lines.front())) {
    lines.removeFirst();
    stripped_refs = true;
  }
  if (stripped_refs) {
    visible = lines.join(QLatin1Char('\n')).trimmed();
  } else if (cut) {
    visible = visible.trimmed();
  }
  return visible;
}

QString ReasoningTextFromDetails(const QJsonValue& details) {
  QJsonValue blocks = details;
  if (details.isString()) {
    const QString trimmed = details.toString().trimmed();
    if (!trimmed.startsWith(QLatin1Char('[')) &&
        !trimmed.startsWith(QLatin1Char('{'))) {
      return trimmed;
    }
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || document.isNull()) {
      return QString();
    }
    blocks = document.isArray() ? QJsonValue(document.array())
                                : QJsonValue(document.object());
  }

  QStringList text;
  const QJsonArray top =
      blocks.isArray() ? blocks.toArray() : QJsonArray{blocks};
  for (const QJsonValue& value : top) {
    if (value.isString()) {
      PushUnique(&text, value.toString());
      continue;
    }
    if (!value.isObject()) {
      continue;
    }
    const QJsonObject record = value.toObject();
    for (const char* key : {"summary", "thinking", "content", "text"}) {
      const QString prose = TrimmedString(record.value(QLatin1String(key)));
      if (!prose.isEmpty()) {
        PushUnique(&text, prose);
        break;
      }
    }
    WalkReasoningNode(value, &text);
  }
  return text.join(QStringLiteral("\n\n")).trimmed();
}

QString ReasoningTextFromRow(const QJsonObject& row) {
  if (row.contains(QStringLiteral("display_reasoning"))) {
    return row.value(QStringLiteral("display_reasoning")).toString();
  }
  const QString reasoning = row.value(QStringLiteral("reasoning")).toString();
  if (!reasoning.isEmpty()) {
    return reasoning;
  }
  const QString content =
      row.value(QStringLiteral("reasoning_content")).toString();
  if (!content.isEmpty()) {
    return content;
  }
  return ReasoningTextFromDetails(
      row.value(QStringLiteral("reasoning_details")));
}

bool HistoryContainsAssistant(const std::vector<ChatMessage>& hydrated,
                              const ChatMessage& live) {
  for (const ChatMessage& candidate : hydrated) {
    if (SameAssistant(candidate, live)) {
      return true;
    }
  }
  return false;
}

void PreserveLiveReasoning(std::vector<ChatMessage>* hydrated,
                           const std::vector<ChatMessage>& live) {
  if (hydrated == nullptr || hydrated->empty()) {
    return;
  }
  QString live_reasoning;
  ChatMessage live_assistant;
  for (auto it = live.rbegin(); it != live.rend(); ++it) {
    if (it->role == MessageRole::kAssistant &&
        !it->reasoning.trimmed().isEmpty()) {
      live_reasoning = it->reasoning;
      live_assistant = *it;
      break;
    }
  }
  if (live_reasoning.isEmpty()) {
    return;
  }
  for (auto it = hydrated->rbegin(); it != hydrated->rend(); ++it) {
    if (!SameAssistant(*it, live_assistant)) {
      continue;
    }
    if (it->reasoning.trimmed().isEmpty()) {
      it->reasoning = live_reasoning;
    }
    return;
  }
}

void PreserveLiveAssistant(std::vector<ChatMessage>* hydrated,
                           const std::vector<ChatMessage>& live) {
  if (hydrated == nullptr) {
    return;
  }
  const ChatMessage* assistant = nullptr;
  for (auto it = live.rbegin(); it != live.rend(); ++it) {
    if (it->role != MessageRole::kAssistant || it->streaming ||
        it->reasoning_streaming) {
      continue;
    }
    if (!it->raw_text.trimmed().isEmpty() ||
        !it->reasoning.trimmed().isEmpty()) {
      assistant = &*it;
      break;
    }
  }
  if (assistant == nullptr) {
    return;
  }
  const QDateTime now = QDateTime::currentDateTime();
  if (!assistant->timestamp.isValid() ||
      assistant->timestamp.secsTo(now) > 10 * 60) {
    return;
  }

  for (ChatMessage& candidate : *hydrated) {
    if (!SameAssistant(candidate, *assistant)) {
      continue;
    }
    if (candidate.reasoning.trimmed().isEmpty()) {
      candidate.reasoning = assistant->reasoning;
      candidate.reasoning_streaming = false;
    }
    // A reasoning-only history row can land before its answer text. Do not
    // blink the completed answer out while the transcript projection catches
    // up with the same row.
    if (candidate.raw_text.trimmed().isEmpty() &&
        !assistant->raw_text.trimmed().isEmpty()) {
      candidate.raw_text = assistant->raw_text;
      candidate.blocks = assistant->blocks;
    }
    return;
  }

  // If history already carries rows well past this assistant, the live tail
  // is stale (a replaced or interrupted turn). Prefer history.
  QDateTime newest_hydrated;
  for (const ChatMessage& message : *hydrated) {
    if (message.timestamp.isValid() &&
        (!newest_hydrated.isValid() ||
         message.timestamp > newest_hydrated)) {
      newest_hydrated = message.timestamp;
    }
  }
  if (newest_hydrated.isValid() &&
      assistant->timestamp.secsTo(newest_hydrated) > 30) {
    return;
  }

  auto insert_at = hydrated->end();
  for (auto it = hydrated->end(); it != hydrated->begin();) {
    --it;
    if (it->timestamp.isValid() &&
        it->timestamp <= assistant->timestamp) {
      insert_at = std::next(it);
      break;
    }
  }
  hydrated->insert(insert_at, *assistant);
}

void PreservePendingUserMessages(std::vector<ChatMessage>* hydrated,
                                 const std::vector<ChatMessage>& live) {
  if (hydrated == nullptr) {
    return;
  }
  const ChatMessage* pending = nullptr;
  for (auto it = live.rbegin(); it != live.rend(); ++it) {
    if (it->role == MessageRole::kUser && it->row_id <= 0 &&
        !UserText(*it).isEmpty()) {
      pending = &*it;
      break;
    }
  }
  if (pending == nullptr) {
    return;
  }
  // Skip ancient local rows left over from a failed send long ago; only the
  // prompt that can still belong to the visible tail of the transcript is
  // worth carrying across.
  if (!hydrated->empty() && pending->timestamp.isValid() &&
      hydrated->back().timestamp.isValid() &&
      hydrated->back().timestamp.secsTo(pending->timestamp) < -2 * 60) {
    return;
  }
  const QString text = UserText(*pending);
  for (const ChatMessage& existing : *hydrated) {
    if (existing.role == MessageRole::kUser && UserText(existing) == text) {
      return;
    }
  }
  hydrated->push_back(*pending);
}

}  // namespace hermes::ui
