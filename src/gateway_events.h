// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_EVENTS_H_
#define HERMES_UI_GATEWAY_EVENTS_H_

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

// `notification.show` / `notification.clear`.
QString GatewayNoticeKey(const QJsonObject& payload);
QString GatewayNoticeTitle(const QString& level);
QString GatewayNoticeLine(const GatewayNotice& notice);
void UpsertGatewayNotice(std::vector<GatewayNotice>* notices,
                         const QJsonObject& payload, quint64 serial);
bool ClearGatewayNotice(std::vector<GatewayNotice>* notices,
                        const QString& key);

// `session.control.update` -> one Context-pane line.
QString SessionControlSummary(const QJsonObject& control);

// MoA fan-out lines, rendered in the reasoning disclosure.
QString MoaReferenceText(const QJsonObject& payload);
QString MoaProgressText(const QJsonObject& payload);
QString MoaPhaseText(const QJsonObject& payload);

// Persistent system lines.
QString ReviewSummaryText(const QJsonObject& payload);
QString AgentPrintText(const QJsonObject& payload);

// `tool.generating` / `tool.progress` status labels.
QString ToolGeneratingText(const QJsonObject& payload);
QString ToolProgressText(const QJsonObject& payload);

// `message.reaction` / transcript `display_metadata.reactions`.
std::vector<ChatReaction> ParseChatReactions(const QJsonArray& reactions);
QString ReactionSummary(const QJsonArray& reactions);

// `prompt.background` / `prompt.btw` receipts and their completion events.
// Upsert by task id so a completion that races ahead of the ACK can fill in the
// prompt text later instead of creating a second row.
BackgroundPrompt* FindBackgroundPrompt(std::vector<BackgroundPrompt>* prompts,
                                       const QString& task_id);
BackgroundPrompt* UpsertBackgroundPrompt(
    std::vector<BackgroundPrompt>* prompts, const QString& task_id,
    const QString& session_id, const QString& verb, const QString& text,
    qint64 now_ms, std::size_t limit = 200);
BackgroundPrompt* ApplyBackgroundPromptCompletion(
    std::vector<BackgroundPrompt>* prompts, const QJsonObject& payload,
    qint64 now_ms, std::size_t limit = 200);

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_EVENTS_H_
