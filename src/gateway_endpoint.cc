// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_endpoint.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTextStream>

namespace hermes::ui {
namespace {

constexpr int kControlTimeoutMs = 3000;

bool ReadControlReply(QLocalSocket* socket, QJsonObject* result,
                      QString* error) {
  QByteArray buffer;
  QElapsedTimer timer;
  timer.start();
  while (!buffer.contains('\n') && timer.elapsed() < kControlTimeoutMs) {
    if (socket->bytesAvailable() > 0) {
      buffer += socket->readAll();
      continue;
    }
    if (!socket->waitForReadyRead(100) &&
        socket->state() != QLocalSocket::ConnectedState) {
      break;
    }
  }
  const int newline = buffer.indexOf('\n');
  if (newline < 0) {
    *error = QStringLiteral("gateway control socket returned no reply");
    return false;
  }
  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(buffer.left(newline), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    *error = QStringLiteral("gateway control reply is not JSON");
    return false;
  }
  const QJsonObject response = document.object();
  if (!response.value(QStringLiteral("ok")).toBool()) {
    *error = response.value(QStringLiteral("error"))
                 .toString(QStringLiteral("gateway control refused"));
    return false;
  }
  *result = response.value(QStringLiteral("result")).toObject();
  return true;
}

}  // namespace

QString HermesHome() {
  const QString override = qEnvironmentVariable("HERMES_HOME");
  if (!override.isEmpty()) {
    return QDir::cleanPath(override);
  }
  return QDir::cleanPath(QDir::homePath() + QStringLiteral("/.hermes"));
}

QString ProfileHome(const QString& profile_name) {
  const QString home = HermesHome();
  if (profile_name.isEmpty() || profile_name == QStringLiteral("default")) {
    return home;
  }
  return QDir::cleanPath(home + QStringLiteral("/profiles/") + profile_name);
}

QString ControlSocketPath(const QString& home, QString* error) {
  const QString direct = QDir(home).filePath(QStringLiteral("gateway.sock"));
  if (QFile::exists(direct)) {
    return direct;
  }
  const QString pointer =
      QDir(home).filePath(QStringLiteral("gateway.sock.path"));
  QFile file(pointer);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error != nullptr) {
      *error = QStringLiteral("Hermes gateway control socket not found at %1")
                   .arg(direct);
    }
    return QString();
  }
  const QString path = QString::fromUtf8(file.readLine()).trimmed();
  if (path.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("gateway.sock.path is empty");
    }
    return QString();
  }
  return path;
}

bool QueryControl(const QString& home, const QJsonObject& request,
                  QJsonObject* result, QString* error) {
  QString path_error;
  const QString path = ControlSocketPath(home, &path_error);
  if (path.isEmpty()) {
    if (error != nullptr) {
      *error = path_error;
    }
    return false;
  }

  QLocalSocket socket;
  socket.connectToServer(path);
  if (!socket.waitForConnected(kControlTimeoutMs)) {
    if (error != nullptr) {
      *error = QStringLiteral("cannot connect to gateway control socket: %1")
                   .arg(socket.errorString());
    }
    return false;
  }

  const QByteArray payload =
      QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
  socket.write(payload);
  if (!socket.waitForBytesWritten(kControlTimeoutMs)) {
    if (error != nullptr) {
      *error = QStringLiteral("cannot write to gateway control socket");
    }
    return false;
  }

  const bool ok = ReadControlReply(&socket, result, error);
  socket.disconnectFromServer();
  return ok;
}

bool DiscoverEndpoint(GatewayEndpoint* endpoint, QString* error) {
  QJsonObject request;
  request.insert(QStringLiteral("protocol"), 1);
  request.insert(QStringLiteral("verb"), QStringLiteral("identify"));
  request.insert(QStringLiteral("id"), 1);

  QJsonObject result;
  if (!QueryControl(HermesHome(), request, &result, error)) {
    return false;
  }

  // `identify` answers the endpoint directly on current gateways; older
  // builds nest it under `endpoint`. Accept both.
  const QJsonObject data =
      result.contains(QStringLiteral("endpoint"))
          ? result.value(QStringLiteral("endpoint")).toObject()
          : result;
  endpoint->api_origin = data.value(QStringLiteral("api_origin")).toString();
  endpoint->instance_id = data.value(QStringLiteral("instance_id")).toString();
  endpoint->profile_id = data.value(QStringLiteral("profile_id")).toString();
  endpoint->code_sha = data.value(QStringLiteral("code_sha")).toString();
  endpoint->runtime_protocol =
      data.value(QStringLiteral("runtime_protocol")).toInt();
  endpoint->served_profiles.clear();
  const QJsonArray served =
      data.value(QStringLiteral("served_profiles")).toArray();
  for (const QJsonValue& value : served) {
    QString id;
    if (value.isObject()) {
      const QJsonObject row = value.toObject();
      id = row.value(QStringLiteral("profile_id")).toString();
      if (id.isEmpty()) {
        id = row.value(QStringLiteral("home")).toString();
      }
    } else {
      id = value.toString();
    }
    if (!id.isEmpty()) {
      endpoint->served_profiles.push_back(id);
    }
  }
  if (!endpoint->valid()) {
    if (error != nullptr) {
      *error = QStringLiteral("gateway identify returned no endpoint");
    }
    return false;
  }
  return true;
}

bool IsStaleEndpointError(const QString& error) {
  return error.contains(QStringLiteral("stale runtime instance")) ||
         error.contains(QStringLiteral("Gateway bootstrap identity changed"));
}

bool RequestTicket(const QString& home, const QString& profile_name,
                   const GatewayEndpoint& endpoint, const QString& purpose,
                   QString* ticket, QString* error) {
  QJsonObject params;
  params.insert(QStringLiteral("profile_id"), ProfileHome(profile_name));
  params.insert(QStringLiteral("instance_id"), endpoint.instance_id);
  params.insert(QStringLiteral("purpose"), purpose);

  QJsonObject request;
  request.insert(QStringLiteral("protocol"), 1);
  request.insert(QStringLiteral("verb"), QStringLiteral("session-ticket"));
  request.insert(QStringLiteral("id"), 1);
  request.insert(QStringLiteral("params"), params);

  QJsonObject result;
  if (!QueryControl(home, request, &result, error)) {
    return false;
  }
  *ticket = result.value(QStringLiteral("ticket")).toString();
  if ((*ticket).isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("gateway issued an empty ticket");
    }
    return false;
  }
  return true;
}

}  // namespace hermes::ui
