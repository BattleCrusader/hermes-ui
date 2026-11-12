// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROFILE_WORKER_H_
#define HERMES_UI_PROFILE_WORKER_H_

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include "src/gateway_endpoint.h"
#include "src/model.h"
#include "src/pending_prompt_tracker.h"

class QTimer;

namespace hermes::ui {

class GatewayHttp;
class GatewaySocket;

// One warm profile. The object lives on a dedicated QThread with its own Qt
// event loop; its QNetworkAccessManager and QWebSocket are created there. The
// UI only reads immutable snapshots under a short mutex.
class ProfileWorker : public QObject {
  Q_OBJECT

 public:
  ProfileWorker(QString name, QString home, QColor color, bool is_default,
                GatewayEndpoint endpoint, QString gateway_version,
                bool enable_streaming, QObject* parent = nullptr);
  ~ProfileWorker() override;

  ProfileWorker(const ProfileWorker&) = delete;
  ProfileWorker& operator=(const ProfileWorker&) = delete;

  std::shared_ptr<const ProfileSnapshot> Snapshot() const;
  void SetForeground(bool foreground);

  // `session.steer` is asynchronous. The callback reports whether the gateway
  // accepted the steer; the fallback path (no running turn) is also success
  // because it sends the text as a normal prompt.
  using SteerCallback = std::function<void(bool ok, const QString& message)>;

 public slots:
  void Start();
  void Shutdown();
  void SelectSession(const QString& session_id);
  void SendPrompt(const QString& text);
  // Queue behind the running turn. The gateway owns the admission; `queued`
  // forces queue mode so run-after input can never become a live steer.
  void QueuePrompt(const QString& text);
  // Image/PDF/files are queued on the gateway (image.attach / pdf.attach /
  // file.attach) before the prompt is submitted, so the model receives real
  // image tickets instead of only paths in the text.
  void SendPromptWithAttachments(const QString& text,
                                 const QStringList& attachments);
  // Edit-and-resend / regenerate: drop the target user row and everything
  // after it before submitting the replacement text.
  void SendPromptAt(const QString& text, qint64 truncate_before_row_id,
                    bool confirm_empty_truncate, bool confirm_deep_truncate);
  void SendPromptAtWithAttachments(const QString& text,
                                   qint64 truncate_before_row_id,
                                   bool confirm_empty_truncate,
                                   bool confirm_deep_truncate,
                                   const QStringList& attachments);
  void NewSession();
  // Project-scoped new chat: `session.create` carries cwd/cwd_explicit so the
  // gateway starts the session in the project's primary folder. `project_id`
  // tags the local placeholder so the sidebar can keep it under the project
  // that started it before `projects.tree` reports the row.
  void NewSessionInDir(const QString& cwd,
                       const QString& project_id = QString());
  void Interrupt();
  void Refresh();
  void AttachIfForeground();
  void RespondRequest(const QString& request_id, const QJsonObject& result);
  // `connectors.connect` returns the initial account-operation snapshot before
  // any broadcast arrives; keep it so the Capabilities page survives a rebuild.
  void RememberAccountOperation(const QJsonObject& result);
  void SetModel(const QString& provider, const QString& model);
  void SetReasoning(const QString& effort);
  void FetchSettings();
  void SaveConfig(const QJsonObject& patch);
  // Settings data pages. The worker owns the route table so the view only
  // names a page ("providers", "cron", ...) and a source key.
  void FetchPage(const QString& page);
  void PageAction(const QString& page, const QString& method,
                  const QString& path, const QJsonObject& body);
  void FetchArtifacts();
  // Root the file browser at the active project (or the user's home when no
  // project is scoped). Navigating inside the page does not change the root;
  // switching projects resets the browser to the new root.
  void SetArtifactsRoot(const QString& path);
  void OpenArtifactsDir(const QString& path);
  void PreviewArtifact(const QString& path);
  // Spot editor save. Re-reads the file first and refuses when the bytes on
  // disk differ from what the editor was seeded with (stale-write guard).
  void SaveArtifact(const QString& path, const QString& text);
  void PatchSession(const QString& session_id, const QJsonObject& patch);
  void DeleteSession(const QString& session_id);
  void AttachFile(const QString& local_path);
  void ExecuteSlash(const QString& command);
  void Steer(const QString& text);
  void SteerWithCallback(const QString& text, SteerCallback done);
  // Appends a queued prompt that the gateway has moved into the running turn.
  // `admission_id` deduplicates against the pending-snapshot transition.
  void RecordInjectedPrompt(const QString& text,
                            const QString& admission_id = QString());
  // Cancelled/replaced admissions must not resurface from stale snapshots.
  void ForgetPendingAdmission(const QString& admission_id);
  void Note(const QString& text);
  void FetchSlashCatalog();
  // Remote skill-hub operations run on this worker thread and report through
  // HubRequestFinished, so a slow network search never freezes the shell.
  void FetchSkillHubSources(quint64 token);
  void SearchSkillHub(quint64 token, const QString& query,
                      const QString& source, int limit);
  void PreviewSkillHub(quint64 token, const QString& identifier);
  void ScanSkillHub(quint64 token, const QString& identifier);
  // Computer Use probes and polls can spawn cua-driver; keep them off the UI.
  void FetchComputerUseStatus(quint64 token);
  void GrantComputerUsePermissions(quint64 token);
  void FetchActionStatus(quint64 token, const QString& name);
  // Global right-rail search; FTS can scan a large store, so it stays here.
  void SearchSessions(quint64 token, const QString& query, int limit);
  void FetchTerminalBackends(quint64 token);
  void SelectTerminalBackend(quint64 token, const QString& backend);
  void FetchToolsetModels(quint64 token, const QString& name,
                          const QString& provider);
  void SelectToolsetModel(quint64 token, const QString& name,
                          const QString& model, const QString& provider);
  // Read-aloud synthesis runs here too: TTS providers can take seconds.
  void SpeakText(quint64 token, const QString& text);
  // Dictation: read a recorded clip, wrap it as a data URL, and let the
  // gateway's STT provider transcribe it off the UI thread.
  void TranscribeAudio(quint64 token, const QString& path);
  // Canonical RPC pass-through for session lifecycle mutations.
  void SendRpc(quint64 token, const QString& method, const QJsonObject& params);

 signals:
  void HubRequestFinished(quint64 token, const QJsonObject& result,
                          const QString& error);
  void SpeechFinished(quint64 token, const QJsonObject& result,
                      const QString& error);
  void TranscriptionFinished(quint64 token, const QJsonObject& result,
                             const QString& error);
  void RpcFinished(quint64 token, const QJsonObject& result,
                   const QString& error);
  void SteerFinished(quint64 token, bool ok, const QString& message);

 private:
  void FinishActionRequest(quint64 token, const QString& method,
                           const QString& route, const QJsonObject& body);
  void SendPromptInternal(const QString& text, qint64 truncate_before_row_id,
                          bool confirm_empty_truncate,
                          bool confirm_deep_truncate,
                          const QStringList& attachments = {},
                          bool queued = false);
  void SubmitPromptFrame(const QString& session_id, const QString& text,
                         qint64 truncate_before_row_id,
                         bool confirm_empty_truncate,
                         bool confirm_deep_truncate, bool queued);
  bool QueuePromptAttachments(const QString& session_id, const QString& text,
                              qint64 truncate_before_row_id,
                              bool confirm_empty_truncate,
                              bool confirm_deep_truncate,
                              const QStringList& attachments);
  void FetchSessions();
  void LoadCachedState();
  void FlushSessionCache();
  void FetchMessages(const QString& session_id);
  void FetchModelOptions();
  void OnSocketConnected(const QString& subprotocol);
  void OnSocketDisconnected();
  void OnSocketEvent(const QString& type, const QString& session_id,
                     const QJsonObject& payload);
  // Folds a canonical snapshot/`session.info` payload's pending admissions
  // into the UI snapshot, ignoring an older same-epoch revision.
  void ApplyPendingSnapshotLocked(const QJsonObject& source);
  void OnReplayGap(const QString& session_id);
  void OnSocketResponse(int id, const QJsonObject& result,
                        const QString& error);
  void AcceptServerRequest(const QJsonValue& id, const QString& method,
                           const QJsonObject& params);
  void AcceptCanonicalPrompt(const QString& kind, const QString& session_id,
                             const QJsonObject& payload);
  void AcceptConnectionRequest(const QString& session_id,
                               const QJsonObject& payload);
  void ApplyConnectionUpdate(const QString& session_id,
                             const QJsonObject& payload);
  void StoreAccountOperationLocked(ConnectionOperation operation);
  void ApplySessions(const QJsonArray& rows);
  void ApplyMessages(const QJsonArray& rows);
  void AppendToolRow(const QString& id, const QString& name,
                     const QString& detail);
  void UpdateToolRow(const QString& id, const QString& detail);
  void AppendAssistantDelta(const QString& text);
  void AppendUserPromptLocked(const QString& text);
  void AppendReasoningDelta(const QString& text, bool streaming);
  void AppendMoaReasoning(const QString& text, bool replace);
  ChatMessage* StreamingAssistantLocked();
  void ReparseStreamingLocked(ChatMessage* message);
  void CompleteAssistant(const QString& text, bool error);
  void PublishLocked();
  void MaybePublishLocked();
  void PublishError(const QString& error);
  void NoteActivityLocked();
  void FinishHubRequest(quint64 token, const QString& route);

  QString name_;
  QString home_;
  QString artifacts_root_;
  QColor color_;
  bool is_default_;
  GatewayEndpoint endpoint_;
  QString gateway_version_;
  bool enable_streaming_ = true;

  GatewayHttp* http_ = nullptr;
  GatewaySocket* socket_ = nullptr;
  QTimer* refresh_timer_ = nullptr;
  QTimer* message_refresh_timer_ = nullptr;
  QTimer* cache_timer_ = nullptr;

  mutable std::mutex mutex_;
  ProfileSnapshot state_;
  std::shared_ptr<const ProfileSnapshot> published_;
  bool foreground_ = false;
  bool shutting_down_ = false;
  qint64 last_publish_ms_ = 0;
  qint64 last_reparse_ms_ = 0;
  QString pending_prompt_;
  QString attached_session_id_;
  QString runtime_session_id_;
  int pending_create_id_ = -1;
  QString pending_create_project_id_;
  int pending_catalog_id_ = -1;
  bool catalog_requested_ = false;
  // Execution identity of the turn currently claimed on this session; the
  // gateway fences session.steer / session.interrupt on it.
  int execution_generation_ = -1;
  std::map<int, QString> pending_commands_;
  std::map<int, QString> pending_steers_;
  std::map<int, SteerCallback> pending_steer_callbacks_;
  std::map<int, quint64> pending_rpc_;
  struct PendingSessionMutation {
    QString session_id;
    QString operation;
    QJsonObject patch;
    bool remove = false;
  };
  std::map<int, PendingSessionMutation> pending_session_mutations_;
  int pending_prompt_request_id_ = -1;
  // prompt.submit mode for the request above. Normal sends already rendered
  // their user row locally, so the admission must not be announced again.
  bool pending_prompt_queued_ = false;
  QString pending_prompt_session_id_;
  QString admission_id_;
  QString admission_status_;
  PendingPromptTracker pending_prompts_;
  bool pending_prompts_baseline_ready_ = false;
  int pending_receipt_id_ = -1;
  struct PendingAttachedPrompt {
    QString session_id;
    QString text;
    qint64 truncate_before_row_id = 0;
    bool confirm_empty_truncate = false;
    bool confirm_deep_truncate = false;
    std::set<int> request_ids;
  };
  std::unique_ptr<PendingAttachedPrompt> pending_attached_prompt_;
  bool history_loaded_ = false;
  bool cache_dirty_ = false;
  bool pending_new_session_ = false;
  QString pending_new_session_cwd_;
  QString pending_new_session_project_id_;
  bool models_loaded_ = false;
  bool settings_requested_ = false;
  // Bytes the spot editor was seeded with, so a save can detect an
  // out-of-band edit instead of clobbering it.
  QString artifacts_preview_text_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_PROFILE_WORKER_H_
