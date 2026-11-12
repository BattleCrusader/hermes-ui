// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_http.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <utility>

namespace hermes::ui {
namespace {

constexpr int kRequestTimeoutMs = 20000;

QUrl ScopedUrl(const GatewayEndpoint& endpoint, const QString& profile_name,
               const QString& path) {
  QUrl url(endpoint.api_origin + path);
  QUrlQuery query(url);
  query.addQueryItem(QStringLiteral("profile"), profile_name);
  url.setQuery(query);
  return url;
}

}  // namespace

GatewayHttp::GatewayHttp(QString home, QString profile_name,
                         GatewayEndpoint endpoint)
    : home_(std::move(home)),
      profile_name_(std::move(profile_name)),
      endpoint_(std::move(endpoint)) {
  manager_ = new QNetworkAccessManager();
}

GatewayHttp::~GatewayHttp() {
  Abort();
  delete manager_;
}

void GatewayHttp::Abort() {
  if (active_reply_ != nullptr) {
    active_reply_->abort();
  }
}

GatewayHttp::Result GatewayHttp::Perform(const QByteArray& method,
                                         const QUrl& url, const QString& ticket,
                                         const QByteArray& body,
                                         const QByteArray& content_type,
                                         bool parse_json) {
  Result result;
  QNetworkRequest request(url);
  request.setRawHeader("Accept", "application/json");
  if (!ticket.isEmpty()) {
    request.setRawHeader("X-Hermes-Gateway-Ticket", ticket.toUtf8());
  }
  if (!content_type.isEmpty()) {
    request.setHeader(QNetworkRequest::ContentTypeHeader, content_type);
  } else if (!body.isEmpty()) {
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
  }

  QNetworkReply* reply = nullptr;
  if (method == "GET") {
    reply = manager_->get(request);
  } else if (method == "PUT") {
    reply = manager_->put(request, body);
  } else if (method == "POST") {
    reply = manager_->post(request, body);
  } else {
    reply = manager_->sendCustomRequest(request, method, body);
  }
  active_reply_ = reply;
  QEventLoop loop;
  QTimer timeout;
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  timeout.start(kRequestTimeoutMs);
  loop.exec();
  if (active_reply_ == reply) {
    active_reply_ = nullptr;
  }

  if (!reply->isFinished()) {
    reply->abort();
    reply->deleteLater();
    result.error = QStringLiteral("gateway request timed out");
    return result;
  }

  // Shutdown aborts the in-flight request from the worker's nested event loop.
  // Reading an aborted reply logs "device not open"; the cancellation is
  // already the result we want.
  if (reply->error() == QNetworkReply::OperationCanceledError) {
    reply->deleteLater();
    result.error = QStringLiteral("gateway request canceled");
    return result;
  }

  result.status =
      reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  QByteArray response;
  if (reply->isReadable()) {
    response = reply->readAll();
  }
  const QString network_error = reply->errorString();
  reply->deleteLater();

  if (result.status == 0) {
    result.error = network_error.isEmpty()
                       ? QStringLiteral("gateway request failed")
                       : network_error;
    return result;
  }
  if (result.status < 200 || result.status >= 300) {
    result.error = QStringLiteral("gateway HTTP %1: %2")
                       .arg(result.status)
                       .arg(QString::fromUtf8(response.left(240)));
    return result;
  }

  result.data = response;
  if (!parse_json) {
    result.ok = true;
    return result;
  }

  // 204/205 and a few DELETE routes answer with no body; that is still a
  // successful mutation, so an empty payload is only a parse failure when
  // something was actually written.
  if (!response.trimmed().isEmpty()) {
    QJsonParseError parse_error;
    result.document = QJsonDocument::fromJson(response, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
      result.error = QStringLiteral("gateway returned invalid JSON");
      return result;
    }
  }
  result.ok = true;
  return result;
}

bool GatewayHttp::PostFile(const QString& path, const QString& file_path,
                           QString* uploaded_path, QString* error) {
  return PostMultipart(path, file_path, QJsonObject(), uploaded_path, error);
}

bool GatewayHttp::PostMultipart(const QString& path, const QString& file_path,
                                const QJsonObject& fields,
                                QString* uploaded_path, QString* error) {
  constexpr qint64 kMaxUploadBytes = 64LL * 1024 * 1024;
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = QStringLiteral("cannot read %1").arg(file_path);
    }
    return false;
  }
  if (file.size() > kMaxUploadBytes) {
    if (error != nullptr) {
      *error = QStringLiteral("file is larger than 64 MiB");
    }
    return false;
  }
  const QByteArray data = file.readAll();
  file.close();

  const QByteArray boundary =
      "----HermesNative" +
      QByteArray::number(QDateTime::currentMSecsSinceEpoch());
  const QString name = QFileInfo(file_path).fileName();
  QByteArray body;
  for (auto it = fields.constBegin(); it != fields.constEnd(); ++it) {
    const QString value = it.value().isBool()
                              ? (it.value().toBool() ? QStringLiteral("true")
                                                     : QStringLiteral("false"))
                              : it.value().toVariant().toString();
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + it.key().toUtf8() +
            "\"\r\n\r\n";
    body += value.toUtf8() + "\r\n";
  }
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; filename=\"" +
          name.toUtf8() + "\"\r\n";
  body += "Content-Type: application/octet-stream\r\n\r\n";
  body += data;
  body += "\r\n--" + boundary + "--\r\n";
  const QByteArray content_type = "multipart/form-data; boundary=" + boundary;

  const QUrl url = ScopedUrl(endpoint_, profile_name_, path);
  Result last;
  for (int attempt = 0; attempt < 2; ++attempt) {
    QString ticket;
    QString ticket_error;
    if (!RequestTicket(home_, profile_name_, endpoint_,
                       QStringLiteral("native-http"), &ticket, &ticket_error)) {
      if (error != nullptr) {
        *error = ticket_error;
      }
      return false;
    }
    last = Perform("POST", url, ticket, body, content_type);
    if (last.ok) {
      if (uploaded_path != nullptr) {
        *uploaded_path =
            last.document.object().value(QStringLiteral("path")).toString();
      }
      return true;
    }
    if (last.status != 401) {
      break;
    }
  }
  if (error != nullptr) {
    *error = last.error;
  }
  return false;
}

bool GatewayHttp::DownloadFile(const QString& path, const QString& local_path,
                               QString* error) {
  const QUrl url = ScopedUrl(endpoint_, profile_name_, path);
  Result last;
  for (int attempt = 0; attempt < 2; ++attempt) {
    QString ticket;
    QString ticket_error;
    if (!RequestTicket(home_, profile_name_, endpoint_,
                       QStringLiteral("native-http"), &ticket, &ticket_error)) {
      if (error != nullptr) {
        *error = ticket_error;
      }
      return false;
    }
    last = Perform("GET", url, ticket, QByteArray(), QByteArray(), false);
    if (last.ok) {
      QSaveFile file(local_path);
      if (!file.open(QIODevice::WriteOnly) || file.write(last.data) < 0 ||
          !file.commit()) {
        if (error != nullptr) {
          *error = QStringLiteral("cannot write %1").arg(local_path);
        }
        return false;
      }
      return true;
    }
    if (last.status != 401) {
      break;
    }
  }
  if (error != nullptr) {
    *error = last.error;
  }
  return false;
}

bool GatewayHttp::RequestJson(const QString& method, const QString& path,
                              const QJsonObject& body, QJsonDocument* document,
                              QString* error, bool authenticate) {
  const QUrl url = ScopedUrl(endpoint_, profile_name_, path);
  const QByteArray payload =
      body.isEmpty() && method == QLatin1String("GET")
          ? QByteArray()
          : QJsonDocument(body).toJson(QJsonDocument::Compact);
  const QByteArray verb = method.toUpper().toLatin1();

  const int attempts = authenticate ? 2 : 1;
  Result last;
  for (int attempt = 0; attempt < attempts; ++attempt) {
    QString ticket;
    if (authenticate) {
      QString ticket_error;
      if (!RequestTicket(home_, profile_name_, endpoint_,
                         QStringLiteral("native-http"), &ticket,
                         &ticket_error)) {
        if (error != nullptr) {
          *error = ticket_error;
        }
        return false;
      }
    }
    last = Perform(verb, url, ticket, payload);
    if (last.ok) {
      if (document != nullptr) {
        *document = last.document;
      }
      return true;
    }
    // A ticket is one-use. A 401 on the first attempt means this one was
    // already spent or the instance moved; mint again once before failing.
    if (last.status != 401) {
      break;
    }
  }

  if (error != nullptr) {
    *error = last.error;
  }
  return false;
}

bool GatewayHttp::GetJson(const QString& path, QJsonDocument* document,
                          QString* error, bool authenticate) {
  return RequestJson(QStringLiteral("GET"), path, QJsonObject(), document,
                     error, authenticate);
}

bool GatewayHttp::PutJson(const QString& path, const QJsonObject& body,
                          QJsonDocument* document, QString* error,
                          bool authenticate) {
  return RequestJson(QStringLiteral("PUT"), path, body, document, error,
                     authenticate);
}

bool GatewayHttp::PostJson(const QString& path, const QJsonObject& body,
                           QJsonDocument* document, QString* error,
                           bool authenticate) {
  return RequestJson(QStringLiteral("POST"), path, body, document, error,
                     authenticate);
}

bool GatewayHttp::PatchJson(const QString& path, const QJsonObject& body,
                            QJsonDocument* document, QString* error,
                            bool authenticate) {
  return RequestJson(QStringLiteral("PATCH"), path, body, document, error,
                     authenticate);
}

bool GatewayHttp::DeleteJson(const QString& path, const QJsonObject& body,
                             QJsonDocument* document, QString* error,
                             bool authenticate) {
  return RequestJson(QStringLiteral("DELETE"), path, body, document, error,
                     authenticate);
}

}  // namespace hermes::ui
