// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_protocol.h"

#include <QStringList>

namespace hermes::ui {
namespace {

const QStringList& MetadataFields() {
  static const QStringList fields{
      QStringLiteral("title"), QStringLiteral("archived"),
      QStringLiteral("hidden"), QStringLiteral("pinned"),
      QStringLiteral("unread")};
  return fields;
}

void AddProfile(QJsonObject* params, const QString& profile) {
  if (params != nullptr && !profile.isEmpty()) {
    params->insert(QStringLiteral("profile"), profile);
  }
}

}  // namespace

QJsonObject BuildSessionCreateParams(const QString& profile,
                                     const QString& request_id,
                                     const QString& cwd) {
  QJsonObject params{{QStringLiteral("source"), QStringLiteral("gui")}};
  if (!request_id.isEmpty()) {
    params.insert(QStringLiteral("request_id"), request_id);
  }
  if (!cwd.trimmed().isEmpty()) {
    params.insert(QStringLiteral("cwd"), cwd);
    params.insert(QStringLiteral("cwd_explicit"), true);
  }
  AddProfile(&params, profile);
  return params;
}

QJsonObject BuildSessionMutateParams(const QString& session_id,
                                     const QString& request_id,
                                     int expected_revision,
                                     const QString& operation,
                                     const QJsonObject& payload,
                                     int expected_generation) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("request_id"), request_id);
  params.insert(QStringLiteral("expected_revision"), expected_revision);
  params.insert(QStringLiteral("operation"), operation);
  params.insert(QStringLiteral("payload"), payload);
  if (expected_generation >= 0) {
    params.insert(QStringLiteral("expected_generation"), expected_generation);
  }
  return params;
}

SessionMutationSpec SessionMutationForPatch(const QJsonObject& patch) {
  SessionMutationSpec spec;
  QStringList metadata_keys;
  for (const QString& field : MetadataFields()) {
    if (patch.contains(field)) {
      metadata_keys.push_back(field);
    }
  }
  if (metadata_keys.size() == 1 &&
      metadata_keys.first() == QStringLiteral("title")) {
    spec.operation = QStringLiteral("rename");
    spec.payload.insert(QStringLiteral("title"),
                        patch.value(QStringLiteral("title")));
    return spec;
  }
  if (metadata_keys.size() == 1 &&
      metadata_keys.first() == QStringLiteral("archived")) {
    spec.operation = QStringLiteral("archive");
    spec.payload.insert(QStringLiteral("archived"),
                        patch.value(QStringLiteral("archived")));
    return spec;
  }
  spec.operation = QStringLiteral("sidebar");
  for (const QString& field : metadata_keys) {
    spec.payload.insert(field, patch.value(field));
  }
  return spec;
}

QJsonObject BuildSessionEventsSinceParams(const QString& session_id,
                                          const QString& profile,
                                          int last_seen) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("last_seen"), last_seen);
  AddProfile(&params, profile);
  return params;
}

QJsonObject BuildPromptReceiptParams(const QString& session_id,
                                     const QString& admission_id) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("admission_id"), admission_id);
  params.insert(QStringLiteral("include_result"), true);
  return params;
}

std::vector<PendingAdmission> ParsePendingAdmissions(const QJsonArray& rows) {
  std::vector<PendingAdmission> pending;
  pending.reserve(static_cast<std::size_t>(rows.size()));
  for (const QJsonValue& value : rows) {
    const QJsonObject row = value.toObject();
    if (row.isEmpty()) {
      continue;
    }
    PendingAdmission admission;
    admission.admission_id =
        row.value(QStringLiteral("admission_id")).toString();
    if (admission.admission_id.isEmpty()) {
      admission.admission_id = row.value(QStringLiteral("id")).toString();
    }
    admission.input_id = row.value(QStringLiteral("input_id")).toString();
    admission.text = row.value(QStringLiteral("text")).toString();
    admission.status =
        row.value(QStringLiteral("status")).toString(QStringLiteral("queued"));
    admission.outcome = row.value(QStringLiteral("outcome")).toString();
    for (const QJsonValue& control :
         row.value(QStringLiteral("controls")).toArray()) {
      const QString name = control.toString();
      if (!name.isEmpty()) {
        admission.controls.push_back(name);
      }
    }
    admission.steering = row.value(QStringLiteral("steering")).toBool();
    admission.sequence = row.value(QStringLiteral("sequence")).toInt();
    admission.authority_epoch =
        row.value(QStringLiteral("authority_epoch")).toInt();
    admission.execution_generation =
        row.value(QStringLiteral("execution_generation")).toInt(-1);
    pending.push_back(std::move(admission));
  }
  return pending;
}

bool PendingSnapshotWins(int authority_epoch, int revision, int known_epoch,
                         int known_revision) {
  if (known_revision < 0) {
    return true;
  }
  if (authority_epoch != known_epoch) {
    return true;
  }
  // A missing revision cannot fence an older snapshot; accept it rather than
  // pinning a stale queue forever.
  if (revision <= 0) {
    return true;
  }
  return revision >= known_revision;
}

QJsonObject BuildPromptCancelParams(const QString& session_id,
                                    const QString& admission_id,
                                    const QString& profile) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("admission_id"), admission_id);
  AddProfile(&params, profile);
  return params;
}

QJsonObject BuildPromptSteerQueuedParams(const QString& session_id,
                                         const QString& admission_id,
                                         const QString& profile) {
  return BuildPromptCancelParams(session_id, admission_id, profile);
}

QJsonObject BuildPromptReplaceQueuedParams(const QString& session_id,
                                           const QString& admission_id,
                                           const QString& submission_id,
                                           const QString& text,
                                           const QString& profile) {
  QJsonObject params =
      BuildPromptCancelParams(session_id, admission_id, profile);
  params.insert(QStringLiteral("submission_id"), submission_id);
  params.insert(QStringLiteral("text"), text);
  return params;
}

QJsonObject BuildPromptResolveUnknownParams(const QString& session_id,
                                            const QString& admission_id,
                                            int execution_generation,
                                            const QString& profile) {
  QJsonObject params =
      BuildPromptCancelParams(session_id, admission_id, profile);
  params.insert(QStringLiteral("execution_generation"), execution_generation);
  return params;
}

ConnectionOperation ParseConnectionOperation(const QJsonObject& payload) {
  ConnectionOperation operation;
  operation.op_id = payload.value(QStringLiteral("op_id")).toString();
  operation.tool_call_id =
      payload.value(QStringLiteral("tool_call_id")).toString();
  operation.owner_type = payload.value(QStringLiteral("owner"))
                             .toObject()
                             .value(QStringLiteral("type"))
                             .toString();
  operation.seq = payload.value(QStringLiteral("seq")).toInt();
  operation.deadline_at =
      payload.value(QStringLiteral("deadline_at")).toDouble();
  operation.settled = payload.value(QStringLiteral("settled")).toBool();
  operation.settled_by = payload.value(QStringLiteral("settled_by")).toString();
  const QJsonObject display =
      payload.value(QStringLiteral("display")).toObject();
  for (const QJsonValue& value :
       display.value(QStringLiteral("suggested")).toArray()) {
    const QString slug = value.toString();
    if (!slug.isEmpty()) {
      operation.suggested.push_back(slug);
    }
  }
  for (const QJsonValue& value :
       payload.value(QStringLiteral("targets")).toArray()) {
    const QJsonObject row = value.toObject();
    ConnectionTarget target;
    target.name = row.value(QStringLiteral("name")).toString();
    if (target.name.isEmpty()) {
      continue;
    }
    target.kind =
        row.value(QStringLiteral("kind")).toString(QStringLiteral("mcp"));
    target.action =
        row.value(QStringLiteral("action")).toString(QStringLiteral("install"));
    target.state =
        row.value(QStringLiteral("state")).toString(QStringLiteral("pending"));
    target.detail = row.value(QStringLiteral("detail")).toString();
    target.connect_url = row.value(QStringLiteral("connect_url")).toString();
    target.instructions = row.value(QStringLiteral("instructions")).toString();
    target.discovery_error =
        row.value(QStringLiteral("discovery_error")).toString();
    target.connection_id =
        row.value(QStringLiteral("connection_id")).toString();
    target.alias = row.value(QStringLiteral("alias")).toString();
    target.resolved = row.value(QStringLiteral("resolved")).toBool();
    for (const QJsonValue& tool :
         row.value(QStringLiteral("tools")).toArray()) {
      const QString name = tool.toString();
      if (!name.isEmpty()) {
        target.tools.push_back(name);
      }
    }
    for (const QJsonValue& field :
         row.value(QStringLiteral("required_env")).toArray()) {
      const QJsonObject entry = field.toObject();
      ConnectionEnvField env;
      env.name = entry.value(QStringLiteral("name")).toString();
      if (env.name.isEmpty()) {
        continue;
      }
      env.prompt = entry.value(QStringLiteral("prompt")).toString();
      env.default_value = entry.value(QStringLiteral("default")).toString();
      env.required = entry.value(QStringLiteral("required")).toBool();
      env.secret = entry.value(QStringLiteral("secret")).toBool();
      target.required_env.push_back(std::move(env));
    }
    operation.targets.push_back(std::move(target));
  }
  return operation;
}

QJsonObject BuildConnectionRespondParams(const QString& op_id,
                                         const QString& session_id,
                                         const QJsonObject& result,
                                         const QString& profile) {
  QJsonObject params;
  params.insert(QStringLiteral("op_id"), op_id);
  params.insert(
      QStringLiteral("owner"),
      QJsonObject{{QStringLiteral("session_id"), session_id},
                  {QStringLiteral("type"), QStringLiteral("session")}});
  params.insert(QStringLiteral("result"), result);
  AddProfile(&params, profile);
  return params;
}

QString NormalizeHandoffPlatform(const QString& args) {
  const QString trimmed = args.trimmed();
  if (trimmed.isEmpty()) {
    return QString();
  }
  return trimmed.section(QLatin1Char(' '), 0, 0).trimmed().toLower();
}

QString HandoffStatusText(const QString& state, const QString& platform,
                          const QString& error) {
  const QString target =
      platform.isEmpty() ? QStringLiteral("the platform") : platform;
  if (state == QStringLiteral("pending")) {
    return QStringLiteral("Handoff to %1 queued…").arg(target);
  }
  if (state == QStringLiteral("running")) {
    return QStringLiteral("Handoff to %1 in progress…").arg(target);
  }
  if (state == QStringLiteral("completed")) {
    return QStringLiteral("Handoff to %1 complete").arg(target);
  }
  if (state == QStringLiteral("failed")) {
    return error.isEmpty()
               ? QStringLiteral("Handoff to %1 failed").arg(target)
               : QStringLiteral("Handoff to %1 failed: %2").arg(target, error);
  }
  return QStringLiteral("No handoff pending");
}

}  // namespace hermes::ui
