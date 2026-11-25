// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_INTERRUPT_RELAY_H_
#define HERMES_UI_INTERRUPT_RELAY_H_

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <optional>

#include "src/gateway_endpoint.h"

class QTimer;

namespace hermes::ui {

class GatewaySocket;

// Dedicated stop path for one profile. The profile worker thread can be
// blocked in a synchronous REST request (sessions, settings pages, a model
// catalog refresh) for up to the HTTP timeout; queueing Stop there makes it
// feel dead. This relay owns a second GatewaySocket on its own thread so the
// `session.interrupt` + `process.stop` pair leaves immediately even while the
// worker is busy.
//
// A cached execution generation goes stale when the event stream was detached
// or reaped. A full Stop therefore asks `session.resume` for the live claim
// identity first and retries the interrupt with that generation. The resume is
// in flight alongside the best-effort cached interrupt, so the common case is
// still immediate; `process.stop` follows the resolved live id.
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

  // GatewaySocket's reconnect backoff reaches 15 s, so a stop waiting for a
  // fresh socket must outlive one backoff cycle instead of silently expiring.
  static constexpr qint64 kPendingLifetimeMs = 20000;
  static constexpr int kResumeDeadlineMs = 1200;

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
  void OnResponse(int id, const QJsonObject& result, const QString& error);
  void OnResumeDeadline();

 private:
  struct PendingStop {
    QString session_id;
    int execution_generation = -1;
    qint64 queued_at_ms = 0;
    bool stop_processes = false;
  };

  struct ActiveStop {
    QString session_id;
    QString live_session_id;
    int cached_generation = -1;
    int resume_request = -1;
    int interrupt_request = -1;
    int process_request = -1;
    int interrupted_generation = -1;
    bool resume_done = false;
    bool process_sent = false;
  };

  void Queue(PendingStop request);
  void Begin(const PendingStop& request);
  void SendInterrupt(const QString& session_id, int execution_generation);
  void SendProcessStop(const QString& session_id);
  void Finish();

  QString home_;
  QString profile_name_;
  GatewayEndpoint endpoint_;
  GatewaySocket* socket_ = nullptr;
  QTimer* resume_deadline_ = nullptr;
  std::optional<PendingStop> pending_;
  std::optional<ActiveStop> active_;
  bool error_logged_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_INTERRUPT_RELAY_H_
