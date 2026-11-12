// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_HTTP_H_
#define HERMES_UI_GATEWAY_HTTP_H_

#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QUrl>

#include "src/gateway_endpoint.h"

class QNetworkAccessManager;
class QNetworkReply;

namespace hermes::ui {

// Synchronous-looking JSON client for a profile worker. The manager and the
// nested event loop live on the worker thread, never the UI thread.
class GatewayHttp {
 public:
  GatewayHttp(QString home, QString profile_name, GatewayEndpoint endpoint);
  ~GatewayHttp();

  GatewayHttp(const GatewayHttp&) = delete;
  GatewayHttp& operator=(const GatewayHttp&) = delete;

  // GET/PUT cover the config and session read/write paths.
  bool GetJson(const QString& path, QJsonDocument* document, QString* error,
               bool authenticate = true, int timeout_ms = 0);
  bool PutJson(const QString& path, const QJsonObject& body,
               QJsonDocument* document, QString* error,
               bool authenticate = true);

  // Every other REST verb the desktop uses. `body` is optional for DELETE,
  // which some gateway routes read as a JSON body (DELETE /api/env).
  bool PostJson(const QString& path, const QJsonObject& body,
                QJsonDocument* document, QString* error,
                bool authenticate = true);
  bool PatchJson(const QString& path, const QJsonObject& body,
                 QJsonDocument* document, QString* error,
                 bool authenticate = true);
  bool DeleteJson(const QString& path, const QJsonObject& body,
                  QJsonDocument* document, QString* error,
                  bool authenticate = true);

  // Multipart upload for the chat attachment routes. `uploaded_path` receives
  // the gateway-visible file path the prompt should reference.
  bool PostFile(const QString& path, const QString& file_path,
                QString* uploaded_path, QString* error);
  // Multipart upload with extra non-file form fields (the managed-file
  // upload-stream route needs a target `path`).
  bool PostMultipart(const QString& path, const QString& file_path,
                     const QJsonObject& fields, QString* uploaded_path,
                     QString* error);
  // Streams a workspace file to `local_path`, replacing it atomically.
  bool DownloadFile(const QString& path, const QString& local_path,
                    QString* error);

  // Generic escape hatch: `method` is an HTTP verb ("PUT", "PATCH", ...).
  // Returns false and fills *error for a non-2xx status or a transport error.
  bool RequestJson(const QString& method, const QString& path,
                   const QJsonObject& body, QJsonDocument* document,
                   QString* error, bool authenticate = true,
                   int timeout_ms = 0);

  // Aborts the in-flight request. Called from the worker's Shutdown while the
  // request's nested event loop may still be running.
  void Abort();

 private:
  struct Result {
    bool ok = false;
    int status = 0;
    QString error;
    QJsonDocument document;
    QByteArray data;
  };

  Result Perform(const QByteArray& method, const QUrl& url,
                 const QString& ticket, const QByteArray& body,
                 const QByteArray& content_type = QByteArray(),
                 bool parse_json = true, int timeout_ms = 0);

  // Mints a native-http ticket. A gateway restart replaces the endpoint's
  // instance id while keeping its API origin, so retry once after discovering
  // the replacement instead of failing every request until the app restarts.
  bool MintTicket(QString* ticket, QString* error);

  QString home_;
  QString profile_name_;
  GatewayEndpoint endpoint_;
  QNetworkAccessManager* manager_ = nullptr;
  QNetworkReply* active_reply_ = nullptr;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_HTTP_H_
