// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_INTERRUPT_RELAY_H_
#define HERMES_UI_INTERRUPT_RELAY_H_

#include <QObject>
#include <QString>
#include <optional>

#include "src/gateway_endpoint.h"

namespace hermes::ui {

class GatewaySocket;

// Dedicated stop path for one profile. The profile worker thread can be
// blocked in a synchronous REST request (sessions, settings pages, a model
// catalog refresh) for up to the HTTP timeout; queueing Stop there makes it
// feel dead. This relay owns a second GatewaySocket on its own thread so the
// `session.interrupt` + `process.stop` pair leaves immediately even while the
// worker is busy.
//
// It is created lazily for the active profile and queues the newest request
// until the socket connects. A pending request expires quickly so a reconnect
// can never stop a later turn the user did not ask to cancel.
class InterruptRelay : public QObject {
  Q_OBJECT

 public:
  InterruptRelay(QString home, QString profile_name, GatewayEndpoint endpoint,
                 QObject* parent = nullptr);
  ~InterruptRelay() override;

  static constexpr qint64 kPendingLifetimeMs = 10000;

 public slots:
  void Start();
  void Shutdown();
  // Turn-only interrupt, retained for callers that must not kill processes.
  void Interrupt(const QString& session_id, int execution_generation);
  // Full Desktop `/stop`: interrupt the turn, then kill this chat's processes.
  void Stop(const QString& session_id, int execution_generation);

 private slots:
  void OnConnected(const QString& subprotocol);
  void OnSocketError(const QString& message);

 private:
  struct PendingStop {
    QString session_id;
    int execution_generation = -1;
    qint64 queued_at_ms = 0;
    bool stop_processes = false;
  };

  void Queue(PendingStop request);
  void Send(const PendingStop& request);

  QString home_;
  QString profile_name_;
  GatewayEndpoint endpoint_;
  GatewaySocket* socket_ = nullptr;
  std::optional<PendingStop> pending_;
  bool error_logged_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_INTERRUPT_RELAY_H_
