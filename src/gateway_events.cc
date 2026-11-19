// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_events.h"

#include <QStringList>
#include <algorithm>
#include <utility>
#include <vector>

namespace hermes::ui {
namespace {

QString Joined(const QStringList& parts) {
  QStringList non_empty;
  for (const QString& part : parts) {
    if (!part.trimmed().isEmpty()) {
      non_empty.push_back(part);
    }
  }
  return non_empty.join(QStringLiteral(" · "));
}

QString StatusLine(const QString& label, const QJsonObject& snapshot) {
  const QString status = snapshot.value(QStringLiteral("status")).toString();
  if (status.trimmed().isEmpty()) {
    return QString();
  }
  return QStringLiteral("%1: %2").arg(label, status);
}

QString IntervalText(double seconds) {
  if (seconds <= 0) {
    return QString();
  }
  if (seconds >= 60 && static_cast<int>(seconds) % 60 == 0) {
    return QStringLiteral("%1m").arg(static_cast<int>(seconds) / 60);
  }
  return QStringLiteral("%1s").arg(static_cast<int>(seconds));
}

}  // namespace

QString GatewayNoticeKey(const QJsonObject& payload) {
  QString key = payload.value(QStringLiteral("key")).toString().trimmed();
  if (key.isEmpty()) {
    key = payload.value(QStringLiteral("id")).toString().trimmed();
  }
  return key;
}

QString GatewayNoticeTitle(const QString& level) {
  if (level == QStringLiteral("error")) {
    return QStringLiteral("Error");
  }
  if (level == QStringLiteral("warn") || level == QStringLiteral("warning")) {
    return QStringLiteral("Warning");
  }
  if (level == QStringLiteral("success")) {
    return QStringLiteral("Done");
  }
  return QStringLiteral("Notice");
}

QString GatewayNoticeLine(const GatewayNotice& notice) {
  const QString text = notice.text.trimmed();
  if (text.isEmpty()) {
    return notice.detail.trimmed();
  }
  return text;
}

void UpsertGatewayNotice(std::vector<GatewayNotice>* notices,
                         const QJsonObject& payload, quint64 serial) {
  if (notices == nullptr) {
    return;
  }
  GatewayNotice notice;
  notice.key = GatewayNoticeKey(payload);
  notice.text = payload.value(QStringLiteral("text")).toString();
  notice.level = payload.value(QStringLiteral("level")).toString();
  notice.kind = payload.value(QStringLiteral("kind")).toString();
  notice.detail = payload.value(QStringLiteral("detail")).toString();
  notice.ttl_ms = payload.value(QStringLiteral("ttl_ms")).toInt();
  notice.serial = serial;
  if (notice.text.trimmed().isEmpty() && notice.detail.trimmed().isEmpty()) {
    return;
  }
  if (!notice.key.isEmpty()) {
    for (GatewayNotice& existing : *notices) {
      if (existing.key == notice.key) {
        existing = std::move(notice);
        return;
      }
    }
  }
  notices->push_back(std::move(notice));
  // Notices are a status list, not an archive. Keep the newest few so a long
  // session cannot grow the snapshot without bound.
  constexpr std::size_t kMaxNotices = 8;
  if (notices->size() > kMaxNotices) {
    notices->erase(notices->begin(),
                   notices->begin() + static_cast<std::ptrdiff_t>(
                                          notices->size() - kMaxNotices));
  }
}

bool ClearGatewayNotice(std::vector<GatewayNotice>* notices,
                        const QString& key) {
  if (notices == nullptr || key.isEmpty()) {
    return false;
  }
  const auto before = notices->size();
  notices->erase(std::remove_if(notices->begin(), notices->end(),
                                [&key](const GatewayNotice& notice) {
                                  return notice.key == key;
                                }),
                 notices->end());
  return notices->size() != before;
}

QString SessionControlSummary(const QJsonObject& control) {
  const QJsonObject snapshot =
      control.value(QStringLiteral("control")).toObject().isEmpty()
          ? control
          : control.value(QStringLiteral("control")).toObject();
  QStringList parts;
  const QJsonObject goal = snapshot.value(QStringLiteral("goal")).toObject();
  if (!goal.isEmpty()) {
    QString line = StatusLine(QStringLiteral("Goal"), goal);
    const QString title =
        goal.value(QStringLiteral("title")).toString().trimmed();
    if (!line.isEmpty() && !title.isEmpty()) {
      line += QStringLiteral(" — %1").arg(title);
    }
    parts.push_back(line);
  }
  const QJsonObject loop = snapshot.value(QStringLiteral("loop")).toObject();
  if (!loop.isEmpty()) {
    QString line = StatusLine(QStringLiteral("Loop"), loop);
    const QString interval =
        IntervalText(loop.value(QStringLiteral("interval_seconds")).toDouble());
    if (!line.isEmpty() && !interval.isEmpty()) {
      line += QStringLiteral(" every %1").arg(interval);
    }
    parts.push_back(line);
  }
  const QJsonObject heartbeat =
      snapshot.value(QStringLiteral("heartbeat")).toObject();
  if (!heartbeat.isEmpty()) {
    QString line = StatusLine(QStringLiteral("Heartbeat"), heartbeat);
    const QString interval = IntervalText(
        heartbeat.value(QStringLiteral("interval_seconds")).toDouble());
    if (!line.isEmpty() && !interval.isEmpty()) {
      line += QStringLiteral(" every %1").arg(interval);
    }
    parts.push_back(line);
  }
  return Joined(parts);
}

QString MoaReferenceText(const QJsonObject& payload) {
  QString label = payload.value(QStringLiteral("label")).toString().trimmed();
  if (label.isEmpty()) {
    label = QStringLiteral("reference");
  }
  const int index = payload.value(QStringLiteral("index")).toInt();
  const int count = payload.value(QStringLiteral("count")).toInt();
  const QString header = index > 0 && count > 0
                             ? QStringLiteral("◇ Reference %1/%2 — %3")
                                   .arg(index)
                                   .arg(count)
                                   .arg(label)
                             : QStringLiteral("◇ Reference — %1").arg(label);
  const QString body =
      payload.value(QStringLiteral("text")).toString().trimmed();
  if (body.isEmpty()) {
    return header + QStringLiteral("\n\n");
  }
  return header + QLatin1Char('\n') + body + QStringLiteral("\n\n");
}

QString MoaProgressText(const QJsonObject& payload) {
  if (!payload.value(QStringLiteral("refs_done")).isDouble() ||
      !payload.value(QStringLiteral("refs_total")).isDouble()) {
    return QString();
  }
  const QString label =
      payload.value(QStringLiteral("label")).toString().trimmed();
  const QString counts =
      QStringLiteral("%1/%2")
          .arg(payload.value(QStringLiteral("refs_done")).toInt())
          .arg(payload.value(QStringLiteral("refs_total")).toInt());
  if (label.isEmpty()) {
    return QStringLiteral("◇ MoA refs %1\n").arg(counts);
  }
  return QStringLiteral("◇ MoA refs %1 — %2\n").arg(counts, label);
}

QString MoaPhaseText(const QJsonObject& payload) {
  if (payload.value(QStringLiteral("phase")).toString() ==
      QStringLiteral("aggregator")) {
    return QStringLiteral("◇ MoA aggregating…\n");
  }
  return QString();
}

QString ReviewSummaryText(const QJsonObject& payload) {
  QString text = payload.value(QStringLiteral("text")).toString().trimmed();
  // The Python side prefixes the line with a 💾 glyph. The transcript row has
  // its own marker, so carrying the emoji would double it.
  while (!text.isEmpty() && !text.front().isLetterOrNumber()) {
    text.remove(0, 1);
  }
  return text.trimmed();
}

QString AgentPrintText(const QJsonObject& payload) {
  return payload.value(QStringLiteral("text")).toString().trimmed();
}

QString ToolGeneratingText(const QJsonObject& payload) {
  const QString name =
      payload.value(QStringLiteral("name")).toString().trimmed();
  if (name.isEmpty()) {
    return QStringLiteral("Preparing tool call…");
  }
  return QStringLiteral("Preparing %1…").arg(name);
}

QString ToolProgressText(const QJsonObject& payload) {
  const QString message =
      payload.value(QStringLiteral("message")).toString().trimmed();
  const QString name =
      payload.value(QStringLiteral("name")).toString().trimmed();
  int percent = payload.value(QStringLiteral("percent")).toInt(-1);
  if (percent < 0) {
    const double progress =
        payload.value(QStringLiteral("progress")).toDouble(-1);
    const double total = payload.value(QStringLiteral("total")).toDouble(-1);
    if (progress >= 0 && total > 0) {
      percent = static_cast<int>((progress / total) * 100.0);
    }
  }
  QStringList parts;
  if (!name.isEmpty()) {
    parts.push_back(name);
  }
  if (!message.isEmpty()) {
    parts.push_back(message);
  }
  if (percent >= 0) {
    parts.push_back(QStringLiteral("%1%").arg(percent));
  }
  return parts.join(QStringLiteral(" · "));
}

std::vector<ChatReaction> ParseChatReactions(const QJsonArray& reactions) {
  std::vector<ChatReaction> parsed;
  for (const QJsonValue& value : reactions) {
    const QJsonObject row = value.toObject();
    ChatReaction reaction;
    reaction.emoji = row.value(QStringLiteral("emoji")).toString();
    reaction.author = row.value(QStringLiteral("author")).toString();
    if (!reaction.emoji.isEmpty()) {
      parsed.push_back(std::move(reaction));
    }
  }
  return parsed;
}

BackgroundPrompt* FindBackgroundPrompt(std::vector<BackgroundPrompt>* prompts,
                                       const QString& task_id) {
  if (prompts == nullptr || task_id.isEmpty()) {
    return nullptr;
  }
  for (BackgroundPrompt& prompt : *prompts) {
    if (prompt.task_id == task_id) {
      return &prompt;
    }
  }
  return nullptr;
}

BackgroundPrompt* UpsertBackgroundPrompt(
    std::vector<BackgroundPrompt>* prompts, const QString& task_id,
    const QString& session_id, const QString& verb, const QString& text,
    qint64 now_ms, std::size_t limit) {
  if (prompts == nullptr || task_id.isEmpty()) {
    return nullptr;
  }
  BackgroundPrompt* prompt = FindBackgroundPrompt(prompts, task_id);
  if (prompt == nullptr) {
    BackgroundPrompt fresh;
    fresh.task_id = task_id;
    fresh.created_ms = now_ms;
    prompts->push_back(std::move(fresh));
    prompt = &prompts->back();
  }
  if (!session_id.isEmpty()) {
    prompt->session_id = session_id;
  }
  if (!verb.isEmpty()) {
    prompt->verb = verb;
  }
  // A completion can arrive before the verb ACK. Do not erase the task's
  // output when the ACK eventually supplies the prompt text.
  if (!text.isEmpty()) {
    prompt->text = text;
  }
  while (limit > 0 && prompts->size() > limit) {
    prompts->erase(prompts->begin());
  }
  return FindBackgroundPrompt(prompts, task_id);
}

BackgroundPrompt* ApplyBackgroundPromptCompletion(
    std::vector<BackgroundPrompt>* prompts, const QJsonObject& payload,
    qint64 now_ms, std::size_t limit) {
  const QString task_id =
      payload.value(QStringLiteral("task_id")).toString().trimmed();
  if (prompts == nullptr || task_id.isEmpty()) {
    return nullptr;
  }
  const QString session_id =
      payload.value(QStringLiteral("session_id")).toString();
  const QString verb = payload.value(QStringLiteral("verb")).toString();
  BackgroundPrompt* prompt =
      UpsertBackgroundPrompt(prompts, task_id, session_id, verb, QString(),
                             now_ms, limit);
  if (prompt == nullptr) {
    return nullptr;
  }
  const QString text = payload.value(QStringLiteral("text")).toString();
  QString failure = payload.value(QStringLiteral("error")).toString().trimmed();
  if (failure.isEmpty() && text.startsWith(QStringLiteral("error: "))) {
    failure = text.mid(7).trimmed();
  }
  if (!failure.isEmpty()) {
    prompt->error = failure;
    prompt->output.clear();
  } else {
    prompt->output = text;
    prompt->error.clear();
  }
  prompt->complete = true;
  prompt->completed_ms = now_ms;
  return prompt;
}

QString ReactionSummary(const QJsonArray& reactions) {
  QStringList labels;
  std::vector<int> counts;
  for (const ChatReaction& reaction : ParseChatReactions(reactions)) {
    const auto it = std::find(labels.begin(), labels.end(), reaction.emoji);
    if (it == labels.end()) {
      labels.push_back(reaction.emoji);
      counts.push_back(1);
    } else {
      ++counts[static_cast<std::size_t>(it - labels.begin())];
    }
  }
  QStringList parts;
  for (int index = 0; index < labels.size(); ++index) {
    parts.push_back(QStringLiteral("%1 %2")
                        .arg(labels.at(index))
                        .arg(counts[static_cast<std::size_t>(index)]));
  }
  return parts.join(QStringLiteral(" · "));
}

}  // namespace hermes::ui
