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

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_EVENTS_H_
