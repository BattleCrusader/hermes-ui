// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/gateway_socket.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>
#include <QUuid>
#include <QWebSocketHandshakeOptions>
#include <algorithm>
#include <utility>

#include "src/gateway_protocol.h"
#include "src/prompt_submit.h"

namespace hermes::ui {
namespace {

constexpr char kGatewayProtocol[] = "hermes-gateway-v1";
constexpr char kTicketPrefix[] = "hermes-gateway-ticket.";
constexpr int kPingIntervalMs = 20000;

}  // namespace

GatewaySocket::GatewaySocket(QString home, QString profile_name,
                             GatewayEndpoint endpoint, QObject* parent)
    : QObject(parent),
      home_(std::move(home)),
      profile_name_(std::move(profile_name)),
      endpoint_(std::move(endpoint)) {
  reconnect_timer_.setSingleShot(true);
  ping_timer_.setInterval(kPingIntervalMs);
  connect(&reconnect_timer_, &QTimer::timeout, this, [this]() { Connect(); });
  connect(&ping_timer_, &QTimer::timeout, this,
          [this]() { Request(QStringLiteral("gateway.ping"), QJsonObject()); });
  connect(&socket_, &QWebSocket::connected, this, &GatewaySocket::OnConnected);
  connect(&socket_, &QWebSocket::disconnected, this,
          &GatewaySocket::OnDisconnected);
  connect(&socket_, &QWebSocket::textMessageReceived, this,
          &GatewaySocket::OnTextMessage);
  connect(&socket_, &QWebSocket::errorOccurred, this, &GatewaySocket::OnError);
}

GatewaySocket::~GatewaySocket() { Close(); }

bool GatewaySocket::IsConnected() const {
  return socket_.state() == QAbstractSocket::ConnectedState;
}

void GatewaySocket::Connect() {
  if (shutting_down_ || socket_.state() != QAbstractSocket::UnconnectedState) {
    return;
  }
  QString ticket;
  QString error;
  if (!RequestTicket(home_, profile_name_, endpoint_,
                     QStringLiteral("interactive"), &ticket, &error)) {
    emit SocketError(error);
    ScheduleReconnect();
    return;
  }

  QUrl url(endpoint_.api_origin);
  if (url.scheme() == QStringLiteral("https")) {
    url.setScheme(QStringLiteral("wss"));
  } else {
    url.setScheme(QStringLiteral("ws"));
  }
  url.setPath(QStringLiteral("/api/ws"));

  QWebSocketHandshakeOptions options;
  options.setSubprotocols({QString::fromLatin1(kGatewayProtocol),
                           QString::fromLatin1(kTicketPrefix) + ticket});
  socket_.open(url, options);
}

void GatewaySocket::Close() {
  shutting_down_ = true;
  reconnect_timer_.stop();
  ping_timer_.stop();
  // Abort rather than a close handshake: Close runs during app teardown, when
  // a pending close timer would outlive the event dispatcher.
  if (socket_.state() != QAbstractSocket::UnconnectedState) {
    socket_.abort();
  }
}

void GatewaySocket::OnConnected() {
  if (socket_.subprotocol() != QString::fromLatin1(kGatewayProtocol)) {
    emit SocketError(
        QStringLiteral("gateway accepted an unexpected "
                       "WebSocket subprotocol"));
    socket_.close();
    return;
  }
  reconnect_attempt_ = 0;
  ping_timer_.start();
  // Announce that this connection can answer approval/clarify server
  // requests. Without it the gateway drops those frames and the agent waits
  // out its approval timeout with no prompt on screen.
  Request(QStringLiteral("client.capabilities"),
          QJsonObject{{QStringLiteral("server_requests"), true}});
  emit Connected(socket_.subprotocol());
}

void GatewaySocket::OnDisconnected() {
  ping_timer_.stop();
  emit Disconnected();
  if (!shutting_down_) {
    ScheduleReconnect();
  }
}

void GatewaySocket::OnError() {
  emit SocketError(socket_.errorString());
  if (!shutting_down_) {
    ScheduleReconnect();
  }
}

void GatewaySocket::ScheduleReconnect() {
  if (shutting_down_ || reconnect_timer_.isActive()) {
    return;
  }
  const int exponent = std::min(reconnect_attempt_, 4);
  const int delay_ms = std::min(15000, 1000 << exponent);
  ++reconnect_attempt_;
  reconnect_timer_.start(delay_ms);
}

void GatewaySocket::OnTextMessage(const QString& message) {
  QJsonParseError parse_error;
  const QJsonDocument document =
      QJsonDocument::fromJson(message.toUtf8(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    emit SocketError(QStringLiteral("gateway sent invalid JSON"));
    return;
  }
  const QJsonObject frame = document.object();

  if (frame.value(QStringLiteral("method")).toString() ==
      QStringLiteral("event")) {
    HandleEventParams(frame.value(QStringLiteral("params")).toObject());
    return;
  }

  if (frame.contains(QStringLiteral("method"))) {
    // A server->client request (approval, clarify, preview read, ...). It has
    // an id but no result/error; answering it as a response would be wrong.
    emit ServerRequestReceived(
        frame.value(QStringLiteral("id")),
        frame.value(QStringLiteral("method")).toString(),
        frame.value(QStringLiteral("params")).toObject());
    return;
  }

  if (!frame.contains(QStringLiteral("id"))) {
    return;
  }
  const int id = frame.value(QStringLiteral("id")).toInt();
  const QJsonObject error = frame.value(QStringLiteral("error")).toObject();
  const QString method = request_methods_.take(id);
  const QString session_id = request_sessions_.take(id);
  if (!error.isEmpty()) {
    qWarning("rpc error id=%d method=%s message=%s data=%s", id,
             qUtf8Printable(method),
             qUtf8Printable(error.value(QStringLiteral("message")).toString()),
             qUtf8Printable(QString::fromUtf8(
                 QJsonDocument(error.value(QStringLiteral("data")).toObject())
                     .toJson(QJsonDocument::Compact))));
    emit ResponseReceived(id, QJsonObject(),
                          error.value(QStringLiteral("message"))
                              .toString(QStringLiteral("gateway RPC failed")));
    return;
  }
  const QJsonObject result = frame.value(QStringLiteral("result")).toObject();
  if (method == QStringLiteral("session.events.since")) {
    const QJsonArray events = result.value(QStringLiteral("events")).toArray();
    for (const QJsonValue& event : events) {
      HandleEventParams(event.toObject());
    }
    if (result.value(QStringLiteral("truncated")).toBool()) {
      // The ring evicted the window before our watermark. Move past it and
      // refetch durable state; otherwise every reconnect asks for the same
      // missing range forever.
      replay_.SetLastSeq(session_id,
                         result.value(QStringLiteral("latest_seq")).toInt());
      emit ReplayGap(session_id);
    }
    // Open requests are part of the replay window: re-deliver cards that
    // survived the dropped socket, not just the events ring.
    for (const QJsonValue& value :
         result.value(QStringLiteral("open_requests")).toArray()) {
      const QJsonObject request = value.toObject();
      emit ServerRequestReceived(
          request.value(QStringLiteral("id")),
          request.value(QStringLiteral("method")).toString(),
          request.value(QStringLiteral("params")).toObject());
    }
  } else if (method == QStringLiteral("session.resume") ||
             method == QStringLiteral("session.activate")) {
    const QString runtime =
        result.value(QStringLiteral("session_id")).toString();
    const QString replay_session = runtime.isEmpty() ? session_id : runtime;
    if (!runtime.isEmpty() && runtime != session_id &&
        replay_.LastSeq(session_id) > 0) {
      // A cold rebuild (or a compression handoff) produced a new runtime id.
      // Its replay ring starts empty, so durable state is the only truth.
      replay_.ForgetSession(session_id);
      emit ReplayGap(session_id);
    }
    if (replay_.LastSeq(replay_session) > 0) {
      RequestReplay(replay_session);
    }
  }
  emit ResponseReceived(id, result, QString());
}

void GatewaySocket::HandleEventParams(const QJsonObject& params) {
  const QString type = params.value(QStringLiteral("type")).toString();
  const QString session_id =
      params.value(QStringLiteral("session_id")).toString();
  QJsonObject payload = params.value(QStringLiteral("payload")).toObject();
  if (type == QStringLiteral("gateway.ready")) {
    const QStringList previous = replay_.Sessions();
    if (replay_.ObserveEpoch(
            payload.value(QStringLiteral("replay_epoch")).toString())) {
      // Sequence numbers are only comparable inside one gateway process.
      for (const QString& session : previous) {
        emit ReplayGap(session);
      }
    }
  }
  // The owner stamps execution identity beside the payload. Fold it in so
  // consumers can read one object: /steer and /stop must echo the running
  // turn's execution_generation or the gateway rejects them as invalid.
  const QJsonValue generation =
      params.value(QStringLiteral("execution_generation"));
  if (generation.isDouble() &&
      !payload.contains(QStringLiteral("execution_generation"))) {
    payload.insert(QStringLiteral("execution_generation"), generation);
  }
  const int seq = params.value(QStringLiteral("seq")).toInt();
  if (!replay_.AcceptEvent(session_id, seq)) {
    return;
  }
  emit EventReceived(type, session_id, payload);
}

void GatewaySocket::RequestReplay(const QString& session_id) {
  const int last_seen = replay_.LastSeq(session_id);
  if (session_id.isEmpty() || last_seen <= 0) {
    return;
  }
  Request(QStringLiteral("session.events.since"),
          BuildSessionEventsSinceParams(session_id, profile_name_, last_seen));
}

int GatewaySocket::Request(const QString& method, const QJsonObject& params) {
  if (!IsConnected()) {
    return -1;
  }
  const int id = next_id_++;
  request_methods_.insert(id, method);
  const QString session_id =
      params.value(QStringLiteral("session_id")).toString();
  if (!session_id.isEmpty()) {
    request_sessions_.insert(id, session_id);
  }
  QJsonObject frame;
  frame.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  frame.insert(QStringLiteral("id"), id);
  frame.insert(QStringLiteral("method"), method);
  frame.insert(QStringLiteral("params"), params);
  socket_.sendTextMessage(
      QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
  return id;
}

void GatewaySocket::ResumeSession(const QString& session_id) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  if (!profile_name_.isEmpty()) {
    params.insert(QStringLiteral("profile"), profile_name_);
  }
  Request(QStringLiteral("session.resume"), params);
}

int GatewaySocket::SendPrompt(const QString& session_id, const QString& text,
                              bool queued) {
  const QJsonObject params = BuildPromptSubmitParams(
      session_id, text, profile_name_,
      QUuid::createUuid().toString(QUuid::WithoutBraces), 0, false, false,
      queued);
  return Request(QStringLiteral("prompt.submit"), params);
}

int GatewaySocket::SendPromptAt(const QString& session_id, const QString& text,
                                qint64 truncate_before_row_id,
                                bool confirm_empty_truncate,
                                bool confirm_deep_truncate) {
  // The canonical owner admits identified input; the desktop mints this per
  // send so a retry cannot create a second turn.
  const QJsonObject params = BuildPromptSubmitParams(
      session_id, text, profile_name_,
      QUuid::createUuid().toString(QUuid::WithoutBraces),
      truncate_before_row_id, confirm_empty_truncate, confirm_deep_truncate);
  return Request(QStringLiteral("prompt.submit"), params);
}

void GatewaySocket::InterruptSession(const QString& session_id,
                                     int execution_generation) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  if (execution_generation >= 0) {
    params.insert(QStringLiteral("execution_generation"), execution_generation);
  }
  Request(QStringLiteral("session.interrupt"), params);
}

void GatewaySocket::SetConfig(const QString& session_id, const QString& key,
                              const QString& value) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("key"), key);
  params.insert(QStringLiteral("value"), value);
  Request(QStringLiteral("config.set"), params);
}

int GatewaySocket::ExecuteSlashCommand(const QString& session_id,
                                       const QString& command) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("command"), command);
  return Request(QStringLiteral("slash.exec"), params);
}

int GatewaySocket::SteerSession(const QString& session_id, const QString& text,
                                int execution_generation) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("text"), text);
  params.insert(QStringLiteral("execution_generation"), execution_generation);
  return Request(QStringLiteral("session.steer"), params);
}

int GatewaySocket::RequestRpc(const QString& method,
                              const QJsonObject& params) {
  return Request(method, params);
}

int GatewaySocket::FetchCommandCatalog(const QString& session_id) {
  QJsonObject params;
  if (!session_id.isEmpty()) {
    params.insert(QStringLiteral("session_id"), session_id);
  }
  return Request(QStringLiteral("commands.catalog"), params);
}

int GatewaySocket::CreateSession(const QString& request_id,
                                 const QString& cwd) {
  const QString key = request_id.isEmpty()
                          ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                          : request_id;
  return Request(QStringLiteral("session.create"),
                 BuildSessionCreateParams(profile_name_, key, cwd));
}

int GatewaySocket::SessionMutate(const QString& session_id,
                                 const QString& operation,
                                 const QJsonObject& payload,
                                 int expected_revision,
                                 int expected_generation) {
  const QJsonObject params = BuildSessionMutateParams(
      session_id, QUuid::createUuid().toString(QUuid::WithoutBraces),
      expected_revision, operation, payload, expected_generation);
  return Request(QStringLiteral("session.mutate"), params);
}

void GatewaySocket::Respond(const QJsonValue& id, const QJsonObject& result) {
  if (!IsConnected()) {
    return;
  }
  QJsonObject frame;
  frame.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  frame.insert(QStringLiteral("id"), id);
  frame.insert(QStringLiteral("result"), result);
  socket_.sendTextMessage(
      QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void GatewaySocket::RespondError(const QJsonValue& id, int code,
                                 const QString& message) {
  if (!IsConnected()) {
    return;
  }
  QJsonObject error;
  error.insert(QStringLiteral("code"), code);
  error.insert(QStringLiteral("message"), message);
  QJsonObject frame;
  frame.insert(QStringLiteral("jsonrpc"), QStringLiteral("2.0"));
  frame.insert(QStringLiteral("id"), id);
  frame.insert(QStringLiteral("error"), error);
  socket_.sendTextMessage(
      QString::fromUtf8(QJsonDocument(frame).toJson(QJsonDocument::Compact)));
}

void GatewaySocket::RespondPrompt(const QString& kind,
                                  const QString& session_id,
                                  int execution_generation,
                                  const QString& request_id,
                                  const QString& answer) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("execution_generation"), execution_generation);
  params.insert(QStringLiteral("prompt_id"), request_id);
  if (kind == QStringLiteral("clarify")) {
    params.insert(QStringLiteral("answer"), answer);
    Request(QStringLiteral("clarify.respond"), params);
    return;
  }
  params.insert(QStringLiteral("choice"), answer);
  Request(QStringLiteral("approval.respond"), params);
}

void GatewaySocket::LockClarify(const QString& request_id,
                                const QString& question_id,
                                const QString& answer) {
  QJsonObject params;
  params.insert(QStringLiteral("request_id"), request_id);
  params.insert(QStringLiteral("question_id"), question_id);
  params.insert(QStringLiteral("answer"), answer);
  Request(QStringLiteral("clarify.lock"), params);
}

void GatewaySocket::RespondConnection(const QString& op_id,
                                      const QString& session_id,
                                      const QJsonObject& result) {
  Request(
      QStringLiteral("connection.respond"),
      BuildConnectionRespondParams(op_id, session_id, result, profile_name_));
}

}  // namespace hermes::ui
