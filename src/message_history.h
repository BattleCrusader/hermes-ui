// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MESSAGE_HISTORY_H_
#define HERMES_UI_MESSAGE_HISTORY_H_

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

// Display-only reasoning stored on a history row. `display_reasoning` is the
// backend-sanitized text (it removes public commentary that is already shown
// as an answer part) and wins even when empty; `reasoning`/`reasoning_content`
// are the raw provider fields, and structured `reasoning_details` is the last
// fallback. Matches the desktop chat-messages hydration contract.
QString ReasoningTextFromRow(const QJsonObject& row);

// Extracts readable prose from a provider reasoning-details envelope. Signed
// and encrypted replay fields are never exposed.
QString ReasoningTextFromDetails(const QJsonValue& details);

// True when the hydrated transcript already carries the live assistant row.
// Matching uses the durable row id, then the answer text, then the trailing
// assistant timestamp. Used to avoid hydrating over a live reasoning-only turn
// that the backend has not persisted yet.
bool HistoryContainsAssistant(const std::vector<ChatMessage>& hydrated,
                              const ChatMessage& live);

// Carries reasoning across a REST refresh when history has the same assistant
// row but has not persisted its reasoning yet. History stays authoritative for
// content and ordering.
void PreserveLiveReasoning(std::vector<ChatMessage>* hydrated,
                           const std::vector<ChatMessage>& live);

}  // namespace hermes::ui

#endif  // HERMES_UI_MESSAGE_HISTORY_H_
