// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_GATEWAY_SOCKET_H_
#define HERMES_UI_GATEWAY_SOCKET_H_

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWebSocket>

#include "src/gateway_endpoint.h"
#include "src/gateway_replay.h"

namespace hermes::ui {

// One canonical /api/ws connection for one profile. The socket lives on the
// profile worker thread; tickets are one-use and minted at every dial.
class GatewaySocket : public QObject {
  Q_OBJECT

 public:
  // `server_requests` advertises that this client can answer approval and
  // clarify prompts. A control-only socket (the interrupt relay) must leave it
  // false so the gateway never routes a prompt to a client that only sends
  // session.interrupt.
  GatewaySocket(QString home, QString profile_name, GatewayEndpoint endpoint,
                QObject* parent = nullptr, bool server_requests = true);
  ~GatewaySocket() override;

  void Connect();
  void Close();
  bool IsConnected() const;

  void ResumeSession(const QString& session_id);
  int SendPrompt(const QString& session_id, const QString& text,
                 bool queued = false);
  // Edits/regenerates carry an explicit truncation target. `row_id` is the
  // durable user-turn row to drop before resubmitting.
  int SendPromptAt(const QString& session_id, const QString& text,
                   qint64 truncate_before_row_id, bool confirm_empty_truncate,
                   bool confirm_deep_truncate);
  int InterruptSession(const QString& session_id,
                       int execution_generation = -1);
  int CreateSession(const QString& request_id = QString(),
                    const QString& cwd = QString());
  int ExecuteSlashCommand(const QString& session_id, const QString& command);
  int SteerSession(const QString& session_id, const QString& text,
                   int execution_generation);
  int FetchCommandCatalog(const QString& session_id);
  // Canonical revision/generation-fenced session mutation.
  int SessionMutate(const QString& session_id, const QString& operation,
                    const QJsonObject& payload, int expected_revision,
                    int expected_generation = -1);
  // Raw canonical RPC. Used for session lifecycle mutations (branch/undo/
  // delete) and message reactions, which have no dedicated wrapper yet.
  int RequestRpc(const QString& method, const QJsonObject& params);
  // Session-scoped config mutation used by the composer model/reasoning pills.
  void SetConfig(const QString& session_id, const QString& key,
                 const QString& value);
  // Answers a server->client request by its own (string) id.
  void Respond(const QJsonValue& id, const QJsonObject& result);
  // Settles a request this build cannot answer. 4404 is the gateway's
  // NOT_SHOWN code: the agent gets a refusal now instead of waiting 300 s.
  void RespondError(const QJsonValue& id, int code, const QString& message);

  // The gateway API's canonical prompt events are answered with normal RPCs,
  // not JSON-RPC response frames. `kind` is "approval" or "clarify"; the
  // generation fences the answer to the turn that raised the prompt.
  void RespondPrompt(const QString& kind, const QString& session_id,
                     int execution_generation, const QString& request_id,
                     const QString& answer);
  // Batch clarify settles one question at a time; the last lock resolves the
  // blocked tool call. Matches ClarifyLockParams (request id only: the owner
  // resolves the session from the open request).
  void LockClarify(const QString& request_id, const QString& question_id,
                   const QString& answer);
  // `connection.request` / `connection.update` cards answer with a normal RPC,
  // not a JSON-RPC response frame. `result` is ConnectionAnswer.
  void RespondConnection(const QString& op_id, const QString& session_id,
                         const QJsonObject& result);

 signals:
  void Connected(const QString& subprotocol);
  void Disconnected();
  void EventReceived(const QString& type, const QString& session_id,
                     const QJsonObject& payload);
  void ResponseReceived(int id, const QJsonObject& result,
                        const QString& error);
  void ServerRequestReceived(const QJsonValue& id, const QString& method,
                             const QJsonObject& params);
  // The replay ring could not bridge the gap (a backend restart, an evicted
  // window, or a new runtime id). The owner refetches durable state.
  void ReplayGap(const QString& session_id);
  void SocketError(const QString& message);

 private slots:
  void OnConnected();
  void OnDisconnected();
  void OnTextMessage(const QString& message);
  void OnError();

 private:
  int Request(const QString& method, const QJsonObject& params);
  void ScheduleReconnect();
  void HandleEventParams(const QJsonObject& params);
  void RequestReplay(const QString& session_id);

  QString home_;
  QString profile_name_;
  GatewayEndpoint endpoint_;
  QWebSocket socket_;
  QTimer reconnect_timer_;
  QTimer ping_timer_;
  QHash<int, QString> request_methods_;
  QHash<int, QString> request_sessions_;
  ReplayCursor replay_;
  int next_id_ = 1;
  int reconnect_attempt_ = 0;
  bool shutting_down_ = false;
  bool server_requests_ = true;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_GATEWAY_SOCKET_H_
