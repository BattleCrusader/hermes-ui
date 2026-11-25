// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/interrupt_relay.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>
#include <utility>

#include "src/gateway_protocol.h"
#include "src/gateway_socket.h"

namespace hermes::ui {

InterruptRelay::InterruptRelay(QString home, QString profile_name,
                               GatewayEndpoint endpoint, QObject* parent)
    : QObject(parent),
      home_(std::move(home)),
      profile_name_(std::move(profile_name)),
      endpoint_(std::move(endpoint)) {
  resume_deadline_ = new QTimer(this);
  resume_deadline_->setSingleShot(true);
  connect(resume_deadline_, &QTimer::timeout, this,
          &InterruptRelay::OnResumeDeadline);
}

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
  connect(socket_, &GatewaySocket::ResponseReceived, this,
          &InterruptRelay::OnResponse);
  socket_->Connect();
}

void InterruptRelay::Shutdown() {
  pending_.reset();
  active_.reset();
  if (resume_deadline_ != nullptr) {
    resume_deadline_->stop();
  }
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
    if (request.stop_processes) {
      Begin(request);
    } else {
      SendInterrupt(request.session_id, request.execution_generation);
    }
    return;
  }
  pending_ = std::move(request);
  if (socket_ == nullptr) {
    Start();
  } else {
    // Do not wait out an exponential reconnect backoff before delivering an
    // explicit Stop. Connect() is a no-op while a socket is already opening.
    socket_->Connect();
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
    qInfo("interrupt relay dropped expired %s profile=%s session=%.12s",
          pending_->stop_processes ? "stop" : "interrupt",
          qUtf8Printable(profile_name_),
          qUtf8Printable(pending_->session_id));
    pending_.reset();
    return;
  }
  PendingStop request = *pending_;
  pending_.reset();
  if (request.stop_processes) {
    Begin(request);
  } else {
    SendInterrupt(request.session_id, request.execution_generation);
  }
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

void InterruptRelay::Begin(const PendingStop& request) {
  if (socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }
  ActiveStop active;
  active.session_id = request.session_id;
  active.cached_generation = request.execution_generation;
  active_ = active;
  resume_deadline_->start(kResumeDeadlineMs);

  // Ask for the live runtime id and claim generation. `omit_messages` keeps
  // this to session metadata, so Stop does not pay for a transcript read.
  active_->resume_request = socket_->RequestRpc(
      QStringLiteral("session.resume"),
      QJsonObject{{QStringLiteral("session_id"), request.session_id},
                  {QStringLiteral("omit_messages"), true}});

  // Common case: the event stream is live and the cached generation is the
  // running claim. Fire the interrupt without waiting for resume.
  if (request.execution_generation >= 0) {
    SendInterrupt(request.session_id, request.execution_generation);
  }
  if (active_->resume_request < 0) {
    // No correlated resume response will arrive; still stop the processes.
    SendProcessStop(request.session_id);
  }
}

void InterruptRelay::SendInterrupt(const QString& session_id,
                                   int execution_generation) {
  if (socket_ == nullptr || !socket_->IsConnected() ||
      session_id.isEmpty() || execution_generation < 0) {
    return;
  }
  if (active_.has_value() &&
      active_->interrupted_generation == execution_generation) {
    return;
  }
  const int id = socket_->InterruptSession(session_id, execution_generation);
  if (id < 0) {
    return;
  }
  if (active_.has_value()) {
    active_->interrupt_request = id;
    active_->interrupted_generation = execution_generation;
  }
}

void InterruptRelay::SendProcessStop(const QString& session_id) {
  if (socket_ == nullptr || !socket_->IsConnected() || session_id.isEmpty()) {
    return;
  }
  if (!active_.has_value() || active_->process_sent) {
    return;
  }
  active_->process_sent = true;
  qInfo("stop profile=%s session=%.12s live=%.12s generation=%d",
        qUtf8Printable(profile_name_), qUtf8Printable(active_->session_id),
        qUtf8Printable(session_id), active_->interrupted_generation);
  active_->process_request = socket_->RequestRpc(
      QStringLiteral("process.stop"), BuildProcessStopParams(session_id));
  if (active_->process_request < 0) {
    Finish();
  }
}

void InterruptRelay::OnResponse(int id, const QJsonObject& result,
                                const QString& error) {
  if (!active_.has_value()) {
    return;
  }
  ActiveStop& active = *active_;
  if (id == active.resume_request) {
    active.resume_done = true;
    resume_deadline_->stop();
    const StopResumeTarget target = ResolveStopResumeTarget(
        result, active.session_id, active.cached_generation);
    active.live_session_id = target.session_id;
    if (error.isEmpty()) {
      if (target.running && target.execution_generation >= 0 &&
          target.execution_generation != active.interrupted_generation) {
        // The cached generation was stale or unknown: fence the interrupt on
        // the claim the gateway just reported.
        SendInterrupt(target.session_id, target.execution_generation);
      }
    } else {
      qInfo("stop resume profile=%s session=%.12s: %s",
            qUtf8Printable(profile_name_),
            qUtf8Printable(active.session_id), qUtf8Printable(error));
      if (active.interrupted_generation < 0 &&
          active.cached_generation >= 0) {
        SendInterrupt(active.session_id, active.cached_generation);
      }
    }
    SendProcessStop(target.session_id.isEmpty() ? active.session_id
                                                 : target.session_id);
    return;
  }
  if (id == active.process_request) {
    if (!error.isEmpty()) {
      qInfo("process.stop profile=%s session=%.12s: %s",
            qUtf8Printable(profile_name_),
            qUtf8Printable(active.live_session_id), qUtf8Printable(error));
    }
    Finish();
    return;
  }
  if (id == active.interrupt_request && !error.isEmpty() &&
      !error.contains(QStringLiteral("stale_generation"))) {
    qInfo("session.interrupt profile=%s session=%.12s: %s",
          qUtf8Printable(profile_name_),
          qUtf8Printable(active.live_session_id), qUtf8Printable(error));
  }
}

void InterruptRelay::OnResumeDeadline() {
  if (!active_.has_value() || active_->resume_done) {
    return;
  }
  // Resume is metadata-only and local; a reply this late means the gateway is
  // wedged. Stop is explicit, so still kill the session's processes.
  qInfo("stop resume timed out profile=%s session=%.12s",
        qUtf8Printable(profile_name_),
        qUtf8Printable(active_->session_id));
  if (active_->interrupted_generation < 0 &&
      active_->cached_generation >= 0) {
    SendInterrupt(active_->session_id, active_->cached_generation);
  }
  SendProcessStop(active_->session_id);
}

void InterruptRelay::Finish() {
  if (resume_deadline_ != nullptr) {
    resume_deadline_->stop();
  }
  active_.reset();
}

}  // namespace hermes::ui
