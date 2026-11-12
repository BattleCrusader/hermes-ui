// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SUPERVISOR_H_
#define HERMES_UI_SUPERVISOR_H_

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "src/gateway_endpoint.h"
#include "src/model.h"

class QThread;

namespace hermes::ui {

class ProfileWorker;

// Owns the warm profile workers and the active-profile pointer. Every worker
// has its own QThread and event loop; activating a profile only flips
// foreground flags.
class Supervisor {
 public:
  explicit Supervisor(bool enable_streaming = true);
  ~Supervisor();

  Supervisor(const Supervisor&) = delete;
  Supervisor& operator=(const Supervisor&) = delete;

  void Start();
  void Stop();

  int profile_count() const;
  int active_index() const;
  void Activate(int index);
  void ActivateByName(const QString& name);

  std::shared_ptr<const ProfileSnapshot> ActiveSnapshot() const;
  std::shared_ptr<const ProfileSnapshot> SnapshotAt(int index) const;

  void SelectSession(int index, const QString& session_id);
  void SendPrompt(int index, const QString& text);
  // Queue behind the active turn; the gateway FIFO owns the admission.
  void QueuePrompt(int index, const QString& text);
  void SendPromptWithAttachments(int index, const QString& text,
                                 const QStringList& attachments);
  void SendPromptAt(int index, const QString& text, qint64 truncate_row_id,
                    bool confirm_empty_truncate, bool confirm_deep_truncate);
  void NewSession(int index, const QString& cwd = QString(),
                  const QString& project_id = QString());
  void Interrupt(int index);
  void RespondRequest(int index, const QString& request_id,
                      const QJsonObject& result);
  // Keep the initial `connectors.connect` operation snapshot on the worker so
  // Capabilities can render account progress after a page rebuild.
  void RememberAccountOperation(int index, const QJsonObject& result);
  void SetModel(int index, const QString& provider, const QString& model);
  void RefreshModelOptions(int index);
  void SetReasoning(int index, const QString& effort);
  void FetchSettings(int index);
  void SaveConfig(int index, const QJsonObject& patch);
  void FetchPage(int index, const QString& page);
  void PageAction(int index, const QString& page, const QString& method,
                  const QString& path, const QJsonObject& body);
  // Workspace pages (artifacts file browser / preview) and session flags.
  void FetchArtifacts(int index);
  void SetArtifactsRoot(int index, const QString& path);
  void OpenArtifactsDir(int index, const QString& path);
  void PreviewArtifact(int index, const QString& path);
  void SaveArtifact(int index, const QString& path, const QString& text);
  void PatchSession(int index, const QString& session_id,
                    const QJsonObject& patch);
  void DeleteSession(int index, const QString& session_id);
  void AttachFile(int index, const QString& local_path);
  void ExecuteSlash(int index, const QString& command);
  using SteerCallback = std::function<void(bool ok, const QString& message)>;
  // `done` reports whether session.steer was accepted (or safely sent as a
  // new prompt when no turn was running). It runs on the UI thread.
  void Steer(int index, const QString& text, SteerCallback done = {});
  void RecordInjectedPrompt(int index, const QString& text,
                            const QString& admission_id = QString());
  void ForgetPendingAdmission(int index, const QString& admission_id);
  void Note(int index, const QString& text);
  void FetchSlashCatalog(int index);
  // Directory listing for composer @-mentions. Synchronous by design: it runs
  // only when the popup opens a folder, not on every keystroke.
  bool ListDirectory(const QString& path, QJsonArray* entries, QString* error);
  // Async skill-hub requests. Callbacks run on the UI thread after the worker
  // finishes; the token keeps overlapping searches/previews independent.
  using SkillHubCallback =
      std::function<void(const QJsonObject& result, const QString& error)>;
  void ListSkillHubSources(int index, SkillHubCallback done);
  void SearchSkillHub(int index, const QString& query, const QString& source,
                      int limit, SkillHubCallback done);
  void PreviewSkillHub(int index, const QString& identifier,
                       SkillHubCallback done);
  void ScanSkillHub(int index, const QString& identifier,
                    SkillHubCallback done);
  // Computer Use: status probes cua-driver, grant spawns the macOS permission
  // action, and action status polls that background action.
  using ComputerUseCallback = SkillHubCallback;
  void GetComputerUseStatus(int index, ComputerUseCallback done);
  void GrantComputerUsePermissions(int index, ComputerUseCallback done);
  void GetActionStatus(int index, const QString& name,
                       ComputerUseCallback done);
  void ListTerminalBackends(int index, ComputerUseCallback done);
  // Global session search for the chat-header bar. Same worker-thread
  // callback contract as the skill hub.
  void SearchSessions(int index, const QString& query, int limit,
                      ComputerUseCallback done);
  void SelectTerminalBackend(int index, const QString& backend,
                             ComputerUseCallback done);
  void GetToolsetModels(int index, const QString& name, const QString& provider,
                        ComputerUseCallback done);
  void SelectToolsetModel(int index, const QString& name, const QString& model,
                          const QString& provider, ComputerUseCallback done);
  void SteerSubagent(int index, const QString& session_id,
                     const QString& subagent_id, const QString& text,
                     SkillHubCallback done);
  void InterruptSubagent(int index, const QString& session_id,
                         const QString& subagent_id, SkillHubCallback done);
  // Artifact transfer. Both block for the duration of the request, which is
  // acceptable because the user explicitly picked a local file/destination.
  bool UploadArtifact(const QString& destination, const QString& local_path,
                      QString* uploaded_path, QString* error);
  bool DownloadArtifact(const QString& remote_path, const QString& local_path,
                        QString* error);
  // Connects to one MCP server, lists its tools, and disconnects. The probe
  // already runs off the gateway event loop; the caller shows the result.
  bool TestMcpServer(const QString& name, QJsonObject* result, QString* error);
  bool ListMcpCatalog(QJsonArray* entries, QString* error);
  // Toolset configuration reads the provider matrix and writes the selected
  // provider and/or non-blank env values into the active profile.
  bool GetToolsetConfig(const QString& name, QJsonObject* config,
                        QString* error);
  bool SaveToolsetConfig(const QString& name, const QString& provider,
                         const QJsonObject& env, QString* error);
  bool GetMcpConfig(QJsonObject* servers, QString* error);
  bool ReplaceMcpConfig(const QJsonObject& servers, QString* error);
  bool GetLearningNode(const QString& id, QJsonObject* detail, QString* error);
  // Starts a dashboard-mediated MCP OAuth flow, polls it, and cancels an
  // abandoned flow. Each call is one short gateway request.
  bool StartMcpOAuth(const QString& name, QJsonObject* flow, QString* error);
  bool PollMcpOAuth(const QString& flow_id, QJsonObject* flow, QString* error);
  bool CancelMcpOAuth(const QString& flow_id, QString* error);
  bool TestMessagingPlatform(const QString& platform_id, QJsonObject* result,
                             QString* error);
  bool StartTelegramOnboarding(const QString& bot_name, QJsonObject* pairing,
                               QString* error);
  bool PollTelegramOnboarding(const QString& pairing_id, QJsonObject* status,
                              QString* error);
  bool ApplyTelegramOnboarding(const QString& pairing_id,
                               const QStringList& allowed_user_ids,
                               QJsonObject* result, QString* error);
  bool CancelTelegramOnboarding(const QString& pairing_id, QString* error);
  // WhatsApp links a device by scanning a QR. Start may immediately report
  // `connected` when a session already exists; otherwise poll until the
  // gateway publishes a QR, then until the phone completes the link.
  bool StartWhatsAppOnboarding(const QString& mode,
                               const QString& allowed_users, bool reset_session,
                               QJsonObject* pairing, QString* error);
  bool PollWhatsAppOnboarding(const QString& pairing_id, QJsonObject* status,
                              QString* error);
  bool ApplyWhatsAppOnboarding(const QString& pairing_id, const QString& mode,
                               const QString& allowed_users,
                               QJsonObject* result, QString* error);
  bool CancelWhatsAppOnboarding(const QString& pairing_id, QString* error);
  // Synthesizes speech for read-aloud off the UI thread. The callback receives
  // the gateway's `data_url`; decoding and playback stay in the shell.
  using SpeechCallback =
      std::function<void(const QJsonObject& result, const QString& error)>;
  void SpeakText(int index, const QString& text, SpeechCallback done);
  // Dictation transcription of a local recording. Same callback shape as
  // read-aloud: the worker does the upload and STT round-trip.
  void TranscribeAudio(int index, const QString& path, SpeechCallback done);
  // Session lifecycle / reaction RPCs. Token-keyed like the other async
  // surfaces; callbacks run on the UI thread.
  using RpcCallback = SkillHubCallback;
  void RequestRpc(int index, const QString& method, const QJsonObject& params,
                  RpcCallback done);
  bool GetSkillContent(const QString& name, QString* content, QString* path,
                       QString* error);
  bool ListOfficialSkills(QJsonArray* skills, QString* error);
  // Session transfer. Export streams the gateway's JSON export to a local
  // file; import reads that shape (or a bare array) and posts it back with the
  // mutation fence the gateway requires.
  bool ExportSession(const QString& session_id, const QString& local_path,
                     QString* error);
  bool ImportSessions(const QString& local_path, int* imported, QString* error);
  // Profile lifecycle. Both reload the worker pool so the new/deleted profile
  // appears (or disappears) without restarting the window.
  bool CreateProfile(const QString& name, QString* error, bool reload = true);
  bool DeleteProfile(const QString& name, QString* error, bool reload = true);
  // SOUL.md and archive transfer are install-wide profile operations; unlike
  // config actions they are not scoped to the active profile.
  bool GetProfileSoul(const QString& name, QString* content, QString* error);
  bool SaveProfileSoul(const QString& name, const QString& content,
                       QString* error);
  bool ExportProfile(const QString& name, QString* archive, QString* error);
  bool ImportProfile(const QString& archive, QString* error,
                     bool reload = true);
  void ReloadProfiles();

  QString ProfileName(int index) const;
  QString ProfileLabel(int index) const;
  QColor ProfileColor(int index) const;
  bool IsDefault(int index) const;
  QString GatewayVersion() const;
  QString Error() const;

  // Immutable request context captured on the UI thread. Background tasks set
  // this on their worker thread so the synchronous gateway helpers never read
  // mutable Supervisor/profile state off the UI thread.
  struct RequestScope {
    QString home;
    QString profile;
    GatewayEndpoint endpoint;
  };
  RequestScope CaptureRequestScope() const;
  static void SetWorkerScope(const RequestScope& scope);
  static void ClearWorkerScope();

 private:
  struct Entry {
    ProfileWorker* worker = nullptr;
    QThread* thread = nullptr;
    QString name;
    QString label;
    QColor color;
    bool is_default = false;
  };

  RequestScope CurrentRequestScope() const;
  void LoadProfiles();
  void StartWorker(Entry* entry);
  void RequestSkillHub(
      int index, SkillHubCallback done,
      const std::function<void(ProfileWorker*, quint64)>& send);
  void RequestComputerUse(
      int index, ComputerUseCallback done,
      const std::function<void(ProfileWorker*, quint64)>& send);
  std::map<quint64, SkillHubCallback> skill_hub_callbacks_;
  std::map<quint64, ComputerUseCallback> computer_use_callbacks_;
  std::map<quint64, SpeechCallback> speech_callbacks_;
  std::map<quint64, SpeechCallback> transcription_callbacks_;
  std::map<quint64, RpcCallback> rpc_callbacks_;
  std::map<quint64, SteerCallback> steer_callbacks_;
  quint64 next_skill_hub_token_ = 1;
  quint64 next_computer_use_token_ = 1;
  quint64 next_speech_token_ = 1;
  quint64 next_transcription_token_ = 1;
  quint64 next_rpc_token_ = 1;
  quint64 next_steer_token_ = 1;

  std::vector<Entry> entries_;
  int active_index_ = 0;
  bool enable_streaming_ = true;
  bool started_ = false;
  bool stopped_ = false;
  GatewayEndpoint endpoint_;
  QString gateway_version_ = QStringLiteral("unknown");
  QString error_;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_SUPERVISOR_H_
