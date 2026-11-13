// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/message_history.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>
#include <cmath>
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
