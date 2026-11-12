// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/supervisor.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

#include "src/gateway_http.h"
#include "src/interrupt_relay.h"
#include "src/profile_worker.h"
#include "src/session_transfer.h"

namespace hermes::ui {
namespace {

thread_local std::optional<Supervisor::RequestScope> g_worker_scope;

// Deterministic hue for a named profile, matching the desktop's
// profileColor(): hsl(hash % 360, 68%, 58%). Default stays neutral.
QColor ProfileColorFor(const QString& name) {
  if (name.isEmpty() || name == QStringLiteral("default")) {
    return QColor(QStringLiteral("#6E6E78"));
  }
  unsigned int hash = 0;
  for (const QChar character : name) {
    hash = hash * 31 + character.unicode();
  }
  return QColor::fromHsl(static_cast<int>(hash % 360), 173, 148);
}

}  // namespace

Supervisor::Supervisor(bool enable_streaming)
    : enable_streaming_(enable_streaming) {}

Supervisor::~Supervisor() { Stop(); }

Supervisor::RequestScope Supervisor::CaptureRequestScope() const {
  const QString profile = active_index_ >= 0 && active_index_ < profile_count()
                              ? ProfileName(active_index_)
                              : QStringLiteral("default");
  return RequestScope{HermesHome(), profile, endpoint_};
}

void Supervisor::SetWorkerScope(const RequestScope& scope) {
  g_worker_scope = scope;
}

void Supervisor::ClearWorkerScope() { g_worker_scope.reset(); }

Supervisor::RequestScope Supervisor::CurrentRequestScope() const {
  if (g_worker_scope.has_value()) {
    return *g_worker_scope;
  }
  return CaptureRequestScope();
}

void Supervisor::Start() {
  if (started_ || stopped_) {
    return;
  }
  started_ = true;
  if (entries_.empty()) {
    LoadProfiles();
  }
  for (Entry& entry : entries_) {
    StartWorker(&entry);
  }
  if (!entries_.empty()) {
    entries_.front().worker->SetForeground(true);
    StartRelay(&entries_.front());
  }
}

void Supervisor::StartWorker(Entry* entry) {
  if (entry == nullptr || entry->worker == nullptr) {
    return;
  }
  entry->thread = new QThread();
  entry->worker->moveToThread(entry->thread);
  QObject::connect(entry->thread, &QThread::finished, entry->worker,
                   &QObject::deleteLater);
  QObject::connect(
      entry->worker, &ProfileWorker::HubRequestFinished,
      QCoreApplication::instance(),
      [this](quint64 token, const QJsonObject& result, const QString& error) {
        const auto found = skill_hub_callbacks_.find(token);
        if (found == skill_hub_callbacks_.end()) {
          return;
        }
        SkillHubCallback done = std::move(found->second);
        skill_hub_callbacks_.erase(found);
        done(result, error);
      });
  QObject::connect(
      entry->worker, &ProfileWorker::HubRequestFinished,
      QCoreApplication::instance(),
      [this](quint64 token, const QJsonObject& result, const QString& error) {
        const auto found = computer_use_callbacks_.find(token);
        if (found == computer_use_callbacks_.end()) {
          return;
        }
        ComputerUseCallback done = std::move(found->second);
        computer_use_callbacks_.erase(found);
        done(result, error);
      });
  QObject::connect(
      entry->worker, &ProfileWorker::RpcFinished, QCoreApplication::instance(),
      [this](quint64 token, const QJsonObject& result, const QString& error) {
        const auto found = rpc_callbacks_.find(token);
        if (found == rpc_callbacks_.end()) {
          return;
        }
        RpcCallback done = std::move(found->second);
        rpc_callbacks_.erase(found);
        done(result, error);
      });
  QObject::connect(entry->worker, &ProfileWorker::SteerFinished,
                   QCoreApplication::instance(),
                   [this](quint64 token, bool ok, const QString& message) {
                     const auto found = steer_callbacks_.find(token);
                     if (found == steer_callbacks_.end()) {
                       return;
                     }
                     SteerCallback done = std::move(found->second);
                     steer_callbacks_.erase(found);
                     done(ok, message);
                   });
  QObject::connect(
      entry->worker, &ProfileWorker::SpeechFinished,
      QCoreApplication::instance(),
      [this](quint64 token, const QJsonObject& result, const QString& error) {
        const auto found = speech_callbacks_.find(token);
        if (found == speech_callbacks_.end()) {
          return;
        }
        SpeechCallback done = std::move(found->second);
        speech_callbacks_.erase(found);
        done(result, error);
      });
  QObject::connect(
      entry->worker, &ProfileWorker::TranscriptionFinished,
      QCoreApplication::instance(),
      [this](quint64 token, const QJsonObject& result, const QString& error) {
        const auto found = transcription_callbacks_.find(token);
        if (found == transcription_callbacks_.end()) {
          return;
        }
        SpeechCallback done = std::move(found->second);
        transcription_callbacks_.erase(found);
        done(result, error);
      });
  entry->thread->start();
  QMetaObject::invokeMethod(entry->worker, "Start", Qt::QueuedConnection);
}

bool Supervisor::StartRelay(Entry* entry) {
  if (entry == nullptr) {
    return false;
  }
  if (entry->relay != nullptr && entry->relay_thread != nullptr &&
      entry->relay_thread->isRunning()) {
    return true;
  }
  if (!endpoint_.valid()) {
    return false;
  }
  entry->relay_thread = new QThread();
  entry->relay = new InterruptRelay(HermesHome(), entry->name, endpoint_);
  entry->relay->moveToThread(entry->relay_thread);
  entry->relay_thread->start();
  QMetaObject::invokeMethod(entry->relay, "Start", Qt::QueuedConnection);
  return true;
}

void Supervisor::RequestSkillHub(
    int index, SkillHubCallback done,
    const std::function<void(ProfileWorker*, quint64)>& send) {
  if (index < 0 || index >= profile_count()) {
    done(QJsonObject(), QStringLiteral("No active profile"));
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    done(QJsonObject(), QStringLiteral("Profile is not running"));
    return;
  }
  const quint64 token = next_skill_hub_token_++;
  skill_hub_callbacks_[token] = std::move(done);
  send(worker, token);
}

void Supervisor::RequestComputerUse(
    int index, ComputerUseCallback done,
    const std::function<void(ProfileWorker*, quint64)>& send) {
  if (index < 0 || index >= profile_count()) {
    done(QJsonObject(), QStringLiteral("No active profile"));
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    done(QJsonObject(), QStringLiteral("Profile is not running"));
    return;
  }
  const quint64 token = next_computer_use_token_++;
  computer_use_callbacks_[token] = std::move(done);
  send(worker, token);
}

void Supervisor::GetComputerUseStatus(int index, ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done), [](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "FetchComputerUseStatus",
                                  Qt::QueuedConnection, Q_ARG(quint64, token));
      });
}

void Supervisor::GrantComputerUsePermissions(int index,
                                             ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done), [](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "GrantComputerUsePermissions",
                                  Qt::QueuedConnection, Q_ARG(quint64, token));
      });
}

void Supervisor::GetActionStatus(int index, const QString& name,
                                 ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done), [name](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "FetchActionStatus",
                                  Qt::QueuedConnection, Q_ARG(quint64, token),
                                  Q_ARG(QString, name));
      });
}

void Supervisor::ListTerminalBackends(int index, ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done), [](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "FetchTerminalBackends",
                                  Qt::QueuedConnection, Q_ARG(quint64, token));
      });
}

void Supervisor::SearchSessions(int index, const QString& query, int limit,
                                ComputerUseCallback done) {
  RequestComputerUse(index, std::move(done),
                     [query, limit](ProfileWorker* worker, quint64 token) {
                       QMetaObject::invokeMethod(
                           worker, "SearchSessions", Qt::QueuedConnection,
                           Q_ARG(quint64, token), Q_ARG(QString, query),
                           Q_ARG(int, limit));
                     });
}

void Supervisor::SelectTerminalBackend(int index, const QString& backend,
                                       ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done), [backend](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "SelectTerminalBackend",
                                  Qt::QueuedConnection, Q_ARG(quint64, token),
                                  Q_ARG(QString, backend));
      });
}

void Supervisor::GetToolsetModels(int index, const QString& name,
                                  const QString& provider,
                                  ComputerUseCallback done) {
  RequestComputerUse(index, std::move(done),
                     [name, provider](ProfileWorker* worker, quint64 token) {
                       QMetaObject::invokeMethod(
                           worker, "FetchToolsetModels", Qt::QueuedConnection,
                           Q_ARG(quint64, token), Q_ARG(QString, name),
                           Q_ARG(QString, provider));
                     });
}

void Supervisor::SelectToolsetModel(int index, const QString& name,
                                    const QString& model,
                                    const QString& provider,
                                    ComputerUseCallback done) {
  RequestComputerUse(
      index, std::move(done),
      [name, model, provider](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "SelectToolsetModel",
                                  Qt::QueuedConnection, Q_ARG(quint64, token),
                                  Q_ARG(QString, name), Q_ARG(QString, model),
                                  Q_ARG(QString, provider));
      });
}

void Supervisor::SteerSubagent(int index, const QString& session_id,
                               const QString& subagent_id, const QString& text,
                               SkillHubCallback done) {
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("subagent_id"), subagent_id},
                     {QStringLiteral("text"), text}};
  const std::shared_ptr<const ProfileSnapshot> snapshot = ActiveSnapshot();
  if (snapshot != nullptr && !snapshot->name.isEmpty()) {
    params.insert(QStringLiteral("profile"), snapshot->name);
  }
  RequestRpc(index, QStringLiteral("subagent.steer"), params, std::move(done));
}

void Supervisor::InterruptSubagent(int index, const QString& session_id,
                                   const QString& subagent_id,
                                   SkillHubCallback done) {
  QJsonObject params{{QStringLiteral("session_id"), session_id},
                     {QStringLiteral("subagent_id"), subagent_id}};
  const std::shared_ptr<const ProfileSnapshot> snapshot = ActiveSnapshot();
  if (snapshot != nullptr && !snapshot->name.isEmpty()) {
    params.insert(QStringLiteral("profile"), snapshot->name);
  }
  RequestRpc(index, QStringLiteral("subagent.interrupt"), params,
             std::move(done));
}

void Supervisor::ListSkillHubSources(int index, SkillHubCallback done) {
  RequestSkillHub(
      index, std::move(done), [](ProfileWorker* worker, quint64 token) {
        QMetaObject::invokeMethod(worker, "FetchSkillHubSources",
                                  Qt::QueuedConnection, Q_ARG(quint64, token));
      });
}

void Supervisor::SearchSkillHub(int index, const QString& query,
                                const QString& source, int limit,
                                SkillHubCallback done) {
  RequestSkillHub(index, std::move(done),
                  [query, source, limit](ProfileWorker* worker, quint64 token) {
                    QMetaObject::invokeMethod(
                        worker, "SearchSkillHub", Qt::QueuedConnection,
                        Q_ARG(quint64, token), Q_ARG(QString, query),
                        Q_ARG(QString, source), Q_ARG(int, limit));
                  });
}

void Supervisor::PreviewSkillHub(int index, const QString& identifier,
                                 SkillHubCallback done) {
  RequestSkillHub(index, std::move(done),
                  [identifier](ProfileWorker* worker, quint64 token) {
                    QMetaObject::invokeMethod(
                        worker, "PreviewSkillHub", Qt::QueuedConnection,
                        Q_ARG(quint64, token), Q_ARG(QString, identifier));
                  });
}

void Supervisor::ScanSkillHub(int index, const QString& identifier,
                              SkillHubCallback done) {
  RequestSkillHub(index, std::move(done),
                  [identifier](ProfileWorker* worker, quint64 token) {
                    QMetaObject::invokeMethod(
                        worker, "ScanSkillHub", Qt::QueuedConnection,
                        Q_ARG(quint64, token), Q_ARG(QString, identifier));
                  });
}

void Supervisor::Stop() {
  if (stopped_) {
    return;
  }
  stopped_ = true;
  for (Entry& entry : entries_) {
    if (entry.relay_thread != nullptr && entry.relay_thread->isRunning()) {
      QMetaObject::invokeMethod(entry.relay, "Shutdown",
                                Qt::BlockingQueuedConnection);
      entry.relay_thread->quit();
      entry.relay_thread->wait();
    }
    delete entry.relay;
    entry.relay = nullptr;
    delete entry.relay_thread;
    entry.relay_thread = nullptr;
    if (entry.thread != nullptr && entry.thread->isRunning()) {
      QMetaObject::invokeMethod(entry.worker, "Shutdown",
                                Qt::BlockingQueuedConnection);
      entry.thread->quit();
      entry.thread->wait();
      delete entry.thread;
      entry.thread = nullptr;
      entry.worker = nullptr;
    } else {
      delete entry.worker;
      entry.worker = nullptr;
      delete entry.thread;
      entry.thread = nullptr;
    }
  }
  entries_.clear();
}

void Supervisor::LoadProfiles() {
  QString discover_error;
  if (!DiscoverEndpoint(&endpoint_, &discover_error)) {
    error_ = discover_error;
  }

  QStringList names;
  if (endpoint_.valid()) {
    GatewayHttp status_http(HermesHome(), QStringLiteral("default"), endpoint_);
    QJsonDocument status;
    QString status_error;
    if (status_http.GetJson(QStringLiteral("/api/status"), &status,
                            &status_error, false)) {
      const QJsonObject object = status.object();
      gateway_version_ = object.value(QStringLiteral("version"))
                             .toString(QStringLiteral("unknown"));
      const QJsonArray profiles =
          object.value(QStringLiteral("profiles")).toArray();
      for (const QJsonValue& value : profiles) {
        const QString name = value.toString();
        if (!name.isEmpty()) {
          names.push_back(name);
        }
      }
    } else if (error_.isEmpty()) {
      error_ = status_error;
    }
  }

  if (names.isEmpty()) {
    names.push_back(QStringLiteral("default"));
  }
  names.removeDuplicates();
  names.removeAll(QStringLiteral("default"));
  names.push_front(QStringLiteral("default"));

  entries_.reserve(static_cast<std::size_t>(names.size()));
  for (const QString& name : names) {
    Entry entry;
    entry.name = name;
    entry.label = name;
    entry.color = ProfileColorFor(name);
    entry.is_default = name == QStringLiteral("default");
    entry.worker = new ProfileWorker(name, ProfileHome(name), entry.color,
                                     entry.is_default, endpoint_,
                                     gateway_version_, enable_streaming_);
    entries_.push_back(std::move(entry));
  }
}

int Supervisor::profile_count() const {
  return static_cast<int>(entries_.size());
}

int Supervisor::active_index() const { return active_index_; }

void Supervisor::Activate(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  active_index_ = index;
  for (int i = 0; i < profile_count(); ++i) {
    Entry& entry = entries_[static_cast<std::size_t>(i)];
    if (entry.worker != nullptr) {
      entry.worker->SetForeground(i == index);
    }
  }
  // Prewarm only the active profile's relay. Seven idle control sockets would
  // duplicate gateway connections for profiles the user is not looking at.
  StartRelay(&entries_[static_cast<std::size_t>(index)]);
}

void Supervisor::ActivateByName(const QString& name) {
  for (int index = 0; index < profile_count(); ++index) {
    if (ProfileName(index) == name) {
      Activate(index);
      return;
    }
  }
}

std::shared_ptr<const ProfileSnapshot> Supervisor::ActiveSnapshot() const {
  return SnapshotAt(active_index_);
}

std::shared_ptr<const ProfileSnapshot> Supervisor::SnapshotAt(int index) const {
  if (index < 0 || index >= profile_count()) {
    return nullptr;
  }
  const Entry& entry = entries_[static_cast<std::size_t>(index)];
  return entry.worker == nullptr ? nullptr : entry.worker->Snapshot();
}

void Supervisor::SelectSession(int index, const QString& session_id) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "SelectSession", Qt::QueuedConnection,
                              Q_ARG(QString, session_id));
  }
}

void Supervisor::SendPrompt(int index, const QString& text) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "SendPrompt", Qt::QueuedConnection,
                              Q_ARG(QString, text));
  }
}

void Supervisor::QueuePrompt(int index, const QString& text) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "QueuePrompt", Qt::QueuedConnection,
                              Q_ARG(QString, text));
  }
}

void Supervisor::SendPromptWithAttachments(int index, const QString& text,
                                           const QStringList& attachments) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "SendPromptWithAttachments",
                              Qt::QueuedConnection, Q_ARG(QString, text),
                              Q_ARG(QStringList, attachments));
  }
}

void Supervisor::SendPromptAt(int index, const QString& text,
                              qint64 truncate_row_id,
                              bool confirm_empty_truncate,
                              bool confirm_deep_truncate) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, "SendPromptAt", Qt::QueuedConnection, Q_ARG(QString, text),
        Q_ARG(qint64, truncate_row_id), Q_ARG(bool, confirm_empty_truncate),
        Q_ARG(bool, confirm_deep_truncate));
  }
}

void Supervisor::NewSession(int index, const QString& cwd,
                            const QString& project_id) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    return;
  }
  // Always use the project-aware entry point: it treats an empty cwd the same
  // as NewSession and carries the sidebar's project tag for the placeholder.
  QMetaObject::invokeMethod(worker, "NewSessionInDir", Qt::QueuedConnection,
                            Q_ARG(QString, cwd), Q_ARG(QString, project_id));
}

void Supervisor::Interrupt(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  Entry& entry = entries_[static_cast<std::size_t>(index)];
  const std::shared_ptr<const ProfileSnapshot> snapshot =
      entry.worker == nullptr ? nullptr : entry.worker->Snapshot();
  const QString session_id =
      snapshot == nullptr ? QString() : snapshot->selected_session_id;
  if (!session_id.isEmpty() && StartRelay(&entry)) {
    // Relay-only: the worker path would read execution_generation_ when its
    // blocked event loop finally runs and could stop a later turn.
    QMetaObject::invokeMethod(entry.relay, "Interrupt", Qt::QueuedConnection,
                              Q_ARG(QString, session_id), Q_ARG(int, -1));
    return;
  }
  if (entry.worker != nullptr) {
    QMetaObject::invokeMethod(entry.worker, "Interrupt", Qt::QueuedConnection);
  }
}

void Supervisor::UseDefaultModel(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "UseDefaultModel", Qt::QueuedConnection);
  }
}

void Supervisor::RespondRequest(int index, const QString& request_id,
                                const QJsonObject& result) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "RespondRequest", Qt::QueuedConnection,
                              Q_ARG(QString, request_id),
                              Q_ARG(QJsonObject, result));
  }
}

void Supervisor::RememberAccountOperation(int index,
                                          const QJsonObject& result) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "RememberAccountOperation",
                              Qt::QueuedConnection, Q_ARG(QJsonObject, result));
  }
}

void Supervisor::SetModel(int index, const QString& provider,
                          const QString& model) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "SetModel", Qt::QueuedConnection,
                              Q_ARG(QString, provider), Q_ARG(QString, model));
  }
}

void Supervisor::RefreshModelOptions(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "RefreshModelOptions",
                              Qt::QueuedConnection);
  }
}

void Supervisor::SetReasoning(int index, const QString& effort) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(worker, "SetReasoning", Qt::QueuedConnection,
                              Q_ARG(QString, effort));
  }
}

void Supervisor::FetchSettings(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker]() { worker->FetchSettings(); }, Qt::QueuedConnection);
  }
}

void Supervisor::SaveConfig(int index, const QJsonObject& patch) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, patch]() { worker->SaveConfig(patch); },
        Qt::QueuedConnection);
  }
}

void Supervisor::FetchPage(int index, const QString& page) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, page]() { worker->FetchPage(page); },
        Qt::QueuedConnection);
  }
}

void Supervisor::PageAction(int index, const QString& page,
                            const QString& method, const QString& path,
                            const QJsonObject& body) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker,
        [worker, page, method, path, body]() {
          worker->PageAction(page, method, path, body);
        },
        Qt::QueuedConnection);
  }
}

void Supervisor::FetchArtifacts(int index) {
  FetchPage(index, QStringLiteral("artifacts"));
}

void Supervisor::SetArtifactsRoot(int index, const QString& path) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, path]() { worker->SetArtifactsRoot(path); },
        Qt::QueuedConnection);
  }
}

void Supervisor::OpenArtifactsDir(int index, const QString& path) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, path]() { worker->OpenArtifactsDir(path); },
        Qt::QueuedConnection);
  }
}

void Supervisor::PreviewArtifact(int index, const QString& path) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, path]() { worker->PreviewArtifact(path); },
        Qt::QueuedConnection);
  }
}

void Supervisor::SaveArtifact(int index, const QString& path,
                              const QString& text) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, path, text]() { worker->SaveArtifact(path, text); },
        Qt::QueuedConnection);
  }
}

void Supervisor::PatchSession(int index, const QString& session_id,
                              const QJsonObject& patch) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker,
        [worker, session_id, patch]() {
          worker->PatchSession(session_id, patch);
        },
        Qt::QueuedConnection);
  }
}

void Supervisor::DeleteSession(int index, const QString& session_id) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, session_id]() { worker->DeleteSession(session_id); },
        Qt::QueuedConnection);
  }
}

void Supervisor::AttachFile(int index, const QString& local_path) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, local_path]() { worker->AttachFile(local_path); },
        Qt::QueuedConnection);
  }
}

void Supervisor::ExecuteSlash(int index, const QString& command) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, command]() { worker->ExecuteSlash(command); },
        Qt::QueuedConnection);
  }
}

void Supervisor::Steer(int index, const QString& text, SteerCallback done) {
  if (index < 0 || index >= profile_count()) {
    if (done) {
      done(false, QStringLiteral("No active profile"));
    }
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    if (done) {
      done(false, QStringLiteral("Profile is not running"));
    }
    return;
  }
  if (!done) {
    QMetaObject::invokeMethod(
        worker, [worker, text]() { worker->Steer(text); },
        Qt::QueuedConnection);
    return;
  }
  const quint64 token = next_steer_token_++;
  steer_callbacks_[token] = std::move(done);
  QMetaObject::invokeMethod(
      worker,
      [worker, text, token]() {
        worker->SteerWithCallback(
            text, [worker, token](bool ok, const QString& message) {
              emit worker->SteerFinished(token, ok, message);
            });
      },
      Qt::QueuedConnection);
}

void Supervisor::RecordInjectedPrompt(int index, const QString& text,
                                      const QString& admission_id) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker,
        [worker, text, admission_id]() {
          worker->RecordInjectedPrompt(text, admission_id);
        },
        Qt::QueuedConnection);
  }
}

void Supervisor::ForgetPendingAdmission(int index,
                                        const QString& admission_id) {
  if (index < 0 || index >= profile_count() || admission_id.isEmpty()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker,
        [worker, admission_id]() {
          worker->ForgetPendingAdmission(admission_id);
        },
        Qt::QueuedConnection);
  }
}

void Supervisor::Note(int index, const QString& text) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker, text]() { worker->Note(text); }, Qt::QueuedConnection);
  }
}

void Supervisor::FetchSlashCatalog(int index) {
  if (index < 0 || index >= profile_count()) {
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker != nullptr) {
    QMetaObject::invokeMethod(
        worker, [worker]() { worker->FetchSlashCatalog(); },
        Qt::QueuedConnection);
  }
}

bool Supervisor::ListDirectory(const QString& path, QJsonArray* entries,
                               QString* error) {
  if (path.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Directory path is empty");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  QString request_error;
  const QString route =
      QStringLiteral("/api/fs/list?path=%1&profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(path)),
               QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  if (!http.GetJson(route, &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (entries != nullptr) {
    *entries = document.object().value(QStringLiteral("entries")).toArray();
  }
  return true;
}

bool Supervisor::UploadArtifact(const QString& destination,
                                const QString& local_path,
                                QString* uploaded_path, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  return http.PostMultipart(QStringLiteral("/api/files/upload-stream"),
                            local_path,
                            QJsonObject{{QStringLiteral("path"), destination},
                                        {QStringLiteral("overwrite"), true}},
                            uploaded_path, error);
}

bool Supervisor::DownloadArtifact(const QString& remote_path,
                                  const QString& local_path, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/fs/download?path=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(remote_path)));
  return http.DownloadFile(route, local_path, error);
}

bool Supervisor::TestMcpServer(const QString& name, QJsonObject* result,
                               QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/mcp/servers/%1/test")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
  if (!http.PostJson(route, QJsonObject(), &document, error)) {
    return false;
  }
  if (result != nullptr) {
    *result = document.object();
  }
  return true;
}

bool Supervisor::ListMcpCatalog(QJsonArray* entries, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/mcp/catalog?profile=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (entries != nullptr) {
    *entries = document.object().value(QStringLiteral("entries")).toArray();
  }
  return true;
}

bool Supervisor::GetMcpConfig(QJsonObject* servers, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/config?include_defaults=false&profile=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (servers != nullptr) {
    *servers =
        document.object().value(QStringLiteral("mcp_servers")).toObject();
  }
  return true;
}

bool Supervisor::ReplaceMcpConfig(const QJsonObject& servers, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/mcp/servers?profile=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  return http.PutJson(route, QJsonObject{{QStringLiteral("servers"), servers}},
                      &document, error);
}

bool Supervisor::GetLearningNode(const QString& id, QJsonObject* detail,
                                 QString* error) {
  if (id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No star map node selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/learning/node?id=%1&profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(id)),
               QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (detail != nullptr) {
    *detail = document.object();
  }
  return true;
}

bool Supervisor::StartMcpOAuth(const QString& name, QJsonObject* flow,
                               QString* error) {
  if (name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No MCP server selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/mcp/servers/%1/auth")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
  if (!http.PostJson(route, QJsonObject(), &document, error)) {
    return false;
  }
  if (flow != nullptr) {
    *flow = document.object();
  }
  return true;
}

bool Supervisor::PollMcpOAuth(const QString& flow_id, QJsonObject* flow,
                              QString* error) {
  if (flow_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No MCP OAuth flow selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/mcp/oauth/flows/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(flow_id)));
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (flow != nullptr) {
    *flow = document.object();
  }
  return true;
}

bool Supervisor::CancelMcpOAuth(const QString& flow_id, QString* error) {
  if (flow_id.isEmpty()) {
    return true;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/mcp/oauth/flows/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(flow_id)));
  return http.DeleteJson(route, QJsonObject(), &document, error);
}

bool Supervisor::TestMessagingPlatform(const QString& platform_id,
                                       QJsonObject* result, QString* error) {
  if (platform_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No messaging platform selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/platforms/%1/test")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(platform_id)));
  if (!http.PostJson(route, QJsonObject(), &document, error)) {
    return false;
  }
  if (result != nullptr) {
    *result = document.object();
  }
  return true;
}

bool Supervisor::StartTelegramOnboarding(const QString& bot_name,
                                         QJsonObject* pairing, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  QJsonObject body;
  if (!bot_name.trimmed().isEmpty()) {
    body.insert(QStringLiteral("bot_name"), bot_name.trimmed());
  }
  if (!http.PostJson(QStringLiteral("/api/messaging/telegram/onboarding/start"),
                     body, &document, error)) {
    return false;
  }
  if (pairing != nullptr) {
    *pairing = document.object();
  }
  return true;
}

bool Supervisor::PollTelegramOnboarding(const QString& pairing_id,
                                        QJsonObject* status, QString* error) {
  if (pairing_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No Telegram setup session selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/telegram/onboarding/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)));
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (status != nullptr) {
    *status = document.object();
  }
  return true;
}

bool Supervisor::ApplyTelegramOnboarding(const QString& pairing_id,
                                         const QStringList& allowed_user_ids,
                                         QJsonObject* result, QString* error) {
  if (pairing_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No Telegram setup session selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/telegram/onboarding/%1/apply?profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)),
               QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonArray ids;
  for (const QString& id : allowed_user_ids) {
    if (!id.trimmed().isEmpty()) {
      ids.append(id.trimmed());
    }
  }
  QJsonObject body{{QStringLiteral("allowed_user_ids"), ids}};
  if (!http.PostJson(route, body, &document, error)) {
    return false;
  }
  if (result != nullptr) {
    *result = document.object();
  }
  return true;
}

bool Supervisor::CancelTelegramOnboarding(const QString& pairing_id,
                                          QString* error) {
  if (pairing_id.isEmpty()) {
    return true;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/telegram/onboarding/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)));
  return http.DeleteJson(route, QJsonObject(), &document, error);
}

bool Supervisor::StartWhatsAppOnboarding(const QString& mode,
                                         const QString& allowed_users,
                                         bool reset_session,
                                         QJsonObject* pairing, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  QJsonObject body{
      {QStringLiteral("mode"),
       mode.trimmed().isEmpty() ? QStringLiteral("bot") : mode.trimmed()},
      {QStringLiteral("allowed_users"), allowed_users.trimmed()},
      {QStringLiteral("reset_session"), reset_session}};
  if (!http.PostJson(QStringLiteral("/api/messaging/whatsapp/onboarding/start"),
                     body, &document, error)) {
    return false;
  }
  if (pairing != nullptr) {
    *pairing = document.object();
  }
  return true;
}

bool Supervisor::PollWhatsAppOnboarding(const QString& pairing_id,
                                        QJsonObject* status, QString* error) {
  if (pairing_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No WhatsApp setup session selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/whatsapp/onboarding/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)));
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (status != nullptr) {
    *status = document.object();
  }
  return true;
}

bool Supervisor::ApplyWhatsAppOnboarding(const QString& pairing_id,
                                         const QString& mode,
                                         const QString& allowed_users,
                                         QJsonObject* result, QString* error) {
  if (pairing_id.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No WhatsApp setup session selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/whatsapp/onboarding/%1/apply")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)));
  QJsonObject body;
  if (!mode.trimmed().isEmpty()) {
    body.insert(QStringLiteral("mode"), mode.trimmed());
  }
  body.insert(QStringLiteral("allowed_users"), allowed_users.trimmed());
  if (!http.PostJson(route, body, &document, error)) {
    return false;
  }
  if (result != nullptr) {
    *result = document.object();
  }
  return true;
}

bool Supervisor::CancelWhatsAppOnboarding(const QString& pairing_id,
                                          QString* error) {
  if (pairing_id.isEmpty()) {
    return true;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/messaging/whatsapp/onboarding/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(pairing_id)));
  return http.DeleteJson(route, QJsonObject(), &document, error);
}

void Supervisor::SpeakText(int index, const QString& text,
                           SpeechCallback done) {
  if (text.trimmed().isEmpty()) {
    done(QJsonObject(), QStringLiteral("There is nothing to read aloud."));
    return;
  }
  if (index < 0 || index >= profile_count()) {
    done(QJsonObject(), QStringLiteral("No active profile"));
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    done(QJsonObject(), QStringLiteral("Profile is not running"));
    return;
  }
  const quint64 token = next_speech_token_++;
  speech_callbacks_[token] = std::move(done);
  QMetaObject::invokeMethod(worker, "SpeakText", Qt::QueuedConnection,
                            Q_ARG(quint64, token), Q_ARG(QString, text));
}

void Supervisor::TranscribeAudio(int index, const QString& path,
                                 SpeechCallback done) {
  if (path.trimmed().isEmpty()) {
    done(QJsonObject(), QStringLiteral("Pick a recording first."));
    return;
  }
  if (index < 0 || index >= profile_count()) {
    done(QJsonObject(), QStringLiteral("No active profile"));
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    done(QJsonObject(), QStringLiteral("Profile is not running"));
    return;
  }
  const quint64 token = next_transcription_token_++;
  transcription_callbacks_[token] = std::move(done);
  QMetaObject::invokeMethod(worker, "TranscribeAudio", Qt::QueuedConnection,
                            Q_ARG(quint64, token), Q_ARG(QString, path));
}

void Supervisor::RequestRpc(int index, const QString& method,
                            const QJsonObject& params, RpcCallback done) {
  if (index < 0 || index >= profile_count()) {
    done(QJsonObject(), QStringLiteral("No active profile"));
    return;
  }
  ProfileWorker* worker = entries_[static_cast<std::size_t>(index)].worker;
  if (worker == nullptr) {
    done(QJsonObject(), QStringLiteral("Profile is not running"));
    return;
  }
  const quint64 token = next_rpc_token_++;
  rpc_callbacks_[token] = std::move(done);
  QMetaObject::invokeMethod(worker, "SendRpc", Qt::QueuedConnection,
                            Q_ARG(quint64, token), Q_ARG(QString, method),
                            Q_ARG(QJsonObject, params));
}

bool Supervisor::GetToolsetConfig(const QString& name, QJsonObject* config,
                                  QString* error) {
  if (name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No toolset selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QJsonDocument document;
  const QString route =
      QStringLiteral("/api/tools/toolsets/%1/config?profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)),
               QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (config != nullptr) {
    *config = document.object();
  }
  return true;
}

bool Supervisor::SaveToolsetConfig(const QString& name, const QString& provider,
                                   const QJsonObject& env, QString* error) {
  if (name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No toolset selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString suffix =
      QStringLiteral("?profile=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  if (!provider.trimmed().isEmpty() &&
      !http.PutJson(
          QStringLiteral("/api/tools/toolsets/%1/provider%2")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)), suffix),
          QJsonObject{{QStringLiteral("provider"), provider.trimmed()}},
          &document, error)) {
    return false;
  }
  if (!env.isEmpty() &&
      !http.PutJson(
          QStringLiteral("/api/tools/toolsets/%1/env%2")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)), suffix),
          QJsonObject{{QStringLiteral("env"), env}}, &document, error)) {
    return false;
  }
  return true;
}

bool Supervisor::GetSkillContent(const QString& name, QString* content,
                                 QString* path, QString* error) {
  if (name.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No skill selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/skills/content?name=%1&profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)),
               QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  const QJsonObject object = document.object();
  if (content != nullptr) {
    *content = object.value(QStringLiteral("content")).toString();
  }
  if (path != nullptr) {
    *path = object.value(QStringLiteral("path")).toString();
  }
  return true;
}

bool Supervisor::ListOfficialSkills(QJsonArray* skills, QString* error) {
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  const QString route =
      QStringLiteral("/api/skills/hub/official?profile=%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  QJsonDocument document;
  if (!http.GetJson(route, &document, error)) {
    return false;
  }
  if (skills != nullptr) {
    *skills = document.object().value(QStringLiteral("skills")).toArray();
  }
  return true;
}

bool Supervisor::ExportSession(const QString& session_id,
                               const QString& local_path, QString* error) {
  if (session_id.isEmpty() || local_path.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("No session selected.");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  GatewayHttp http(scope.home, profile, scope.endpoint);
  QString path =
      QStringLiteral("/api/sessions/%1/export")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(session_id)));
  if (!profile.isEmpty()) {
    path += QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  }
  return http.DownloadFile(path, local_path, error);
}

bool Supervisor::ImportSessions(const QString& local_path, int* imported,
                                QString* error) {
  if (imported != nullptr) {
    *imported = 0;
  }
  QFile file(local_path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error != nullptr) {
      *error = file.errorString();
    }
    return false;
  }
  // The gateway caps session imports at 25 MiB; refuse locally rather than
  // buffering more than the server would accept.
  constexpr qint64 kImportLimit = 25 * 1024 * 1024;
  if (file.size() > kImportLimit) {
    if (error != nullptr) {
      *error = QStringLiteral("Session archive is larger than 25 MiB.");
    }
    return false;
  }
  const QByteArray bytes = file.readAll();
  QJsonArray sessions;
  if (!ParseSessionImport(bytes, &sessions, error)) {
    return false;
  }
  const QString anchor =
      sessions.first().toObject().value(QStringLiteral("id")).toString();

  const RequestScope scope = CurrentRequestScope();
  const QString& profile = scope.profile;
  const QString profile_query =
      profile.isEmpty()
          ? QString()
          : QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(profile)));
  GatewayHttp http(scope.home, profile, scope.endpoint);

  // Import is fenced against the anchor session. A brand-new anchor has no
  // runtime row yet; the gateway treats revision zero as the empty baseline.
  int revision = 0;
  if (!anchor.isEmpty()) {
    QJsonDocument mutation;
    QString ignored;
    if (http.GetJson(
            QStringLiteral("/api/sessions/%1/mutation-snapshot%2")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(anchor)),
                     profile_query),
            &mutation, &ignored)) {
      revision =
          mutation.object().value(QStringLiteral("runtime_revision")).toInt();
    }
  }
  QJsonObject body{{QStringLiteral("request_id"),
                    QUuid::createUuid().toString(QUuid::WithoutBraces)},
                   {QStringLiteral("expected_revision"), revision},
                   {QStringLiteral("sessions"), sessions}};
  if (!profile.isEmpty()) {
    body.insert(QStringLiteral("profile"), profile);
  }
  QJsonDocument response;
  if (!http.PostJson(QStringLiteral("/api/sessions/import"), body, &response,
                     error)) {
    return false;
  }
  if (imported != nullptr) {
    *imported = sessions.size();
  }
  return true;
}

bool Supervisor::CreateProfile(const QString& name, QString* error,
                               bool reload) {
  if (name.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Profile name is empty");
    }
    return false;
  }
  // /api/profiles is install-wide, not profile-scoped: send it against the
  // install home with the default profile so the new directory lands where
  // discovery expects it.
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonObject body;
  body.insert(QStringLiteral("name"), name.trimmed());
  QJsonDocument document;
  QString request_error;
  if (!http.PostJson(QStringLiteral("/api/profiles"), body, &document,
                     &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (reload) {
    ReloadProfiles();
  }
  return true;
}

bool Supervisor::DeleteProfile(const QString& name, QString* error,
                               bool reload) {
  if (name.trimmed().isEmpty() || name == QStringLiteral("default")) {
    if (error != nullptr) {
      *error = QStringLiteral("The default profile cannot be deleted");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonDocument document;
  QString request_error;
  if (!http.DeleteJson(QStringLiteral("/api/profiles/%1").arg(name),
                       QJsonObject(), &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (reload) {
    ReloadProfiles();
  }
  return true;
}

bool Supervisor::GetProfileSoul(const QString& name, QString* content,
                                QString* error) {
  if (name.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Profile name is empty");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonDocument document;
  QString request_error;
  const QString path =
      QStringLiteral("/api/profiles/%1/soul")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
  if (!http.GetJson(path, &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (content != nullptr) {
    *content = document.object().value(QStringLiteral("content")).toString();
  }
  return true;
}

bool Supervisor::SaveProfileSoul(const QString& name, const QString& content,
                                 QString* error) {
  if (name.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Profile name is empty");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonDocument document;
  QString request_error;
  const QString path =
      QStringLiteral("/api/profiles/%1/soul")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
  if (!http.PutJson(path, QJsonObject{{QStringLiteral("content"), content}},
                    &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  return true;
}

bool Supervisor::ExportProfile(const QString& name, QString* archive,
                               QString* error) {
  if (name.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Profile name is empty");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonDocument document;
  QString request_error;
  const QString path =
      QStringLiteral("/api/profiles/%1/export")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)));
  if (!http.PostJson(path, QJsonObject(), &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (archive != nullptr) {
    *archive = document.object().value(QStringLiteral("archive")).toString();
  }
  return true;
}

bool Supervisor::ImportProfile(const QString& archive, QString* error,
                               bool reload) {
  if (archive.trimmed().isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Archive path is empty");
    }
    return false;
  }
  const RequestScope scope = CurrentRequestScope();
  GatewayHttp http(scope.home, QStringLiteral("default"), scope.endpoint);
  QJsonDocument document;
  QString request_error;
  if (!http.PostJson(
          QStringLiteral("/api/profiles/import"),
          QJsonObject{{QStringLiteral("archive"), archive.trimmed()}},
          &document, &request_error)) {
    if (error != nullptr) {
      *error = request_error;
    }
    return false;
  }
  if (reload) {
    ReloadProfiles();
  }
  return true;
}

void Supervisor::ReloadProfiles() {
  const QString keep =
      !entries_.empty() && active_index_ < profile_count()
          ? entries_[static_cast<std::size_t>(active_index_)].name
          : QString();
  for (Entry& entry : entries_) {
    if (entry.thread != nullptr && entry.thread->isRunning()) {
      QMetaObject::invokeMethod(entry.worker, "Shutdown",
                                Qt::BlockingQueuedConnection);
      entry.thread->quit();
      entry.thread->wait();
      delete entry.thread;
    } else {
      delete entry.worker;
    }
    entry.thread = nullptr;
    entry.worker = nullptr;
  }
  entries_.clear();
  active_index_ = 0;
  LoadProfiles();
  for (Entry& entry : entries_) {
    StartWorker(&entry);
  }
  for (int index = 0; index < profile_count(); ++index) {
    if (ProfileName(index) == keep && index != 0) {
      Activate(index);
      break;
    }
  }
  if (!entries_.empty() && active_index_ == 0) {
    Activate(0);
  }
}

QString Supervisor::ProfileName(int index) const {
  if (index < 0 || index >= profile_count()) {
    return QString();
  }
  return entries_[static_cast<std::size_t>(index)].name;
}

QString Supervisor::ProfileLabel(int index) const {
  if (index < 0 || index >= profile_count()) {
    return QString();
  }
  return entries_[static_cast<std::size_t>(index)].label;
}

QColor Supervisor::ProfileColor(int index) const {
  if (index < 0 || index >= profile_count()) {
    return QColor();
  }
  return entries_[static_cast<std::size_t>(index)].color;
}

bool Supervisor::IsDefault(int index) const {
  if (index < 0 || index >= profile_count()) {
    return false;
  }
  return entries_[static_cast<std::size_t>(index)].is_default;
}

QString Supervisor::GatewayVersion() const { return gateway_version_; }

QString Supervisor::Error() const { return error_; }

}  // namespace hermes::ui
