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

// Stored user rows carry attachment transport after a send: leading
// `@file:`/`@image:` lines that the client prepended, and the gateway's
// `--- Attached Context ---` expansion. The bubble shows what the user typed,
// not the wire payload. Only the leading ref block and the expansion tail are
// removed; a ref typed mid-prose stays where the user put it.
QString VisibleUserPromptText(const QString& text);

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

// Carries the most recent completed assistant row across a REST refresh whose
// page predates the commit that persisted it. This is the normal
// `message.complete` race: the live answer is authoritative for a few hundred
// milliseconds, then the next refresh must show it instead of replacing the
// transcript with the older page. If history already has the row, fill in
// reasoning/answer fields it has not projected yet. Otherwise insert the live
// row chronologically, but only when it is recent and history has not moved
// well past it.
void PreserveLiveAssistant(std::vector<ChatMessage>* hydrated,
                           const std::vector<ChatMessage>& live);

// Carries an optimistic user prompt across a REST refresh whose page predates
// the commit that persisted it. Only a trailing local row (row_id 0) that is
// not already present by text is appended; completed history stays
// authoritative for ordering and duplicates.
void PreservePendingUserMessages(std::vector<ChatMessage>* hydrated,
                                 const std::vector<ChatMessage>& live);

}  // namespace hermes::ui

#endif  // HERMES_UI_MESSAGE_HISTORY_H_
