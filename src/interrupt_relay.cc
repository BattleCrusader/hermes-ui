// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/interrupt_relay.h"

#include <QDateTime>
#include <QDebug>
#include <utility>

#include "src/gateway_protocol.h"
#include "src/gateway_socket.h"

namespace hermes::ui {

InterruptRelay::InterruptRelay(QString home, QString profile_name,
                               GatewayEndpoint endpoint, QObject* parent)
    : QObject(parent),
      home_(std::move(home)),
      profile_name_(std::move(profile_name)),
      endpoint_(std::move(endpoint)) {}

InterruptRelay::~InterruptRelay() { Shutdown(); }

void InterruptRelay::Start() {
  if (socket_ != nullptr) {
    return;
  }
  // Construct the socket here, on the relay thread. A QWebSocket value member
  // cannot be moved safely with its owner across thread affinity boundaries.
  socket_ = new GatewaySocket(home_, profile_name_, endpoint_, this, false);
  connect(socket_, &GatewaySocket::Connected, this,
          &InterruptRelay::OnConnected);
  connect(socket_, &GatewaySocket::SocketError, this,
          &InterruptRelay::OnSocketError);
  socket_->Connect();
}

void InterruptRelay::Shutdown() {
  pending_.reset();
  if (socket_ != nullptr) {
    socket_->Close();
    delete socket_;
    socket_ = nullptr;
  }
}

void InterruptRelay::Interrupt(const QString& session_id,
                               int execution_generation) {
  Queue(PendingStop{session_id, execution_generation,
                    QDateTime::currentMSecsSinceEpoch(), false});
}

void InterruptRelay::Stop(const QString& session_id,
                          int execution_generation) {
  Queue(PendingStop{session_id, execution_generation,
                    QDateTime::currentMSecsSinceEpoch(), true});
}

void InterruptRelay::Queue(PendingStop request) {
  if (request.session_id.isEmpty()) {
    return;
  }
  if (socket_ != nullptr && socket_->IsConnected()) {
    Send(request);
    return;
  }
  pending_ = std::move(request);
  if (socket_ == nullptr) {
    Start();
  }
}

void InterruptRelay::OnConnected(const QString& subprotocol) {
  Q_UNUSED(subprotocol);
  error_logged_ = false;
  if (!pending_.has_value()) {
    return;
  }
  const qint64 age =
      QDateTime::currentMSecsSinceEpoch() - pending_->queued_at_ms;
  if (age > kPendingLifetimeMs) {
    pending_.reset();
    return;
  }
  Send(*pending_);
  pending_.reset();
}

void InterruptRelay::OnSocketError(const QString& message) {
  // GatewaySocket retries on its own. The pending interrupt stays queued until
  // it either connects or expires. Log only the first failure in a backoff
  // run; a rejected profile otherwise repeats the same line forever.
  if (error_logged_) {
    return;
  }
  error_logged_ = true;
  qInfo("interrupt relay profile=%s: %s", qUtf8Printable(profile_name_),
        qUtf8Printable(message));
}

void InterruptRelay::Send(const PendingStop& request) {
  if (socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }
  if (!request.stop_processes) {
    socket_->InterruptSession(request.session_id,
                              request.execution_generation);
    return;
  }
  for (const GatewayCall& call : BuildSessionStopCalls(
           request.session_id, request.execution_generation)) {
    socket_->RequestRpc(call.method, call.params);
  }
}

}  // namespace hermes::ui
