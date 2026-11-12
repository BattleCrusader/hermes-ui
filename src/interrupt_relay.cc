// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/interrupt_relay.h"

#include <QDateTime>
#include <QDebug>
#include <utility>

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
  if (session_id.isEmpty()) {
    return;
  }
  PendingInterrupt request{session_id, execution_generation,
                           QDateTime::currentMSecsSinceEpoch()};
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
  // it either connects or expires. One line per failure; the backoff repeats
  // the same message, so do not turn this into a warning stream.
  qInfo("interrupt relay profile=%s: %s", qUtf8Printable(profile_name_),
        qUtf8Printable(message));
}

void InterruptRelay::Send(const PendingInterrupt& request) {
  if (socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }
  socket_->InterruptSession(request.session_id, request.execution_generation);
}

}  // namespace hermes::ui
