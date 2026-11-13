// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/profile_worker.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "src/audio_data_url.h"
#include "src/cache_store.h"
#include "src/gateway_events.h"
#include "src/gateway_http.h"
#include "src/gateway_protocol.h"
#include "src/gateway_socket.h"
#include "src/markdown.h"
#include "src/message_history.h"
#include "src/prompt_submit.h"
#include "src/session_usage.h"

namespace hermes::ui {
namespace {

// `/api/sessions` caps one page at 100. The sidebar mirrors the project tree's
// source filter so project counts and nested rows agree; it walks pages up to
// the same 500-session window instead of stopping at a stale first page.
constexpr int kSessionFetchPageSize = 100;
constexpr int kSessionFetchLimit = 500;
constexpr int kMessageFetchLimit = 500;
// The transcript is painted in full on every snapshot. Keep the window small
// until the layout cache lands; the gateway still has the full history.
constexpr int kRenderMessageLimit = 500;
constexpr qint64 kPublishIntervalMs = 120;

QString Preview(const QString& text, int max_chars = 160) {
  QString flat = text;
  flat.replace('\n', ' ');
  flat = flat.trimmed();
  if (flat.size() > max_chars) {
    flat = flat.left(max_chars - 1) + QChar(0x2026);
  }
  return flat;
}

// `connection.update` sends a full target list, but optional metadata can be
// absent. Copy only omitted fields so a state-only transition cannot erase a
// connect link or install instructions.
void MergeConnectionTargetMetadata(ConnectionTarget* target,
                                   const ConnectionTarget& cached) {
  if (target->detail.isEmpty()) {
    target->detail = cached.detail;
  }
  if (target->instructions.isEmpty()) {
    target->instructions = cached.instructions;
  }
  if (target->discovery_error.isEmpty()) {
    target->discovery_error = cached.discovery_error;
  }
  if (target->connect_url.isEmpty()) {
    target->connect_url = cached.connect_url;
  }
  if (target->connection_id.isEmpty()) {
    target->connection_id = cached.connection_id;
  }
  if (target->alias.isEmpty()) {
    target->alias = cached.alias;
  }
  if (target->tools.empty()) {
    target->tools = cached.tools;
  }
  if (target->required_env.empty()) {
    target->required_env = cached.required_env;
  }
  target->resolved = target->resolved || cached.resolved;
}

// Tool results arrive as JSON envelopes, e.g. {"output":"line1\nline2"}.
// Showing that raw escapes every newline and turns the row into one giant
// line, so unwrap the common envelope keys before display.
bool ExtractToolText(const QJsonValue& value, int depth, QString* out) {
  if (depth > 4) {
    return false;
  }
  if (value.isString()) {
    // One envelope layer only: a tool whose own output is JSON must stay
    // JSON, so do not recurse into the string payload.
    *out = value.toString();
    return true;
  }
  if (value.isObject()) {
    const QJsonObject object = value.toObject();
    for (const char* key :
         {"output", "content", "stdout", "result", "text", "message"}) {
      const auto it = object.find(QLatin1String(key));
      if (it == object.end()) {
        continue;
      }
      if (ExtractToolText(it.value(), depth + 1, out)) {
        return true;
      }
    }
  }
  return false;
}

QString ToolValueText(const QJsonValue& value) {
  if (value.isString()) {
    return value.toString();
  }
  if (value.isObject()) {
    return QString::fromUtf8(
        QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
  }
  if (value.isArray()) {
    return QString::fromUtf8(
        QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
  }
  return value.toVariant().toString();
}

QString ToolOutput(const QString& text) {
  QString output = text;
  const QString trimmed = output.trimmed();
  if (trimmed.startsWith(QLatin1Char('{')) ||
      trimmed.startsWith(QLatin1Char('[')) ||
      trimmed.startsWith(QLatin1Char('"'))) {
    QJsonParseError error;
    const QJsonDocument document =
        QJsonDocument::fromJson(trimmed.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError) {
      const QJsonValue root = document.isObject()
                                  ? QJsonValue(document.object())
                                  : QJsonValue(document.array());
      QString extracted;
      if (ExtractToolText(root, 0, &extracted)) {
        output = extracted;
      }
    }
  }
  while (output.endsWith(QLatin1Char('\n'))) {
    output.chop(1);
  }
  // Safety valve for pathological logs, not for ordinary tool output: 1 MB
  // is far above a terminal pane and still keeps snapshots bounded.
  constexpr int kMaxToolOutputChars = 1024 * 1024;
  if (output.size() > kMaxToolOutputChars) {
    output = output.left(kMaxToolOutputChars) +
             QStringLiteral("\n… output capped at 1 MB");
  }
  return output;
}

QString ArgumentsPreview(const QJsonValue& value) {
  if (value.isString()) {
    return Preview(value.toString(), 90);
  }
  if (value.isObject()) {
    return Preview(
        QString::fromUtf8(
            QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact)),
        90);
  }
  return QString();
}

QDateTime MessageTime(const QJsonObject& row) {
  const double seconds = row.value(QStringLiteral("timestamp")).toDouble(0.0);
  if (seconds <= 0.0) {
    return QDateTime::currentDateTime();
  }
  return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds));
}

QString SessionTitle(const QJsonObject& row) {
  const QString title = row.value(QStringLiteral("title")).toString().trimmed();
  if (!title.isEmpty()) {
    return title;
  }
  const QString preview =
      row.value(QStringLiteral("preview")).toString().trimmed();
  return preview.isEmpty() ? QStringLiteral("Untitled") : Preview(preview, 80);
}

bool IsToday(qint64 seconds) {
  if (seconds <= 0) {
    return false;
  }
  return QDateTime::fromSecsSinceEpoch(seconds).date() == QDate::currentDate();
}

bool SessionsEqual(const std::vector<SessionSummary>& left,
                   const std::vector<SessionSummary>& right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0; index < left.size(); ++index) {
    const SessionSummary& a = left[index];
    const SessionSummary& b = right[index];
    if (a.id != b.id || a.title != b.title || a.preview != b.preview ||
        a.source != b.source || a.project_id != b.project_id ||
        a.model != b.model || a.provider != b.provider ||
        a.reasoning_effort != b.reasoning_effort ||
        a.started_at_secs != b.started_at_secs ||
        a.last_active_secs != b.last_active_secs ||
        a.message_count != b.message_count || a.pinned != b.pinned ||
        a.archived != b.archived || a.unread != b.unread ||
        a.parent_session_id != b.parent_session_id ||
        a.branched_from != b.branched_from || a.reset_from != b.reset_from ||
        a.continuation_kind != b.continuation_kind ||
        a.revision != b.revision) {
      return false;
    }
  }
  return true;
}

QJsonArray DisplayReactions(const QJsonObject& row) {
  QJsonValue metadata = row.value(QStringLiteral("display_metadata"));
  if (metadata.isString()) {
    const QJsonDocument parsed =
        QJsonDocument::fromJson(metadata.toString().toUtf8());
    metadata = parsed.object();
  }
  if (!metadata.isObject()) {
    return QJsonArray();
  }
  return metadata.toObject().value(QStringLiteral("reactions")).toArray();
}

}  // namespace

ProfileWorker::ProfileWorker(QString name, QString home, QColor color,
                             bool is_default, GatewayEndpoint endpoint,
                             QString gateway_version, bool enable_streaming,
                             QObject* parent)
    : QObject(parent),
      name_(std::move(name)),
      home_(std::move(home)),
      color_(std::move(color)),
      is_default_(is_default),
      endpoint_(std::move(endpoint)),
      gateway_version_(std::move(gateway_version)),
      enable_streaming_(enable_streaming) {
  state_.name = name_;
  state_.label = name_;
  state_.home = home_;
  artifacts_root_ = home_;
  state_.color = color_;
  state_.is_default = is_default_;
  state_.gateway_version = gateway_version_;
  state_.status_text = QStringLiteral("connecting");
  published_ = std::make_shared<ProfileSnapshot>(state_);
}

ProfileWorker::~ProfileWorker() {
  if (!shutting_down_) {
    Shutdown();
  }
  delete socket_;
  socket_ = nullptr;
  delete http_;
  http_ = nullptr;
}

void ProfileWorker::Start() {
  if (http_ != nullptr || shutting_down_) {
    return;
  }
  // Tickets are minted by the owner's control socket at the root home; the
  // profile home only names the authority in the ticket request.
  http_ = new GatewayHttp(HermesHome(), name_, endpoint_);

  refresh_timer_ = new QTimer(this);
  refresh_timer_->setInterval(10000);
  connect(refresh_timer_, &QTimer::timeout, this, &ProfileWorker::Refresh);
  refresh_timer_->start();

  message_refresh_timer_ = new QTimer(this);
  message_refresh_timer_->setSingleShot(true);
  connect(message_refresh_timer_, &QTimer::timeout, this, [this]() {
    if (!state_.selected_session_id.isEmpty()) {
      FetchMessages(state_.selected_session_id);
    }
  });

  cache_timer_ = new QTimer(this);
  cache_timer_->setInterval(3000);
  connect(cache_timer_, &QTimer::timeout, this,
          [this]() { FlushSessionCache(); });
  cache_timer_->start();

  // Paint the last workspace before the gateway answers. The cached rows are
  // explicitly not `history_loaded_`, so the canonical REST page replaces
  // them as soon as it arrives instead of pinning stale content forever.
  LoadCachedState();

  // Fetch history before opening the socket: a resume replays events, and the
  // REST page is the canonical base for those deltas.
  FetchSessions();

  if (enable_streaming_) {
    socket_ = new GatewaySocket(HermesHome(), name_, endpoint_, this);
    connect(socket_, &GatewaySocket::Connected, this,
            &ProfileWorker::OnSocketConnected);
    connect(socket_, &GatewaySocket::Disconnected, this,
            &ProfileWorker::OnSocketDisconnected);
    connect(socket_, &GatewaySocket::EventReceived, this,
            &ProfileWorker::OnSocketEvent);
    connect(socket_, &GatewaySocket::ResponseReceived, this,
            &ProfileWorker::OnSocketResponse);
    connect(socket_, &GatewaySocket::ReplayGap, this,
            &ProfileWorker::OnReplayGap);
    connect(socket_, &GatewaySocket::SocketError, this,
            [this](const QString& message) { PublishError(message); });
    connect(socket_, &GatewaySocket::ServerRequestReceived, this,
            [this](const QJsonValue& id, const QString& method,
                   const QJsonObject& params) {
              AcceptServerRequest(id, method, params);
            });
    socket_->Connect();
  }
}

void ProfileWorker::Shutdown() {
  shutting_down_ = true;
  if (refresh_timer_ != nullptr) {
    refresh_timer_->stop();
  }
  if (message_refresh_timer_ != nullptr) {
    message_refresh_timer_->stop();
  }
  if (cache_timer_ != nullptr) {
    cache_timer_->stop();
  }
  FlushSessionCache();
  // Abort, do not delete: a GET may be parked in a nested event loop on this
  // thread. The destructor owns both objects once the thread has stopped.
  if (http_ != nullptr) {
    http_->Abort();
  }
  if (socket_ != nullptr) {
    socket_->Close();
  }
}

std::shared_ptr<const ProfileSnapshot> ProfileWorker::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return published_;
}

void ProfileWorker::SetForeground(bool foreground) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (foreground_ == foreground) {
      return;
    }
    foreground_ = foreground;
    if (foreground_ && state_.unread != 0) {
      state_.unread = 0;
      PublishLocked();
    }
  }
  if (foreground && !models_loaded_) {
    // Only the visible profile pays the provider-catalog probe. Six workers
    // fetching the same catalog at once made the REST picker look stuck.
    QTimer::singleShot(0, this, [this]() { FetchModelOptions(); });
  }
  if (foreground) {
    // SetForeground runs on the UI thread; attach the session on the worker
    // thread, where the socket lives.
    QMetaObject::invokeMethod(this, "AttachIfForeground", Qt::QueuedConnection);
  }
}

void ProfileWorker::AttachIfForeground() {
  QString session_id;
  bool need_history = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!foreground_) {
      return;
    }
    session_id = state_.selected_session_id;
    need_history = !history_loaded_;
  }
  if (session_id.isEmpty()) {
    return;
  }
  if (need_history) {
    FetchMessages(session_id);
  }
  if (socket_ != nullptr && socket_->IsConnected() &&
      attached_session_id_ != session_id) {
    socket_->ResumeSession(session_id);
    attached_session_id_ = session_id;
    if (!catalog_requested_) {
      catalog_requested_ = true;
      FetchSlashCatalog();
    }
  }
}

void ProfileWorker::SelectSession(const QString& session_id) {
  if (session_id.isEmpty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_session_id == session_id && !state_.messages.empty()) {
      return;
    }
    if (state_.selected_session_id != session_id) {
      pinned_session_id_.clear();
      pinned_provider_.clear();
      pinned_model_.clear();
    }
    state_.selected_session_id = session_id;
    // Never let the previous chat's runtime controls leak into a placeholder
    // session that has not reached /api/sessions yet.
    state_.model.clear();
    state_.provider.clear();
    state_.reasoning_effort = QStringLiteral("medium");
    for (const SessionSummary& session : state_.sessions) {
      if (session.id == session_id) {
        state_.model = session.model;
        state_.provider = session.provider;
        if (!session.reasoning_effort.isEmpty()) {
          state_.reasoning_effort = session.reasoning_effort;
        }
        break;
      }
    }
    state_.messages.clear();
    state_.unread = 0;
    state_.error_text.clear();
    state_.status_text = QStringLiteral("loading");
    history_loaded_ = false;
    attached_session_id_.clear();
    runtime_session_id_.clear();
    pending_attached_prompt_.reset();
    state_.requests.clear();
    state_.todos.clear();
    state_.todo_revision = 0;
    state_.session_control_summary.clear();
    ++state_.session_control_serial;
    state_.pending_admissions.clear();
    state_.pending_authority_epoch = 0;
    state_.pending_revision = -1;
    pending_prompts_.Reset();
    pending_prompts_baseline_ready_ = false;
    admission_id_.clear();
    admission_status_.clear();
    pending_receipt_id_ = -1;
    PublishLocked();
  }
  FetchMessages(session_id);
  AttachIfForeground();
}

void ProfileWorker::SendPrompt(const QString& text) {
  SendPromptInternal(text, 0, false, false);
}

void ProfileWorker::SendPromptWithCallback(const QString& text,
                                           SteerCallback done) {
  SendPromptInternal(text, 0, false, false, {}, false, std::move(done));
}

void ProfileWorker::QueuePrompt(const QString& text) {
  SendPromptInternal(text, 0, false, false, {}, true);
}

void ProfileWorker::SendPromptWithAttachments(const QString& text,
                                              const QStringList& attachments) {
  SendPromptInternal(text, 0, false, false, attachments);
}

void ProfileWorker::SendPromptAt(const QString& text,
                                 qint64 truncate_before_row_id,
                                 bool confirm_empty_truncate,
                                 bool confirm_deep_truncate) {
  SendPromptInternal(text, truncate_before_row_id, confirm_empty_truncate,
                     confirm_deep_truncate);
}

void ProfileWorker::SendPromptAtWithAttachments(
    const QString& text, qint64 truncate_before_row_id,
    bool confirm_empty_truncate, bool confirm_deep_truncate,
    const QStringList& attachments) {
  SendPromptInternal(text, truncate_before_row_id, confirm_empty_truncate,
                     confirm_deep_truncate, attachments);
}

void ProfileWorker::SubmitPromptFrame(const QString& session_id,
                                      const QString& text,
                                      qint64 truncate_before_row_id,
                                      bool confirm_empty_truncate,
                                      bool confirm_deep_truncate, bool queued,
                                      SteerCallback done) {
  if (socket_ == nullptr || !socket_->IsConnected()) {
    if (done) {
      done(false, QStringLiteral("Gateway socket is not connected"));
    }
    PublishError(QStringLiteral("Gateway socket is not connected"));
    return;
  }
  qInfo("send session=%.12s chars=%lld truncate_row=%lld",
        qUtf8Printable(session_id), static_cast<qint64>(text.size()),
        truncate_before_row_id);
  pending_prompt_queued_ = queued;
  if (truncate_before_row_id > 0) {
    pending_prompt_request_id_ =
        socket_->SendPromptAt(session_id, text, truncate_before_row_id,
                              confirm_empty_truncate, confirm_deep_truncate);
  } else {
    pending_prompt_request_id_ = socket_->SendPrompt(session_id, text, queued);
  }
  pending_prompt_session_id_ = session_id;
  if (pending_prompt_request_id_ < 0) {
    if (done) {
      done(false, QStringLiteral("Could not submit the prompt"));
    }
    PublishError(QStringLiteral("Could not submit the prompt"));
    return;
  }
  if (done) {
    pending_prompt_callbacks_[pending_prompt_request_id_] = std::move(done);
  }
}

bool ProfileWorker::QueuePromptAttachments(const QString& session_id,
                                           const QString& text,
                                           qint64 truncate_before_row_id,
                                           bool confirm_empty_truncate,
                                           bool confirm_deep_truncate,
                                           const QStringList& attachments,
                                           SteerCallback done) {
  if (attachments.isEmpty() || socket_ == nullptr || !socket_->IsConnected()) {
    return false;
  }
  auto pending = std::make_unique<PendingAttachedPrompt>();
  pending->session_id = session_id;
  pending->text = text;
  pending->truncate_before_row_id = truncate_before_row_id;
  pending->confirm_empty_truncate = confirm_empty_truncate;
  pending->confirm_deep_truncate = confirm_deep_truncate;
  for (const QString& path : attachments) {
    if (path.trimmed().isEmpty()) {
      continue;
    }
    const int id =
        socket_->RequestRpc(AttachmentAttachMethod(path),
                            BuildAttachmentAttachParams(path, session_id));
    if (id >= 0) {
      pending->request_ids.insert(id);
    }
  }
  if (pending->request_ids.empty()) {
    return false;
  }
  pending->done = std::move(done);
  pending_attached_prompt_ = std::move(pending);
  return true;
}

void ProfileWorker::SendPromptInternal(const QString& text,
                                       qint64 truncate_before_row_id,
                                       bool confirm_empty_truncate,
                                       bool confirm_deep_truncate,
                                       const QStringList& attachments,
                                       bool queued, SteerCallback done) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    if (done) {
      done(false, QStringLiteral("A prompt cannot be empty"));
    }
    return;
  }
  QString session_id;
  bool missing_session = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_new_session_ || pending_create_id_ >= 0) {
      // New chat was pressed but the create ACK has not landed yet. Hold the
      // prompt so it can never be sent to the previous session; an edit has no
      // row to target in the new session yet, so report that instead.
      if (truncate_before_row_id > 0) {
        missing_session = true;
      } else {
        pending_prompt_ = trimmed;
        deferred_prompt_callback_ = std::move(done);
        return;
      }
    }
    if (!missing_session) {
      session_id = state_.selected_session_id;
    }
    if (session_id.isEmpty()) {
      // A rewind without a session is an error; otherwise hold ordinary
      // prompts until NewSession's ACK arrives.
      missing_session = truncate_before_row_id > 0;
      if (!missing_session) {
        pending_prompt_ = trimmed;
        deferred_prompt_callback_ = std::move(done);
      }
    }
  }
  if (missing_session) {
    if (done) {
      done(false, QStringLiteral("No session is selected to edit"));
    }
    PublishError(QStringLiteral("No session is selected to edit"));
    return;
  }
  if (session_id.isEmpty()) {
    NewSession();
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (truncate_before_row_id > 0) {
      auto target =
          std::find_if(state_.messages.begin(), state_.messages.end(),
                       [truncate_before_row_id](const ChatMessage& message) {
                         return message.role == MessageRole::kUser &&
                                message.row_id == truncate_before_row_id;
                       });
      if (target != state_.messages.end()) {
        state_.messages.erase(target, state_.messages.end());
      }
    }
    if (!queued) {
      ChatMessage message;
      message.role = MessageRole::kUser;
      message.author = QStringLiteral("You");
      message.timestamp = QDateTime::currentDateTime();
      message.raw_text = trimmed;
      message.blocks = ParseMarkdown(trimmed);
      state_.messages.push_back(std::move(message));
      state_.running = true;
      state_.running_session_id = state_.selected_session_id;
      state_.error_text.clear();
      state_.status_text = QStringLiteral("working");
      foreground_ = true;
      state_.unread = 0;
    }
    PublishLocked();
  }

  if (QueuePromptAttachments(session_id, trimmed, truncate_before_row_id,
                             confirm_empty_truncate, confirm_deep_truncate,
                             attachments, std::move(done))) {
    return;
  }
  SubmitPromptFrame(session_id, trimmed, truncate_before_row_id,
                    confirm_empty_truncate, confirm_deep_truncate, queued,
                    std::move(done));
}

void ProfileWorker::NewSession() { NewSessionInDir(QString()); }

void ProfileWorker::NewSessionInDir(const QString& cwd,
                                    const QString& project_id) {
  if (socket_ == nullptr || !socket_->IsConnected()) {
    // A New chat pressed while the socket is still dialing waits for the
    // connection instead of failing the gesture.
    pending_new_session_ = true;
    pending_new_session_cwd_ = cwd;
    pending_new_session_project_id_ = project_id;
    std::lock_guard<std::mutex> lock(mutex_);
    state_.status_text = QStringLiteral("waiting for gateway");
    PublishLocked();
    return;
  }
  pending_create_project_id_ = project_id;
  pending_create_id_ = socket_->CreateSession(QString(), cwd);
  if (pending_create_id_ < 0) {
    pending_create_project_id_.clear();
    PublishError(QStringLiteral("Could not request a new session"));
    pending_prompt_.clear();
    auto deferred = std::move(deferred_prompt_callback_);
    if (deferred) {
      deferred(false, QStringLiteral("Could not request a new session"));
    }
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  state_.status_text = QStringLiteral("creating session");
  PublishLocked();
}

void ProfileWorker::Interrupt() {
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
    state_.status_text = QStringLiteral("stopping");
    PublishLocked();
  }
  if (!session_id.isEmpty() && socket_ != nullptr && socket_->IsConnected()) {
    socket_->InterruptSession(session_id, execution_generation_);
  }
}

void ProfileWorker::Refresh() {
  if (shutting_down_ || http_ == nullptr) {
    return;
  }
  FetchSessions();
  // The socket event stream is the fast path, but a dropped/detached stream
  // used to freeze the visible turn forever: the 700 ms post-turn refresh is
  // the only history read, and it may race the prompt commit. Reconcile a
  // running foreground session when no event has arrived for a while. The
  // history page is authoritative, so this also recovers turns that started
  // during an Inject cancel/resubmit.
  QString selected;
  bool stale = false;
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    selected = state_.selected_session_id;
    stale = foreground_ && history_loaded_ && state_.running &&
            !selected.isEmpty() && last_event_ms_ > 0 &&
            now - last_event_ms_ > 15000;
    if (stale) {
      last_event_ms_ = now;
    }
  }
  if (stale) {
    qInfo("profile %s: reconciling stalled session=%s", qUtf8Printable(name_),
          qUtf8Printable(selected));
    FetchMessages(selected);
    // Re-attach as well: the REST page repairs durable rows, while resume
    // re-delivers the event ring and reports `running`/pending so the spinner
    // and queued rows converge even if the old subscription went silent.
    if (socket_ != nullptr && socket_->IsConnected()) {
      socket_->ResumeSession(selected);
      attached_session_id_ = selected;
    }
  }
}

void ProfileWorker::LoadCachedState() {
  CachedSessionState cache;
  if (!LoadSessionCache(name_, &cache)) {
    return;
  }
  if (cache.sessions.empty() && cache.messages.empty() &&
      cache.selected_session_id.isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  state_.sessions = std::move(cache.sessions);
  state_.selected_session_id = std::move(cache.selected_session_id);
  state_.messages = std::move(cache.messages);
  state_.today_count = 0;
  for (const SessionSummary& session : state_.sessions) {
    if (!IsToday(session.last_active_secs)) {
      break;
    }
    ++state_.today_count;
  }
  for (const SessionSummary& session : state_.sessions) {
    if (session.id == state_.selected_session_id) {
      state_.model = session.model;
      state_.provider = session.provider;
      state_.reasoning_effort = session.reasoning_effort.isEmpty()
                                    ? QStringLiteral("medium")
                                    : session.reasoning_effort;
      break;
    }
  }
  state_.connected = false;
  state_.gateway_ready = false;
  state_.running = false;
  state_.status_text = QStringLiteral("syncing");
  state_.error_text.clear();
  // Keep history_loaded_ false: the gateway REST page is still authoritative
  // and will replace this snapshot as soon as the worker reaches it.
  history_loaded_ = false;
  PublishLocked();
}

void ProfileWorker::FlushSessionCache() {
  CachedSessionState cache;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cache_dirty_) {
      return;
    }
    cache_dirty_ = false;
    if (state_.sessions.empty() && state_.messages.empty() &&
        state_.selected_session_id.isEmpty()) {
      return;
    }
    cache.profile = name_;
    cache.selected_session_id = state_.selected_session_id;
    cache.sessions = state_.sessions;
    cache.messages = state_.messages;
  }
  QString error;
  if (!SaveSessionCache(cache, &error) && !shutting_down_) {
    qWarning("profile %s: session cache: %s", qUtf8Printable(name_),
             qUtf8Printable(error));
  }
}

void ProfileWorker::FetchSessions() {
  if (shutting_down_ || http_ == nullptr) {
    return;
  }

  const auto parse_rows = [](const QJsonArray& rows) {
    std::vector<SessionSummary> parsed;
    parsed.reserve(static_cast<std::size_t>(rows.size()));
    for (const QJsonValue& value : rows) {
      const QJsonObject row = value.toObject();
      SessionSummary session;
      session.id = row.value(QStringLiteral("id")).toString();
      session.title = SessionTitle(row);
      session.preview = row.value(QStringLiteral("preview")).toString();
      session.source = row.value(QStringLiteral("source")).toString();
      session.model = row.value(QStringLiteral("model")).toString();
      session.provider = row.value(QStringLiteral("provider")).toString();
      session.reasoning_effort =
          row.value(QStringLiteral("reasoning_effort")).toString();
      session.started_at_secs = static_cast<qint64>(
          row.value(QStringLiteral("started_at")).toDouble());
      session.last_active_secs = static_cast<qint64>(
          row.value(QStringLiteral("last_active")).toDouble());
      session.message_count =
          row.value(QStringLiteral("message_count")).toInt();
      session.pinned = row.value(QStringLiteral("pinned")).toBool();
      session.archived = row.value(QStringLiteral("archived")).toBool();
      session.unread = row.value(QStringLiteral("unread")).toBool();
      session.parent_session_id =
          row.value(QStringLiteral("parent_session_id")).toString();
      session.branched_from =
          row.value(QStringLiteral("_branched_from")).toString();
      session.reset_from = row.value(QStringLiteral("_reset_from")).toString();
      session.continuation_kind =
          row.value(QStringLiteral("continuation_kind")).toString();
      session.revision = row.value(QStringLiteral("runtime_revision")).toInt();
      if (!session.id.isEmpty()) {
        parsed.push_back(std::move(session));
      }
    }
    return parsed;
  };

  std::vector<SessionSummary> sessions;
  // Pinned rows are back-filled outside the page window, so pages can repeat
  // ids; dedupe while keeping the first (most relevant) occurrence.
  std::set<QString> seen_ids;
  int total = 0;
  for (int offset = 0; offset < kSessionFetchLimit;
       offset += kSessionFetchPageSize) {
    QJsonDocument document;
    QString error;
    if (!http_->GetJson(
            QStringLiteral(
                "/api/sessions?limit=%1&offset=%2&order=recent"
                "&exclude_sources=acp,cron,kanban,oneshot,subagent,tool")
                .arg(kSessionFetchPageSize)
                .arg(offset),
            &document, &error)) {
      if (offset == 0) {
        PublishError(error);
        return;
      }
      qWarning("profile %s: session page %d: %s", qUtf8Printable(name_), offset,
               qUtf8Printable(error));
      break;
    }

    const QJsonObject page = document.object();
    if (offset == 0) {
      total = page.value(QStringLiteral("total")).toInt();
    }
    const QJsonArray rows = page.value(QStringLiteral("sessions")).toArray();
    for (SessionSummary& session : parse_rows(rows)) {
      if (seen_ids.insert(session.id).second) {
        sessions.push_back(std::move(session));
      }
    }
    if (rows.isEmpty() || rows.size() < kSessionFetchPageSize ||
        (total > 0 && static_cast<int>(sessions.size()) >= total)) {
      break;
    }
  }

  QString fetch_session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // /api/sessions carries the model but not the session's provider or
    // reasoning effort. Keep the cached/live values for rows where the list
    // leaves them empty; otherwise every 10 s poll would blank the composer.
    std::map<QString, SessionSummary> previous_sessions;
    for (const SessionSummary& session : state_.sessions) {
      previous_sessions.emplace(session.id, session);
    }
    for (SessionSummary& session : sessions) {
      const auto previous = previous_sessions.find(session.id);
      if (previous == previous_sessions.end()) {
        continue;
      }
      if (session.model.isEmpty()) {
        session.model = previous->second.model;
      }
      if (session.provider.isEmpty()) {
        session.provider = previous->second.provider;
      }
      if (session.reasoning_effort.isEmpty()) {
        session.reasoning_effort = previous->second.reasoning_effort;
      }
      if (session.project_id.isEmpty()) {
        session.project_id = previous->second.project_id;
      }
    }
    // The worker polls sessions every 10 s. Publishing an identical list used
    // to bump the global revision, which rebuilt every visible page and reset
    // scroll positions. Only publish actual changes.
    bool changed = !SessionsEqual(state_.sessions, sessions);
    const QString previous_selection = state_.selected_session_id;
    const QString previous_model = state_.model;
    const QString previous_provider = state_.provider;
    const QString previous_reasoning = state_.reasoning_effort;
    const bool previous_connected = state_.connected;
    const QString previous_status = state_.status_text;
    const int previous_today = state_.today_count;
    state_.sessions = std::move(sessions);
    state_.today_count = 0;
    for (const SessionSummary& session : state_.sessions) {
      if (!IsToday(session.last_active_secs)) {
        break;
      }
      ++state_.today_count;
    }

    bool selected_exists = false;
    for (const SessionSummary& session : state_.sessions) {
      if (session.id == state_.selected_session_id) {
        selected_exists = true;
        break;
      }
    }
    if (!selected_exists) {
      state_.selected_session_id =
          state_.sessions.empty() ? QString() : state_.sessions.front().id;
      state_.messages.clear();
      history_loaded_ = false;
      // Prefetch the latest transcript for every profile. This is a REST
      // SQLite read (no agent build, no socket resume), so the first profile
      // pick paints history immediately instead of waiting on the network.
      fetch_session_id = state_.selected_session_id;
    } else if (!history_loaded_) {
      // The selected session came from the on-disk cache. Refresh it now even
      // if the websocket has not connected yet.
      fetch_session_id = state_.selected_session_id;
    }
    const bool pinned_selection =
        !pinned_model_.isEmpty() &&
        pinned_session_id_ == state_.selected_session_id;
    for (const SessionSummary& session : state_.sessions) {
      if (session.id != state_.selected_session_id) {
        continue;
      }
      if (pinned_selection && session.model == pinned_model_ &&
          session.provider == pinned_provider_) {
        // The backend caught up with the pick; stop overriding its rows.
        pinned_session_id_.clear();
        pinned_provider_.clear();
        pinned_model_.clear();
      }
      if (!pinned_model_.isEmpty() &&
          pinned_session_id_ == state_.selected_session_id) {
        state_.model = pinned_model_;
        state_.provider = pinned_provider_;
      } else {
        state_.model = session.model;
        state_.provider = session.provider;
      }
      state_.reasoning_effort = session.reasoning_effort.isEmpty()
                                    ? QStringLiteral("medium")
                                    : session.reasoning_effort;
      break;
    }
    state_.connected = true;
    if (state_.status_text.isEmpty() ||
        state_.status_text == QStringLiteral("connecting")) {
      state_.status_text = QStringLiteral("ready");
    }
    changed = changed || previous_selection != state_.selected_session_id ||
              previous_model != state_.model ||
              previous_provider != state_.provider ||
              previous_reasoning != state_.reasoning_effort ||
              previous_connected != state_.connected ||
              previous_status != state_.status_text ||
              previous_today != state_.today_count;
    if (changed) {
      PublishLocked();
    }
  }

  if (!fetch_session_id.isEmpty()) {
    FetchMessages(fetch_session_id);
  }
  if (!models_loaded_ && foreground_) {
    FetchModelOptions();
  }
}

void ProfileWorker::RefreshModelOptions() { FetchModelOptions(true); }

void ProfileWorker::FetchModelOptions(bool refresh) {
  if (shutting_down_ || http_ == nullptr) {
    return;
  }
  QJsonDocument document;
  QString error;
  QString path = QStringLiteral("/api/model/options?profile=%1")
                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(name_)));
  if (refresh) {
    path += QStringLiteral("&refresh=true");
  }
  if (!http_->GetJson(path, &document, &error)) {
    qInfo("model options profile=%s failed: %s", qUtf8Printable(name_),
          qUtf8Printable(error));
    return;
  }
  const QJsonObject root = document.object();
  std::vector<ModelProvider> providers;
  for (const QJsonValue& value :
       root.value(QStringLiteral("providers")).toArray()) {
    const QJsonObject row = value.toObject();
    ModelProvider provider;
    provider.name = row.value(QStringLiteral("name"))
                        .toString(row.value(QStringLiteral("slug")).toString());
    provider.slug = row.value(QStringLiteral("slug")).toString();
    for (const QJsonValue& model :
         row.value(QStringLiteral("models")).toArray()) {
      const QString id =
          model.isString()
              ? model.toString()
              : model.toObject().value(QStringLiteral("id")).toString();
      if (!id.isEmpty()) {
        provider.models.push_back(id);
      }
    }
    if (!provider.slug.isEmpty() && !provider.models.isEmpty()) {
      providers.push_back(std::move(provider));
    }
  }

  const QString root_provider =
      root.value(QStringLiteral("provider")).toString();
  const QString root_model = root.value(QStringLiteral("model")).toString();
  std::lock_guard<std::mutex> lock(mutex_);
  state_.model_providers = std::move(providers);
  if (!root_provider.isEmpty()) {
    profile_provider_ = root_provider;
  }
  if (!root_model.isEmpty()) {
    profile_model_ = root_model;
  }
  // Root defaults describe the profile's configured model, not necessarily
  // the model the user picked in this composer. Keep an explicit pick until
  // the session row or the backend confirms it.
  if (pinned_model_.isEmpty()) {
    if (!root_provider.isEmpty()) {
      state_.provider = root_provider;
    }
    if (!root_model.isEmpty()) {
      state_.model = root_model;
    }
  }
  models_loaded_ = true;
  PublishLocked();
  qInfo("model options profile=%s providers=%lld", qUtf8Printable(name_),
        static_cast<qint64>(state_.model_providers.size()));
}

void ProfileWorker::SetModel(const QString& provider, const QString& model) {
  if (provider.isEmpty() || model.isEmpty()) {
    return;
  }
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
    state_.provider = provider;
    state_.model = model;
    pinned_session_id_ = session_id;
    pinned_provider_ = provider;
    pinned_model_ = model;
    for (SessionSummary& session : state_.sessions) {
      if (session.id == session_id) {
        session.provider = provider;
        session.model = model;
        break;
      }
    }
    PublishLocked();
  }
  if (session_id.isEmpty() || socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }
  // `--session` is explicit even though an exploratory provider pick is
  // already session-scoped by default. It keeps a composer pick from ever
  // rewriting the profile's configured model.
  socket_->SetConfig(
      session_id, QStringLiteral("model"),
      QStringLiteral("%1 --provider %2 --session").arg(model, provider));
  qInfo("model profile=%s provider=%s model=%s", qUtf8Printable(name_),
        qUtf8Printable(provider), qUtf8Printable(model));
  QTimer::singleShot(800, this, [this]() { FetchModelOptions(); });
}

void ProfileWorker::UseDefaultModel() {
  QString provider;
  QString model;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    provider = profile_provider_;
    model = profile_model_;
  }
  if ((provider.isEmpty() || model.isEmpty()) && http_ != nullptr) {
    FetchModelOptions();
    std::lock_guard<std::mutex> lock(mutex_);
    provider = profile_provider_;
    model = profile_model_;
  }
  if (!provider.isEmpty() && !model.isEmpty()) {
    SetModel(provider, model);
  }
}

void ProfileWorker::SetReasoning(const QString& effort) {
  if (effort.isEmpty()) {
    return;
  }
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
    state_.reasoning_effort = effort;
    for (SessionSummary& session : state_.sessions) {
      if (session.id == session_id) {
        session.reasoning_effort = effort;
        break;
      }
    }
    PublishLocked();
  }
  if (session_id.isEmpty() || socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }
  socket_->SetConfig(session_id, QStringLiteral("reasoning"), effort);
  qInfo("reasoning profile=%s effort=%s", qUtf8Printable(name_),
        qUtf8Printable(effort));
}

void ProfileWorker::FetchSettings() {
  settings_requested_ = true;
  if (http_ == nullptr) {
    return;
  }

  QJsonDocument config_doc;
  QString error;
  // include_defaults=false keeps the record to the user's explicit overrides,
  // which is what the desktop writes back as a sparse patch.
  if (!http_->GetJson(QStringLiteral("/api/config?include_defaults=false"),
                      &config_doc, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.config_error = error;
    PublishLocked();
    return;
  }

  QJsonDocument schema_doc;
  if (!http_->GetJson(QStringLiteral("/api/config/schema"), &schema_doc,
                      &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.config = config_doc.object();
    state_.config_error = error;
    PublishLocked();
    return;
  }

  const QJsonObject schema_root = schema_doc.object();
  std::lock_guard<std::mutex> lock(mutex_);
  state_.config = config_doc.object();
  state_.config_schema = schema_root.value(QStringLiteral("fields")).toObject();
  state_.config_category_order =
      schema_root.value(QStringLiteral("category_order")).toArray();
  state_.config_loaded = true;
  state_.config_error.clear();
  PublishLocked();
  qInfo("settings profile=%s fields=%lld", qUtf8Printable(name_),
        static_cast<qint64>(state_.config_schema.size()));
}

void ProfileWorker::SaveConfig(const QJsonObject& patch) {
  if (http_ == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.config_saving = true;
    PublishLocked();
  }

  // PUT /api/config deep-merges this sparse patch. Echoing the whole cached
  // record would overwrite keys another surface changed since it loaded.
  QJsonDocument response;
  QString error;
  const bool ok =
      http_->PutJson(QStringLiteral("/api/config"),
                     QJsonObject{{"config", patch}}, &response, &error);
  if (!ok) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.config_saving = false;
    state_.config_error = error;
    PublishLocked();
    qInfo("config save failed: %s", qUtf8Printable(error));
    return;
  }

  QString changed_provider;
  QString changed_model;
  bool model_changed = false;
  const QJsonValue model_patch = patch.value(QStringLiteral("model"));
  if (model_patch.isObject()) {
    const QJsonObject model_object = model_patch.toObject();
    if (model_object.contains(QStringLiteral("default")) ||
        model_object.contains(QStringLiteral("provider"))) {
      model_changed = true;
      changed_provider =
          model_object.value(QStringLiteral("provider")).toString();
      changed_model = model_object.value(QStringLiteral("default")).toString();
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.config_saving = false;
    if (model_changed) {
      if (!changed_provider.isEmpty()) {
        profile_provider_ = changed_provider;
      } else {
        changed_provider = profile_provider_;
      }
      if (!changed_model.isEmpty()) {
        profile_model_ = changed_model;
      } else {
        changed_model = profile_model_;
      }
    }
    PublishLocked();
  }
  if (model_changed && !changed_provider.isEmpty() &&
      !changed_model.isEmpty()) {
    // Saving the profile default is an explicit answer to "which model should
    // this profile use", so move the open chat off any stale per-session
    // override. This is the path that prevents a later turn from restoring the
    // old "primary" and announcing that the configured model was a fallback.
    SetModel(changed_provider, changed_model);
  }
  FetchSettings();
}

namespace {

// The page -> REST source table. Keys are what the page widget reads back from
// PageState::data, so a route change stays in one place.
struct PageSource {
  const char* page;
  const char* key;
  const char* path;
  bool profile_scoped = false;
  int timeout_ms = 0;
};

constexpr PageSource kPageSources[] = {
    {"providers", "options", "/api/model/options"},
    {"providers", "endpoints", "/api/providers/custom-endpoints"},
    {"providers", "env", "/api/env"},
    {"modelaux", "auxiliary", "/api/model/auxiliary", true},
    {"modelmoa", "moa", "/api/model/moa", true},
    {"keys", "env", "/api/env"},
    {"vault", "pool", "/api/credentials/pool"},
    {"gateway", "access", "/api/webapp-access/status"},
    {"gateway", "status", "/api/status"},
    // Memory status is profile-scoped: the configured provider and the
    // built-in MEMORY.md/USER.md layers can differ per profile.
    {"memory", "status", "/api/memory", true},
    {"plugins", "plugins", "/api/dashboard/plugins", false, 60000},
    {"plugins", "hub", "/api/dashboard/plugins/hub", false, 60000},
    // Catalog scans every installed plugin and clones nothing, but it is slow
    // enough to keep off the page's critical path: it has its own lazy state,
    // fetched only when the user asks for it.
    {"plugincatalog", "catalog", "/api/dashboard/plugins/catalog", false,
     120000},
    {"billing", "portal", "/api/portal"},
    {"about", "status", "/api/status"},
    {"update", "update", "/api/hermes/update/check"},
    {"sessions", "stats", "/api/sessions/stats"},
    {"cron", "jobs", "/api/cron/jobs"},
    {"cron", "delivery", "/api/cron/delivery-targets"},
    {"cron", "blueprints", "/api/cron/blueprints"},
    {"localmodels", "status", "/api/local-models/status"},
    {"localmodels", "hardware", "/api/local-models/hardware"},
    {"localmodels", "catalog", "/api/local-models/catalog"},
    {"delegation", "routing", "/api/delegation/routing"},
    {"capabilities", "toolsets", "/api/tools/toolsets"},
    {"capabilities", "skills", "/api/skills"},
    {"capabilities", "mcp", "/api/mcp/servers"},
    {"messaging", "platforms", "/api/messaging/platforms"},
    {"profiles", "profiles", "/api/profiles"},
    {"webhooks", "webhooks", "/api/webhooks"},
    {"starmap", "graph", "/api/learning/graph"},
};

}  // namespace

void ProfileWorker::FetchPage(const QString& page) {
  if (shutting_down_ || http_ == nullptr || page.isEmpty()) {
    return;
  }
  if (page == QStringLiteral("artifacts")) {
    FetchArtifacts();
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[page];
    state.loading = true;
    state.error.clear();
    state.notice.clear();
    PublishLocked();
  }

  QJsonObject data;
  QString error;
  // Memory provider configuration is addressed per provider, so it cannot be
  // a static page-source row. The settings page asks for `memory:<name>` and
  // reads the provider's declared schema from PageState::data["config"].
  const QString memory_prefix = QStringLiteral("memory:");
  if (page.startsWith(memory_prefix)) {
    const QString provider = page.mid(memory_prefix.size()).trimmed();
    if (provider.isEmpty()) {
      error = QStringLiteral("memory provider name is required");
    } else {
      QString source_path =
          QStringLiteral("/api/memory/providers/%1/config?surface=declared")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(provider)));
      if (!name_.isEmpty()) {
        source_path +=
            QStringLiteral("&profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(name_)));
      }
      QJsonDocument document;
      QString source_error;
      if (http_->GetJson(source_path, &document, &source_error)) {
        data.insert(QStringLiteral("config"),
                    document.isObject() ? QJsonValue(document.object())
                                        : QJsonValue(QJsonObject()));
      } else {
        error = source_error;
      }
    }
  } else {
    for (const PageSource& source : kPageSources) {
      if (page != QLatin1String(source.page)) {
        continue;
      }
      QString source_path = QLatin1String(source.path);
      if ((source.profile_scoped || page == QLatin1String("webhooks") ||
           page == QLatin1String("starmap")) &&
          !name_.isEmpty()) {
        source_path +=
            QStringLiteral("?profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(name_)));
      }
      QJsonDocument document;
      QString source_error;
      if (!http_->GetJson(source_path, &document, &source_error, true,
                          source.timeout_ms)) {
        const QString labelled = QStringLiteral("%1: %2").arg(
            QLatin1String(source.key), source_error);
        if (error.isEmpty()) {
          error = labelled;
        } else {
          error += QStringLiteral("\n") + labelled;
        }
        continue;
      }
      data.insert(QLatin1String(source.key),
                  document.isObject() ? QJsonValue(document.object())
                                      : QJsonValue(document.array()));
    }
  }

  if (data.isEmpty() && error.isEmpty()) {
    error = QStringLiteral("no gateway routes registered for this page");
  }

  // Cron has no batch run-history route: fetch the last few runs of each job
  // (bounded, so a long job list cannot turn one page load into hundreds).
  if (page == QStringLiteral("cron")) {
    QJsonArray jobs = data.value(QStringLiteral("jobs")).toArray();
    if (!jobs.isEmpty()) {
      QJsonObject runs;
      int fetched = 0;
      for (const QJsonValue& value : jobs) {
        if (fetched++ >= 10) {
          break;
        }
        const QString job_id =
            value.toObject().value(QStringLiteral("id")).toString();
        if (job_id.isEmpty()) {
          continue;
        }
        QJsonDocument document;
        QString run_error;
        if (http_->GetJson(
                QStringLiteral("/api/cron/jobs/%1/runs?limit=5").arg(job_id),
                &document, &run_error)) {
          runs.insert(
              job_id,
              document.isObject()
                  ? QJsonValue(document.object().value(QStringLiteral("runs")))
                  : QJsonValue(document.array()));
        }
      }
      data.insert(QStringLiteral("runs"), runs);
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[page];
    state.loading = false;
    state.loaded = error.isEmpty();
    state.error = error;
    state.data = data;
    PublishLocked();
  }
  qInfo("settings page profile=%s page=%s sources=%lld error=%s",
        qUtf8Printable(name_), qUtf8Printable(page),
        static_cast<qint64>(data.size()), qUtf8Printable(error));
}

void ProfileWorker::FetchArtifacts() {
  if (shutting_down_ || http_ == nullptr) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[QStringLiteral("artifacts")];
    state.loading = true;
    state.error.clear();
    state.notice.clear();
    PublishLocked();
  }

  QString cwd;
  QString preview;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cwd = state_.artifacts_cwd;
    preview = state_.artifacts_preview;
  }
  if (cwd.isEmpty()) {
    // The browser is rooted at the active project, not the gateway process
    // cwd. `SetArtifactsRoot` supplies that path from the scoped project.
    cwd = artifacts_root_.isEmpty() ? home_ : artifacts_root_;
  }

  QJsonObject data;
  QString error;
  QJsonDocument listing;
  QString listing_error;
  if (http_->GetJson(
          QStringLiteral("/api/fs/list?path=%1")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(cwd))),
          &listing, &listing_error)) {
    data.insert(QStringLiteral("listing"), listing.object());
  } else if (error.isEmpty()) {
    error = listing_error;
  }

  QJsonDocument git_root;
  QString git_error;
  if (http_->GetJson(
          QStringLiteral("/api/fs/git-root?path=%1")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(cwd))),
          &git_root, &git_error)) {
    data.insert(QStringLiteral("git"), git_root.object());
  }

  if (!preview.isEmpty()) {
    QJsonDocument document;
    QString preview_error;
    if (http_->GetJson(
            QStringLiteral("/api/fs/read-text?path=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(preview))),
            &document, &preview_error)) {
      data.insert(QStringLiteral("preview"), document.object());
      const QString mime =
          document.object().value(QStringLiteral("mimeType")).toString();
      // Text previews cannot show pixels: fetch a data URL for image (and PDF,
      // which the page hands to the OS viewer) files as well.
      if (mime.startsWith(QStringLiteral("image/")) ||
          mime == QStringLiteral("application/pdf")) {
        QJsonDocument media;
        QString media_error;
        if (http_->GetJson(
                QStringLiteral("/api/fs/read-data-url?path=%1")
                    .arg(QString::fromLatin1(QUrl::toPercentEncoding(preview))),
                &media, &media_error)) {
          data.insert(QStringLiteral("previewMedia"), media.object());
        }
      }
    } else if (error.isEmpty()) {
      error = preview_error;
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.artifacts_cwd = cwd;
    state_.artifacts_preview = preview;
    artifacts_preview_text_ = data.value(QStringLiteral("preview"))
                                  .toObject()
                                  .value(QStringLiteral("text"))
                                  .toString();
    PageState& state = state_.pages[QStringLiteral("artifacts")];
    state.loading = false;
    state.loaded = error.isEmpty();
    state.error = error;
    state.data = data;
    PublishLocked();
  }
  qInfo("artifacts profile=%s cwd=%s preview=%s error=%s",
        qUtf8Printable(name_), qUtf8Printable(cwd), qUtf8Printable(preview),
        qUtf8Printable(error));
}

void ProfileWorker::SetArtifactsRoot(const QString& path) {
  const QString root = path.trimmed().isEmpty() ? home_ : path.trimmed();
  bool refresh = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (artifacts_root_ == root) {
      return;
    }
    artifacts_root_ = root;
    state_.artifacts_cwd = root;
    state_.artifacts_preview.clear();
    artifacts_preview_text_.clear();
    // If the page has been opened before, keep it consistent with the new
    // project instead of showing the previous project's listing.
    refresh = state_.pages.contains(QStringLiteral("artifacts"));
    PublishLocked();
  }
  if (refresh) {
    FetchArtifacts();
  }
}

void ProfileWorker::OpenArtifactsDir(const QString& path) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.artifacts_cwd = path;
    state_.artifacts_preview.clear();
  }
  FetchArtifacts();
}

void ProfileWorker::PreviewArtifact(const QString& path) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.artifacts_preview = path;
  }
  FetchArtifacts();
}

void ProfileWorker::SaveArtifact(const QString& path, const QString& text) {
  if (shutting_down_ || http_ == nullptr || path.isEmpty()) {
    return;
  }
  // Stale-write guard: the route overwrites unconditionally, so compare what
  // is on disk now with what the editor was seeded from before writing.
  QJsonDocument current;
  QString error;
  if (!http_->GetJson(
          QStringLiteral("/api/fs/read-text?path=%1")
              .arg(QString::fromLatin1(QUrl::toPercentEncoding(path))),
          &current, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[QStringLiteral("artifacts")];
    state.error = error;
    PublishLocked();
    return;
  }
  const QString on_disk =
      current.object().value(QStringLiteral("text")).toString();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (on_disk != artifacts_preview_text_) {
      PageState& state = state_.pages[QStringLiteral("artifacts")];
      state.error = QStringLiteral("%1 changed on disk — reload before saving.")
                        .arg(QFileInfo(path).fileName());
      PublishLocked();
      return;
    }
  }

  QJsonDocument saved;
  if (!http_->PostJson(QStringLiteral("/api/fs/write-text"),
                       QJsonObject{{QStringLiteral("path"), path},
                                   {QStringLiteral("content"), text}},
                       &saved, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[QStringLiteral("artifacts")];
    state.error = error;
    PublishLocked();
    qInfo("artifact save failed %s: %s", qUtf8Printable(path),
          qUtf8Printable(error));
    return;
  }
  artifacts_preview_text_ = text;
  FetchArtifacts();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.pages[QStringLiteral("artifacts")].notice =
        QStringLiteral("Saved %1").arg(QFileInfo(path).fileName());
    PublishLocked();
  }
  qInfo("artifact saved profile=%s path=%s", qUtf8Printable(name_),
        qUtf8Printable(path));
}

void ProfileWorker::PatchSession(const QString& session_id,
                                 const QJsonObject& patch) {
  if (shutting_down_ || http_ == nullptr || session_id.isEmpty()) {
    return;
  }
  QJsonObject body = patch;
  int revision = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const SessionSummary& session : state_.sessions) {
      if (session.id == session_id) {
        revision = session.revision;
        break;
      }
    }
  }
  if (!name_.isEmpty()) {
    body.insert(QStringLiteral("profile"), name_);
  }
  // Session mutations are fenced: the gateway refuses an unfenced write with
  // client_upgrade_required, so each patch carries its own request id and the
  // revision the sidebar last read for the row.
  body.insert(QStringLiteral("request_id"),
              QUuid::createUuid().toString(QUuid::WithoutBraces));
  body.insert(QStringLiteral("expected_revision"), revision);
  QJsonDocument document;
  QString error;
  const QString path =
      QStringLiteral("/api/sessions/%1")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(session_id)));
  if (!http_->RequestJson(QStringLiteral("PATCH"), path, body, &document,
                          &error) &&
      !error.isEmpty()) {
    PublishError(error);
    return;
  }
  // Reflect the flag locally first so the row reorders without waiting for the
  // round-trip, then re-read the list from the gateway (canonical order).
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (SessionSummary& session : state_.sessions) {
      if (session.id != session_id) {
        continue;
      }
      if (patch.contains(QStringLiteral("pinned"))) {
        session.pinned = patch.value(QStringLiteral("pinned")).toBool();
      }
      if (patch.contains(QStringLiteral("archived"))) {
        session.archived = patch.value(QStringLiteral("archived")).toBool();
      }
      if (patch.contains(QStringLiteral("title"))) {
        session.title = patch.value(QStringLiteral("title")).toString();
      }
      break;
    }
    PublishLocked();
  }
  FetchSessions();
}

void ProfileWorker::DeleteSession(const QString& session_id) {
  if (shutting_down_ || http_ == nullptr || session_id.isEmpty()) {
    return;
  }
  // Delete is fenced on THREE identities: request_id + expected_revision +
  // expected_generation. A delete without the generation is answered
  // 400 invalid_params, and a stale generation is a 409 admission_conflict.
  // Read them from the mutation snapshot first (what the desktop does).
  const QString escaped =
      QString::fromLatin1(QUrl::toPercentEncoding(session_id));
  QString profile_query;
  if (!name_.isEmpty()) {
    profile_query =
        QStringLiteral("?profile=%1")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(name_)));
  }
  QJsonDocument document;
  QString error;
  if (!http_->GetJson(QStringLiteral("/api/sessions/%1/mutation-snapshot%2")
                          .arg(escaped, profile_query),
                      &document, &error)) {
    if (!error.isEmpty()) {
      PublishError(error);
    }
    return;
  }
  const QJsonObject snapshot = document.object();
  const int revision =
      snapshot.value(QStringLiteral("runtime_revision")).toInt();
  const int generation =
      snapshot.value(QStringLiteral("runtime_generation")).toInt();
  if (socket_ != nullptr && socket_->IsConnected()) {
    const int id = socket_->SessionMutate(session_id, QStringLiteral("delete"),
                                          QJsonObject(), revision, generation);
    if (id >= 0) {
      PendingSessionMutation pending;
      pending.session_id = session_id;
      pending.operation = QStringLiteral("delete");
      pending.remove = true;
      pending_session_mutations_[id] = std::move(pending);
      qInfo("session delete rpc id=%s revision=%d generation=%d",
            qUtf8Printable(session_id), revision, generation);
      return;
    }
  }
  QString path =
      QStringLiteral(
          "/api/sessions/%1?request_id=%2&expected_revision=%3&"
          "expected_generation=%4")
          .arg(escaped, QUuid::createUuid().toString(QUuid::WithoutBraces))
          .arg(revision)
          .arg(generation);
  if (!name_.isEmpty()) {
    path += QStringLiteral("&profile=%1")
                .arg(QString::fromLatin1(QUrl::toPercentEncoding(name_)));
  }
  if (!http_->DeleteJson(path, QJsonObject(), &document, &error)) {
    if (!error.isEmpty()) {
      PublishError(error);
    }
    qWarning("session delete failed id=%s revision=%d generation=%d error=%s",
             qUtf8Printable(session_id), revision, generation,
             qUtf8Printable(error));
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = state_.sessions.begin(); it != state_.sessions.end(); ++it) {
      if (it->id == session_id) {
        state_.sessions.erase(it);
        break;
      }
    }
    PublishLocked();
  }
  qInfo("session deleted id=%s revision=%d generation=%d",
        qUtf8Printable(session_id), revision, generation);
  FetchSessions();
}

void ProfileWorker::ExecuteSlash(const QString& command) {
  if (shutting_down_ || socket_ == nullptr || command.trimmed().isEmpty()) {
    return;
  }
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
  }
  const int id = socket_->ExecuteSlashCommand(session_id, command.trimmed());
  if (id < 0) {
    PublishError(QStringLiteral("gateway is not connected"));
    return;
  }
  pending_commands_[id] = command.trimmed();
  qInfo("slash profile=%s command=%s", qUtf8Printable(name_),
        qUtf8Printable(command.trimmed()));
}

void ProfileWorker::Note(const QString& text) {
  if (shutting_down_ || text.trimmed().isEmpty()) {
    return;
  }
  MessageBlock block;
  block.kind = BlockKind::kParagraph;
  block.text = text;
  ChatMessage message;
  message.role = MessageRole::kSystem;
  message.author = QStringLiteral("hermes");
  message.timestamp = QDateTime::currentDateTime();
  message.blocks.push_back(std::move(block));
  std::lock_guard<std::mutex> lock(mutex_);
  state_.messages.push_back(std::move(message));
  PublishLocked();
}

void ProfileWorker::AppendUserPromptLocked(const QString& text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return;
  }
  // The prompt belongs below the assistant text that is still streaming. Close
  // that message now so later deltas open a fresh assistant bubble instead of
  // continuing above the just-inserted user row.
  if (!state_.messages.empty()) {
    ChatMessage& last = state_.messages.back();
    if (last.role == MessageRole::kAssistant &&
        (last.streaming || last.reasoning_streaming)) {
      last.streaming = false;
      last.reasoning_streaming = false;
    }
  }
  ChatMessage message;
  message.role = MessageRole::kUser;
  message.author = QStringLiteral("You");
  message.timestamp = QDateTime::currentDateTime();
  message.blocks = ParseMarkdown(trimmed);
  state_.messages.push_back(std::move(message));
  state_.running = true;
  state_.running_session_id = state_.selected_session_id;
  state_.error_text.clear();
  state_.status_text = QStringLiteral("working");
  foreground_ = true;
  state_.unread = 0;
  NoteActivityLocked();
}

void ProfileWorker::RecordInjectedPrompt(const QString& text,
                                         const QString& admission_id) {
  if (shutting_down_ || text.trimmed().isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (!admission_id.isEmpty() &&
      !pending_prompts_.ClaimAnnouncement(admission_id)) {
    return;
  }
  AppendUserPromptLocked(text);
  PublishLocked();
}

void ProfileWorker::ForgetPendingAdmission(const QString& admission_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  pending_prompts_.Forget(admission_id);
}

void ProfileWorker::Steer(const QString& text) { SteerWithCallback(text, {}); }

void ProfileWorker::SteerWithCallback(const QString& text, SteerCallback done) {
  auto finish = [&done](bool ok, const QString& message) {
    if (done) {
      done(ok, message);
    }
  };
  if (shutting_down_ || socket_ == nullptr) {
    finish(false, QStringLiteral("gateway is not connected"));
    return;
  }
  if (text.trimmed().isEmpty()) {
    finish(false, QStringLiteral("the steer text is empty"));
    return;
  }
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
  }
  if (execution_generation_ < 0) {
    // Nothing has claimed this session since we attached, so there is no turn
    // to steer: send the words as the next prompt instead of erroring (the
    // desktop queues a steer typed at an idle chat the same way).
    Note(
        QStringLiteral("/steer: no turn is running, so the text was sent as "
                       "a new prompt"));
    SendPrompt(text);
    finish(true, QStringLiteral("sent as a new prompt"));
    return;
  }
  const int id =
      socket_->SteerSession(session_id, text.trimmed(), execution_generation_);
  if (id < 0) {
    PublishError(QStringLiteral("gateway is not connected"));
    finish(false, QStringLiteral("gateway is not connected"));
    return;
  }
  pending_steers_[id] = text.trimmed();
  if (done) {
    pending_steer_callbacks_[id] = std::move(done);
  }
}

void ProfileWorker::FinishHubRequest(quint64 token, const QString& route) {
  if (http_ == nullptr) {
    emit HubRequestFinished(token, QJsonObject(),
                            QStringLiteral("Gateway HTTP is not ready"));
    return;
  }
  QJsonDocument document;
  QString error;
  if (!http_->GetJson(route, &document, &error)) {
    emit HubRequestFinished(token, QJsonObject(), error);
    return;
  }
  emit HubRequestFinished(token, document.object(), QString());
}

void ProfileWorker::FinishActionRequest(quint64 token, const QString& method,
                                        const QString& route,
                                        const QJsonObject& body) {
  if (http_ == nullptr) {
    emit HubRequestFinished(token, QJsonObject(),
                            QStringLiteral("Gateway HTTP is not ready"));
    return;
  }
  QJsonDocument document;
  QString error;
  if (!http_->RequestJson(method, route, body, &document, &error)) {
    emit HubRequestFinished(token, QJsonObject(), error);
    return;
  }
  emit HubRequestFinished(token, document.object(), QString());
}

void ProfileWorker::FetchComputerUseStatus(quint64 token) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishActionRequest(
      token, QStringLiteral("GET"),
      QStringLiteral("/api/tools/computer-use/status?profile=%1").arg(profile),
      QJsonObject());
}

void ProfileWorker::GrantComputerUsePermissions(quint64 token) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishActionRequest(
      token, QStringLiteral("POST"),
      QStringLiteral("/api/tools/computer-use/permissions/grant?profile=%1")
          .arg(profile),
      QJsonObject());
}

void ProfileWorker::FetchActionStatus(quint64 token, const QString& name) {
  FinishActionRequest(
      token, QStringLiteral("GET"),
      QStringLiteral("/api/actions/%1/status?lines=200")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name))),
      QJsonObject());
}

void ProfileWorker::SearchSessions(quint64 token, const QString& query,
                                   int limit) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishHubRequest(
      token, QStringLiteral("/api/sessions/search?q=%1&limit=%2&profile=%3")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(query)),
                      QString::number(limit), profile));
}

void ProfileWorker::FetchTerminalBackends(quint64 token) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishActionRequest(
      token, QStringLiteral("GET"),
      QStringLiteral("/api/tools/terminal/backends?profile=%1").arg(profile),
      QJsonObject());
}

void ProfileWorker::SelectTerminalBackend(quint64 token,
                                          const QString& backend) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishActionRequest(
      token, QStringLiteral("PUT"),
      QStringLiteral("/api/tools/terminal/backend?profile=%1").arg(profile),
      QJsonObject{{QStringLiteral("backend"), backend}});
}

void ProfileWorker::FetchToolsetModels(quint64 token, const QString& name,
                                       const QString& provider) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  QString route =
      QStringLiteral("/api/tools/toolsets/%1/models?profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)), profile);
  if (!provider.isEmpty()) {
    route += QStringLiteral("&provider=%1")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(provider)));
  }
  FinishActionRequest(token, QStringLiteral("GET"), route, QJsonObject());
}

void ProfileWorker::SelectToolsetModel(quint64 token, const QString& name,
                                       const QString& model,
                                       const QString& provider) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishActionRequest(
      token, QStringLiteral("PUT"),
      QStringLiteral("/api/tools/toolsets/%1/model?profile=%2")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(name)), profile),
      QJsonObject{{QStringLiteral("model"), model},
                  {QStringLiteral("provider"), provider}});
}

void ProfileWorker::FetchSkillHubSources(quint64 token) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishHubRequest(
      token, QStringLiteral("/api/skills/hub/sources?profile=%1").arg(profile));
}

void ProfileWorker::SearchSkillHub(quint64 token, const QString& query,
                                   const QString& source, int limit) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  const QString route =
      QStringLiteral(
          "/api/skills/hub/search?q=%1&source=%2&limit=%3&profile=%4")
          .arg(QString::fromLatin1(QUrl::toPercentEncoding(query)),
               QString::fromLatin1(QUrl::toPercentEncoding(source)),
               QString::number(limit), profile);
  FinishHubRequest(token, route);
}

void ProfileWorker::PreviewSkillHub(quint64 token, const QString& identifier) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishHubRequest(
      token, QStringLiteral("/api/skills/hub/preview?identifier=%1&profile=%2")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(identifier)),
                      profile));
}

void ProfileWorker::ScanSkillHub(quint64 token, const QString& identifier) {
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  FinishHubRequest(
      token, QStringLiteral("/api/skills/hub/scan?identifier=%1&profile=%2")
                 .arg(QString::fromLatin1(QUrl::toPercentEncoding(identifier)),
                      profile));
}

void ProfileWorker::SendRpc(quint64 token, const QString& method,
                            const QJsonObject& params) {
  if (shutting_down_ || socket_ == nullptr || method.isEmpty()) {
    emit RpcFinished(token, QJsonObject(),
                     QStringLiteral("Gateway is not connected"));
    return;
  }
  const int id = socket_->RequestRpc(method, params);
  pending_rpc_[id] = token;
}

void ProfileWorker::SpeakText(quint64 token, const QString& text) {
  if (http_ == nullptr) {
    emit SpeechFinished(token, QJsonObject(),
                        QStringLiteral("Gateway HTTP is not ready"));
    return;
  }
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  const QString route =
      QStringLiteral("/api/audio/speak?profile=%1").arg(profile);
  QJsonDocument document;
  QString error;
  if (!http_->PostJson(route, QJsonObject{{QStringLiteral("text"), text}},
                       &document, &error)) {
    emit SpeechFinished(token, QJsonObject(), error);
    return;
  }
  emit SpeechFinished(token, document.object(), QString());
}

void ProfileWorker::TranscribeAudio(quint64 token, const QString& path) {
  if (http_ == nullptr) {
    emit TranscriptionFinished(token, QJsonObject(),
                               QStringLiteral("Gateway HTTP is not ready"));
    return;
  }
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    emit TranscriptionFinished(token, QJsonObject(),
                               QStringLiteral("Could not read the recording."));
    return;
  }
  constexpr qint64 kMaxUploadBytes = 24 * 1024 * 1024;
  if (file.size() <= 0 || file.size() > kMaxUploadBytes) {
    emit TranscriptionFinished(
        token, QJsonObject(),
        QStringLiteral("Pick a recording smaller than 24 MB."));
    return;
  }
  const QString mime = AudioMimeForFilePath(path);
  if (mime.isEmpty()) {
    emit TranscriptionFinished(
        token, QJsonObject(),
        QStringLiteral("Unsupported recording type. Use WAV, MP3, M4A, OGG, "
                       "FLAC or WebM."));
    return;
  }
  const QString data_url = EncodeAudioDataUrl(file.readAll(), mime);
  if (data_url.isEmpty()) {
    emit TranscriptionFinished(token, QJsonObject(),
                               QStringLiteral("The recording is empty."));
    return;
  }
  const QString profile = QString::fromLatin1(QUrl::toPercentEncoding(name_));
  QJsonDocument document;
  QString error;
  if (!http_->PostJson(
          QStringLiteral("/api/audio/transcribe?profile=%1").arg(profile),
          QJsonObject{{QStringLiteral("data_url"), data_url},
                      {QStringLiteral("mime_type"), mime}},
          &document, &error)) {
    emit TranscriptionFinished(token, QJsonObject(), error);
    return;
  }
  emit TranscriptionFinished(token, document.object(), QString());
}

void ProfileWorker::FetchSlashCatalog() {
  if (shutting_down_ || socket_ == nullptr) {
    return;
  }
  QString session_id;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id = state_.selected_session_id;
  }
  pending_catalog_id_ = socket_->FetchCommandCatalog(session_id);
}

void ProfileWorker::AttachFile(const QString& local_path) {
  if (shutting_down_ || http_ == nullptr || local_path.isEmpty()) {
    return;
  }
  // The gateway's /api/chat/file-upload route is mounted only on the Webapp
  // surface (404 on the native API), and this app runs as the same user on the
  // same host, so stage the file the way the route does: a unique 0600 copy
  // under the profile's uploads/ directory.
  QDir uploads(home_ + QStringLiteral("/uploads"));
  if (!uploads.exists() && !uploads.mkpath(QStringLiteral("."))) {
    PublishError(QStringLiteral("cannot create %1").arg(uploads.path()));
    return;
  }
  const QString staged_name =
      QStringLiteral("native-%1-%2")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(12),
               QFileInfo(local_path).fileName());
  const QString uploaded = uploads.filePath(staged_name);
  if (!QFile::copy(local_path, uploaded)) {
    PublishError(QStringLiteral("cannot copy %1").arg(local_path));
    return;
  }
  QFile::setPermissions(uploaded,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.attachments.append(uploaded);
    ++state_.attachment_serial;
    PublishLocked();
  }
  qInfo("attached profile=%s path=%s", qUtf8Printable(name_),
        qUtf8Printable(uploaded));
}

void ProfileWorker::PageAction(const QString& page, const QString& method,
                               const QString& path, const QJsonObject& body) {
  if (shutting_down_ || http_ == nullptr || path.isEmpty()) {
    return;
  }
  // Session mutations need the worker's fenced paths (request id, expected
  // revision and, for delete, expected generation). A raw PATCH/DELETE from a
  // settings page would otherwise be refused with client_upgrade_required.
  if (page == QStringLiteral("sessions") &&
      (method == QStringLiteral("PATCH") ||
       method == QStringLiteral("DELETE"))) {
    const QString prefix = QStringLiteral("/api/sessions/");
    if (path.startsWith(prefix)) {
      QString session_id = path.mid(prefix.size());
      const int query = session_id.indexOf(QLatin1Char('?'));
      if (query >= 0) {
        session_id = session_id.left(query);
      }
      if (!session_id.isEmpty()) {
        if (method == QStringLiteral("PATCH")) {
          PatchSession(session_id, body);
        } else {
          DeleteSession(session_id);
        }
        FetchPage(page);
        return;
      }
    }
  }
  QJsonDocument document;
  QString error;
  if (!http_->RequestJson(method, path, body, &document, &error)) {
    std::lock_guard<std::mutex> lock(mutex_);
    PageState& state = state_.pages[page];
    state.loading = false;
    state.error = error;
    PublishLocked();
    qInfo("settings action failed profile=%s %s %s: %s", qUtf8Printable(name_),
          qUtf8Printable(method), qUtf8Printable(path), qUtf8Printable(error));
    return;
  }

  qInfo("settings action result profile=%s %s %s -> %s", qUtf8Printable(name_),
        qUtf8Printable(method), qUtf8Printable(path),
        qUtf8Printable(QString::fromUtf8(
            QJsonDocument(document.object()).toJson(QJsonDocument::Compact))));
  // Re-read the page so an action is reflected from the backend's own state
  // rather than an optimistic local edit.
  if (page.startsWith(QStringLiteral("memory:"))) {
    // Saving a provider field also changes the memory status snapshot (secret
    // badges, readiness), so refresh both the provider and its parent page.
    FetchPage(page);
    FetchPage(QStringLiteral("memory"));
  } else {
    FetchPage(page);
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // A created webhook's secret is returned exactly once, so surface the
    // server's response instead of a generic "applied" notice.
    const QJsonObject result = document.object();
    const QString secret = result.value(QStringLiteral("secret")).toString();
    const QString message = result.value(QStringLiteral("message")).toString();
    const QString action_id =
        result.value(QStringLiteral("action_id")).toString();
    QString notice;
    if (!secret.isEmpty()) {
      notice = QStringLiteral("Secret (copy now): %1").arg(secret);
    } else if (result.value(QStringLiteral("ok")).isBool() &&
               !result.value(QStringLiteral("ok")).toBool()) {
      notice = message.isEmpty()
                   ? result.value(QStringLiteral("error")).toString()
                   : message;
    } else if (!message.isEmpty()) {
      notice = message;
    } else if (result.value(QStringLiteral("already_running")).toBool()) {
      notice = QStringLiteral("Action is already running.");
    } else if (!action_id.isEmpty()) {
      notice = QStringLiteral("%1 started (action %2).").arg(method, action_id);
    } else {
      notice = QStringLiteral("%1 applied").arg(method);
    }
    state_.pages[page].notice = notice;
    PublishLocked();
  }
  qInfo("settings action profile=%s %s %s", qUtf8Printable(name_),
        qUtf8Printable(method), qUtf8Printable(path));
}

void ProfileWorker::FetchMessages(const QString& session_id) {
  if (shutting_down_ || http_ == nullptr || session_id.isEmpty()) {
    return;
  }
  QJsonDocument document;
  QString error;
  if (!http_->GetJson(
          QStringLiteral("/api/sessions/%1/messages?limit=%2&order=latest")
              .arg(session_id)
              .arg(kMessageFetchLimit),
          &document, &error)) {
    PublishError(error);
    return;
  }

  const QJsonArray rows =
      document.object().value(QStringLiteral("messages")).toArray();
  std::vector<ChatMessage> messages;
  messages.reserve(static_cast<std::size_t>(rows.size()));

  for (const QJsonValue& value : rows) {
    const QJsonObject row = value.toObject();
    const QString role = row.value(QStringLiteral("role")).toString();
    const QString content = row.value(QStringLiteral("content")).toString();
    const QString reasoning = ReasoningTextFromRow(row);
    const QDateTime timestamp = MessageTime(row);
    const qint64 row_id =
        static_cast<qint64>(row.value(QStringLiteral("id")).toDouble());
    const std::vector<ChatReaction> reactions =
        ParseChatReactions(DisplayReactions(row));

    if (role == QStringLiteral("user")) {
      ChatMessage message;
      message.row_id = row_id;
      message.role = MessageRole::kUser;
      message.author = QStringLiteral("You");
      message.timestamp = timestamp;
      message.raw_text = content;
      message.blocks = ParseMarkdown(content);
      message.reactions = reactions;
      messages.push_back(std::move(message));
      continue;
    }

    if (role == QStringLiteral("assistant")) {
      // A reasoning-only assistant row has no content, but it is still a
      // durable transcript row. Dropping it here made a completed turn lose
      // its thinking section on the refresh 700 ms after completion.
      if (!content.trimmed().isEmpty() || !reasoning.trimmed().isEmpty()) {
        ChatMessage message;
        message.row_id = row_id;
        message.role = MessageRole::kAssistant;
        message.author = QStringLiteral("Hermes");
        message.timestamp = timestamp;
        message.reasoning = reasoning;
        message.raw_text = content;
        message.blocks = ParseMarkdown(content);
        message.reactions = reactions;
        messages.push_back(std::move(message));
      }
      // Tool calls live on the assistant row, but the transcript renders the
      // matching `tool` result row below. Rendering both duplicated every
      // call as an arguments row followed by its result.
      continue;
    }

    if (role == QStringLiteral("tool")) {
      MessageBlock block;
      block.kind = BlockKind::kTool;
      block.text = row.value(QStringLiteral("tool_name"))
                       .toString(QStringLiteral("tool"));
      block.detail = content.trimmed().isEmpty() ? QStringLiteral("completed")
                                                 : ToolOutput(content);
      block.id = row.value(QStringLiteral("tool_call_id")).toString();
      // History repeats one tool row per call; fold consecutive calls into a
      // single message so the transcript can show one collapsed action group.
      const bool append_to_last =
          !messages.empty() &&
          messages.back().role == MessageRole::kAssistant &&
          !messages.back().blocks.empty() &&
          std::all_of(messages.back().blocks.begin(),
                      messages.back().blocks.end(),
                      [](const MessageBlock& existing) {
                        return existing.kind == BlockKind::kTool;
                      });
      if (append_to_last) {
        messages.back().blocks.push_back(std::move(block));
        continue;
      }
      ChatMessage message;
      message.row_id = row_id;
      message.id = block.id;
      message.role = MessageRole::kAssistant;
      message.author = QStringLiteral("Hermes");
      message.timestamp = timestamp;
      message.blocks.push_back(std::move(block));
      messages.push_back(std::move(message));
    }
  }

  if (messages.size() > kRenderMessageLimit) {
    std::size_t start = messages.size() - kRenderMessageLimit;
    const std::size_t lookback = start > 40 ? start - 40 : 0;
    for (std::size_t index = start; index > lookback; --index) {
      if (messages[index - 1].role == MessageRole::kUser) {
        start = index - 1;
        break;
      }
    }
    messages.erase(messages.begin(),
                   messages.begin() + static_cast<std::ptrdiff_t>(start));
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    // REST history is authoritative for completed rows, but its write can
    // land just after `message.complete`. Keep the live transcript for a
    // recent reasoning-only turn the backend has not projected yet instead of
    // blinking the thinking section out (desktop #118755). Once the row lands,
    // the normal hydration path takes over; reasoning omitted from that row is
    // carried across below.
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime newest_hydrated =
        messages.empty() ? QDateTime() : messages.back().timestamp;
    bool keep_live = false;
    for (auto it = state_.messages.rbegin(); it != state_.messages.rend();
         ++it) {
      if (it->role != MessageRole::kAssistant ||
          it->reasoning.trimmed().isEmpty()) {
        continue;
      }
      if (!it->timestamp.isValid() || it->timestamp.secsTo(now) > 10 * 60 ||
          HistoryContainsAssistant(messages, *it)) {
        continue;
      }
      // If REST already carries rows well past this live assistant, the
      // stream is stale (a dropped subscription or a replaced turn). Prefer
      // the page so reconciliation can repair the transcript instead of
      // keeping the frozen live tail.
      if (newest_hydrated.isValid() &&
          it->timestamp.secsTo(newest_hydrated) > 30) {
        continue;
      }
      keep_live = true;
      break;
    }
    PreserveLiveReasoning(&messages, state_.messages);
    // A REST page can be older than the prompt that just went out (the
    // `message.complete` refresh races the commit). Keep the optimistic user
    // row so an injected prompt never disappears after it was accepted.
    PreservePendingUserMessages(&messages, state_.messages);
    if (!keep_live) {
      state_.messages = std::move(messages);
    }
    state_.error_text.clear();
    state_.status_text = QStringLiteral("ready");
    history_loaded_ = true;
    PublishLocked();
  }
}

void ProfileWorker::OnSocketConnected(const QString& subprotocol) {
  Q_UNUSED(subprotocol);
  if (shutting_down_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.connected = true;
    state_.gateway_ready = false;
    state_.status_text = QStringLiteral("ready");
    PublishLocked();
  }
  if (pending_new_session_) {
    pending_new_session_ = false;
    const QString cwd = pending_new_session_cwd_;
    const QString project_id = pending_new_session_project_id_;
    pending_new_session_cwd_.clear();
    pending_new_session_project_id_.clear();
    NewSessionInDir(cwd, project_id);
    return;
  }
  AttachIfForeground();
}

void ProfileWorker::OnSocketDisconnected() {
  if (shutting_down_) {
    return;
  }
  std::vector<SteerCallback> prompt_callbacks;
  for (auto& entry : pending_prompt_callbacks_) {
    if (entry.second) {
      prompt_callbacks.push_back(std::move(entry.second));
    }
  }
  pending_prompt_callbacks_.clear();
  if (deferred_prompt_callback_) {
    prompt_callbacks.push_back(std::move(deferred_prompt_callback_));
  }
  if (pending_attached_prompt_ != nullptr &&
      pending_attached_prompt_->done) {
    prompt_callbacks.push_back(std::move(pending_attached_prompt_->done));
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.connected = false;
    state_.gateway_ready = false;
    state_.status_text = QStringLiteral("reconnecting");
    attached_session_id_.clear();
    runtime_session_id_.clear();
    pending_attached_prompt_.reset();
    pending_session_mutations_.clear();
    pending_prompt_request_id_ = -1;
    pending_receipt_id_ = -1;
    state_.requests.clear();
    PublishLocked();
  }
  for (auto& done : prompt_callbacks) {
    done(false, QStringLiteral(
                   "Gateway disconnected before the prompt was acknowledged"));
  }
}

void ProfileWorker::OnReplayGap(const QString& session_id) {
  if (shutting_down_) {
    return;
  }
  QString selected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_.selected_session_id.isEmpty()) {
      return;
    }
    if (!session_id.isEmpty() && session_id != state_.selected_session_id &&
        session_id != runtime_session_id_) {
      return;
    }
    history_loaded_ = false;
    selected = state_.selected_session_id;
  }
  FetchMessages(selected);
}

void ProfileWorker::AcceptConnectionRequest(const QString& session_id,
                                            const QJsonObject& payload) {
  if (shutting_down_) {
    return;
  }
  ConnectionOperation operation = ParseConnectionOperation(payload);
  if (operation.op_id.isEmpty() ||
      (operation.targets.empty() && operation.suggested.empty())) {
    return;
  }
  PendingRequest request;
  request.id = operation.op_id;
  request.method = QStringLiteral("connection");
  request.via_rpc = true;
  request.session_id =
      session_id.isEmpty()
          ? payload.value(QStringLiteral("session_id")).toString()
          : session_id;
  request.title = operation.targets.empty()
                      ? QStringLiteral("Connector directory")
                      : QStringLiteral("Connection needed");
  if (operation.targets.empty()) {
    request.detail = QStringLiteral("Suggested connectors: %1")
                         .arg(operation.suggested.join(QStringLiteral(", ")));
  } else {
    request.detail =
        QStringLiteral("%1 target%2 · backend-owned state")
            .arg(operation.targets.size())
            .arg(operation.targets.size() == 1 ? QString()
                                               : QStringLiteral("s"));
  }
  request.revision = operation.seq;
  request.connection = std::move(operation);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (PendingRequest& existing : state_.requests) {
      if (existing.id == request.id) {
        // A reconnect can re-deliver the request; keep the card but take the
        // newer authoritative snapshot.
        existing.connection = std::move(request.connection);
        ++existing.revision;
        PublishLocked();
        return;
      }
    }
    state_.requests.push_back(std::move(request));
    PublishLocked();
  }
  qInfo("connection request profile=%s op=%s", qUtf8Printable(name_),
        qUtf8Printable(payload.value(QStringLiteral("op_id")).toString()));
}

void ProfileWorker::RememberAccountOperation(const QJsonObject& result) {
  if (shutting_down_) {
    return;
  }
  ConnectionOperation operation = ParseConnectionOperation(result);
  if (operation.op_id.isEmpty()) {
    return;
  }
  operation.owner_type = QStringLiteral("account");
  std::lock_guard<std::mutex> lock(mutex_);
  StoreAccountOperationLocked(std::move(operation));
  PublishLocked();
}

void ProfileWorker::StoreAccountOperationLocked(ConnectionOperation operation) {
  const auto existing = std::find_if(
      state_.account_operations.begin(), state_.account_operations.end(),
      [&operation](const ConnectionOperation& candidate) {
        return candidate.op_id == operation.op_id;
      });
  if (existing != state_.account_operations.end()) {
    // Broadcasts reject equal sequence numbers; replies only reject older
    // ones. Treat equal as an authoritative refresh so both paths converge.
    if (operation.seq > 0 && existing->seq > 0 &&
        operation.seq < existing->seq) {
      return;
    }
    for (ConnectionTarget& target : operation.targets) {
      const auto old =
          std::find_if(existing->targets.begin(), existing->targets.end(),
                       [&target](const ConnectionTarget& candidate) {
                         return candidate.name == target.name;
                       });
      if (old != existing->targets.end()) {
        MergeConnectionTargetMetadata(&target, *old);
      }
    }
    *existing = std::move(operation);
  } else {
    state_.account_operations.push_back(std::move(operation));
  }
  constexpr std::size_t kMaxAccountOperations = 12;
  while (state_.account_operations.size() > kMaxAccountOperations) {
    state_.account_operations.erase(state_.account_operations.begin());
  }
  ++state_.revision;
}

void ProfileWorker::ApplyConnectionUpdate(const QString& session_id,
                                          const QJsonObject& payload) {
  ConnectionOperation operation = ParseConnectionOperation(payload);
  if (operation.op_id.isEmpty()) {
    return;
  }
  if (operation.owner_type == QStringLiteral("account")) {
    std::lock_guard<std::mutex> lock(mutex_);
    StoreAccountOperationLocked(std::move(operation));
    PublishLocked();
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto it = state_.requests.begin(); it != state_.requests.end(); ++it) {
    if (it->method != QStringLiteral("connection") ||
        it->id != operation.op_id) {
      continue;
    }
    // `seq` is the operation's monotonic write counter. A frame that is not
    // higher than the one the card holds cannot move a row, even when it
    // arrives after a local optimistic state change.
    if (operation.seq > 0 && it->connection.seq > 0 &&
        operation.seq <= it->connection.seq) {
      return;
    }
    if (operation.tool_call_id.isEmpty()) {
      operation.tool_call_id = it->connection.tool_call_id;
    }
    if (operation.suggested.empty()) {
      operation.suggested = it->connection.suggested;
    }
    for (ConnectionTarget& target : operation.targets) {
      const auto old = std::find_if(
          it->connection.targets.begin(), it->connection.targets.end(),
          [&target](const ConnectionTarget& candidate) {
            return candidate.name == target.name;
          });
      if (old != it->connection.targets.end()) {
        MergeConnectionTargetMetadata(&target, *old);
      }
    }
    const bool settled = operation.settled;
    if (settled) {
      state_.requests.erase(it);
    } else {
      it->connection = std::move(operation);
      it->revision = it->connection.seq;
      it->detail =
          QStringLiteral("%1 target%2 · backend-owned state")
              .arg(it->connection.targets.size())
              .arg(it->connection.targets.size() == 1 ? QString()
                                                      : QStringLiteral("s"));
    }
    PublishLocked();
    return;
  }
  (void)session_id;
}

void ProfileWorker::AcceptCanonicalPrompt(const QString& kind,
                                          const QString& session_id,
                                          const QJsonObject& payload) {
  if (shutting_down_ || socket_ == nullptr) {
    return;
  }
  // Gateway API sessions publish these as events (the desktop's canonical
  // path), not as JSON-RPC request frames. `prompt_id` is the correlation id.
  const QString request_id =
      payload.value(QStringLiteral("prompt_id"))
          .toString(payload.value(QStringLiteral("request_id")).toString());
  if (request_id.isEmpty()) {
    return;
  }
  PendingRequest request;
  request.id = request_id;
  request.method = kind;
  request.via_rpc = true;
  request.execution_generation =
      payload.value(QStringLiteral("execution_generation")).toInt(-1);
  request.session_id =
      session_id.isEmpty()
          ? payload.value(QStringLiteral("session_id")).toString()
          : session_id;

  if (kind == QStringLiteral("approval")) {
    request.title = QStringLiteral("Approval required");
    const QString command = payload.value(QStringLiteral("command")).toString();
    const QString description =
        payload.value(QStringLiteral("description")).toString();
    request.detail = description.isEmpty()
                         ? command
                         : command + QStringLiteral("  -  ") + description;
    const QJsonArray choices =
        payload.value(QStringLiteral("choices")).toArray();
    for (const QJsonValue& choice : choices) {
      request.choices.push_back(choice.toString());
    }
    if (request.choices.isEmpty()) {
      request.choices = {QStringLiteral("once"), QStringLiteral("deny")};
    }
  } else {
    request.title = QStringLiteral("Clarification needed");
    const QJsonArray questions =
        payload.value(QStringLiteral("questions")).toArray();
    for (const QJsonValue& value : questions) {
      const QJsonObject row = value.toObject();
      ClarifyQuestion question;
      question.qid = row.value(QStringLiteral("qid")).toString();
      question.question = row.value(QStringLiteral("question")).toString();
      question.multi_select =
          row.value(QStringLiteral("multi_select")).toBool();
      const QJsonArray choices = row.value(QStringLiteral("choices")).toArray();
      for (const QJsonValue& choice : choices) {
        if (!choice.toString().trimmed().isEmpty()) {
          question.choices.push_back(choice.toString());
        }
      }
      if (question.qid.isEmpty()) {
        question.qid = request_id;
      }
      if (!question.question.isEmpty()) {
        request.questions.push_back(std::move(question));
      }
    }
    if (request.questions.empty()) {
      // A single-question canonical event uses the top-level fields.
      ClarifyQuestion question;
      question.qid = payload.value(QStringLiteral("qid")).toString();
      if (question.qid.isEmpty()) {
        question.qid = request_id;
      }
      question.question = payload.value(QStringLiteral("question")).toString();
      question.multi_select =
          payload.value(QStringLiteral("multi_select")).toBool();
      const QJsonArray choices =
          payload.value(QStringLiteral("choices")).toArray();
      for (const QJsonValue& choice : choices) {
        if (!choice.toString().trimmed().isEmpty()) {
          question.choices.push_back(choice.toString());
        }
      }
      if (question.question.isEmpty()) {
        question.question = QStringLiteral("The agent asked a question.");
      }
      request.questions.push_back(std::move(question));
    }
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const PendingRequest& existing : state_.requests) {
      if (existing.id == request_id) {
        return;
      }
    }
    state_.requests.push_back(std::move(request));
    PublishLocked();
  }
  qInfo("prompt profile=%s kind=%s id=%s", qUtf8Printable(name_),
        qUtf8Printable(kind), qUtf8Printable(request_id));
}

void ProfileWorker::AcceptServerRequest(const QJsonValue& id,
                                        const QString& method,
                                        const QJsonObject& params) {
  if (shutting_down_ || socket_ == nullptr) {
    return;
  }
  const QString request_id = id.toString();
  if (request_id.isEmpty()) {
    return;
  }

  PendingRequest request;
  request.id = request_id;
  request.method = method;
  request.session_id = params.value(QStringLiteral("session_id")).toString();

  if (method == QStringLiteral("approval")) {
    request.title = QStringLiteral("Approval required");
    const QString command = params.value(QStringLiteral("command")).toString();
    const QString description =
        params.value(QStringLiteral("description")).toString();
    request.detail = description.isEmpty()
                         ? command
                         : command + QStringLiteral("  -  ") + description;
    const QJsonArray choices =
        params.value(QStringLiteral("choices")).toArray();
    for (const QJsonValue& choice : choices) {
      request.choices.push_back(choice.toString());
    }
    if (request.choices.isEmpty()) {
      request.choices = {QStringLiteral("once"), QStringLiteral("deny")};
    }
  } else if (method == QStringLiteral("clarify")) {
    request.title = QStringLiteral("Clarification needed");
    const QJsonArray questions =
        params.value(QStringLiteral("questions")).toArray();
    for (const QJsonValue& value : questions) {
      const QJsonObject row = value.toObject();
      ClarifyQuestion question;
      question.qid = row.value(QStringLiteral("qid")).toString();
      question.question = row.value(QStringLiteral("question")).toString();
      question.multi_select =
          row.value(QStringLiteral("multi_select")).toBool();
      const QJsonArray choices = row.value(QStringLiteral("choices")).toArray();
      for (const QJsonValue& choice : choices) {
        if (!choice.toString().trimmed().isEmpty()) {
          question.choices.push_back(choice.toString());
        }
      }
      if (!question.qid.isEmpty() && !question.question.isEmpty()) {
        request.questions.push_back(std::move(question));
      }
    }
    if (request.questions.empty()) {
      // A malformed/empty batch has nothing to show; settling it now keeps
      // the agent from blocking for the clarify deadline.
      socket_->Respond(QJsonValue(request_id), QJsonObject());
      return;
    }
  } else if (method == QStringLiteral("sudo")) {
    request.title = QStringLiteral("sudo password");
    request.detail = params.value(QStringLiteral("command")).toString();
    request.placeholder = QStringLiteral("Password");
    request.secret = true;
  } else if (method == QStringLiteral("secret")) {
    request.title = params.value(QStringLiteral("prompt"))
                        .toString(QStringLiteral("Secret required"));
    request.detail = params.value(QStringLiteral("env_var")).toString();
    request.placeholder = QStringLiteral("Value");
    request.secret = true;
  } else if (method == QStringLiteral("vault.unlock_prompt")) {
    request.title = QStringLiteral("Unlock password manager");
    request.detail = params.value(QStringLiteral("display_name")).toString();
    request.placeholder = QStringLiteral("Master password");
    request.secret = true;
  } else if (method == QStringLiteral("vault.code")) {
    request.title = QStringLiteral("Verification code");
    const QString site = params.value(QStringLiteral("site")).toString();
    const QString hint = params.value(QStringLiteral("hint")).toString();
    request.detail =
        site.isEmpty() ? hint : site + QStringLiteral(" - ") + hint;
    request.placeholder = QStringLiteral("Code");
    request.secret = true;
  } else {
    // Preview/terminal/window/tour need a surface this build does not have;
    // setup/theme pickers are onboarding-only. Settle with NOT_SHOWN instead
    // of leaving the agent blocked for its full deadline.
    qInfo("profile %s unsupported server request method=%s id=%s",
          qUtf8Printable(name_), qUtf8Printable(method),
          qUtf8Printable(request_id));
    socket_->RespondError(
        id, 4404,
        QStringLiteral("This Hermes UI build does not implement %1")
            .arg(method));
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const PendingRequest& existing : state_.requests) {
      if (existing.id == request_id) {
        return;
      }
    }
    state_.requests.push_back(std::move(request));
    PublishLocked();
  }
  qInfo("request profile=%s method=%s id=%s", qUtf8Printable(name_),
        qUtf8Printable(method), qUtf8Printable(request_id));
}

void ProfileWorker::OnSocketEvent(const QString& type,
                                  const QString& session_id,
                                  const QJsonObject& payload) {
  if (shutting_down_) {
    return;
  }
  const QJsonValue generation =
      payload.value(QStringLiteral("execution_generation"));
  if (generation.isDouble()) {
    execution_generation_ = generation.toInt();
  }
  if (qEnvironmentVariableIsSet("HERMES_UI_DEBUG_GEN")) {
    qInfo("event type=%s generation=%d has=%d", qUtf8Printable(type),
          execution_generation_, generation.isDouble() ? 1 : 0);
  }
  if (type == QStringLiteral("voice.status")) {
    const QString status = payload.value(QStringLiteral("state")).toString();
    std::lock_guard<std::mutex> lock(mutex_);
    state_.voice_status = status;
    if (status == QStringLiteral("idle")) {
      state_.voice_recording = false;
      state_.voice_partial.clear();
    } else {
      state_.voice_recording = status == QStringLiteral("listening") ||
                               status == QStringLiteral("recording");
    }
    PublishLocked();
    return;
  }
  if (type == QStringLiteral("voice.partial")) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.voice_partial = payload.value(QStringLiteral("text")).toString();
    state_.voice_recording = true;
    state_.voice_status = QStringLiteral("recording");
    PublishLocked();
    return;
  }
  if (type == QStringLiteral("voice.transcript")) {
    const QString text = payload.value(QStringLiteral("text")).toString();
    std::lock_guard<std::mutex> lock(mutex_);
    state_.voice_recording = false;
    state_.voice_partial.clear();
    if (payload.value(QStringLiteral("no_speech_limit")).toBool()) {
      state_.voice_status = QStringLiteral("No speech detected");
    } else if (payload.value(QStringLiteral("stop_phrase")).toBool()) {
      state_.voice_status = QStringLiteral("Voice chat ended");
    } else if (!text.trimmed().isEmpty()) {
      state_.voice_status.clear();
      state_.voice_transcript = text;
      ++state_.voice_transcript_serial;
    }
    PublishLocked();
    return;
  }
  if (type == QStringLiteral("approval.request") ||
      type == QStringLiteral("clarify.request")) {
    AcceptCanonicalPrompt(type.startsWith(QStringLiteral("approval"))
                              ? QStringLiteral("approval")
                              : QStringLiteral("clarify"),
                          session_id, payload);
    return;
  }
  if (type == QStringLiteral("connection.request")) {
    AcceptConnectionRequest(session_id, payload);
    return;
  }
  if (type == QStringLiteral("connection.update")) {
    ApplyConnectionUpdate(session_id, payload);
    return;
  }
  if (type == QStringLiteral("request.cancel") ||
      type == QStringLiteral("approval.settled") ||
      type == QStringLiteral("clarify.settled")) {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString request_id =
        payload.value(QStringLiteral("id"))
            .toString(payload.value(QStringLiteral("prompt_id")).toString());
    if (request_id.isEmpty()) {
      state_.requests.clear();
    } else {
      for (auto it = state_.requests.begin(); it != state_.requests.end();
           ++it) {
        if (it->id == request_id) {
          state_.requests.erase(it);
          break;
        }
      }
    }
    PublishLocked();
    return;
  }
  if (type.startsWith(QStringLiteral("subagent."))) {
    std::lock_guard<std::mutex> lock(mutex_);
    ApplySubagentEvent(&state_.subagents, type, session_id, payload,
                       QDateTime::currentMSecsSinceEpoch());
    PublishLocked();
    return;
  }
  if (type == QStringLiteral("todo.updated")) {
    std::vector<TodoItem> todos;
    int revision = 0;
    if (!ParseTodoState(payload, &todos, &revision)) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (revision < state_.todo_revision) {
      return;
    }
    state_.todos = std::move(todos);
    state_.todo_revision = revision;
    PublishLocked();
    return;
  }
  if (type == QStringLiteral("gateway.ready")) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.gateway_ready = true;
      state_.connected = true;
      state_.status_text = QStringLiteral("ready");
      PublishLocked();
    }
    if (pending_new_session_) {
      pending_new_session_ = false;
      const QString cwd = pending_new_session_cwd_;
      const QString project_id = pending_new_session_project_id_;
      pending_new_session_cwd_.clear();
      pending_new_session_project_id_.clear();
      NewSessionInDir(cwd, project_id);
      return;
    }
    AttachIfForeground();
    return;
  }

  if (type == QStringLiteral("notification.show")) {
    // Agent notices are account-wide and often carry no session id, so they
    // must be consumed before the session filter below.
    std::lock_guard<std::mutex> lock(mutex_);
    ++state_.gateway_notice_serial;
    UpsertGatewayNotice(&state_.gateway_notices, payload,
                        state_.gateway_notice_serial);
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("notification.clear")) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ClearGatewayNotice(&state_.gateway_notices,
                           GatewayNoticeKey(payload))) {
      ++state_.gateway_notice_serial;
      PublishLocked();
    }
    return;
  }

  QString selected;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    selected = state_.selected_session_id;
  }
  if (!session_id.isEmpty() && !selected.isEmpty() && session_id != selected &&
      session_id != runtime_session_id_) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    last_event_ms_ = QDateTime::currentMSecsSinceEpoch();
  }

  if (type == QStringLiteral("session.control.update")) {
    const QString summary = SessionControlSummary(
        payload.value(QStringLiteral("control")).toObject());
    std::lock_guard<std::mutex> lock(mutex_);
    state_.session_control_summary = summary;
    ++state_.session_control_serial;
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("moa.reference")) {
    AppendMoaReasoning(MoaReferenceText(payload),
                       payload.value(QStringLiteral("index")).toInt() <= 1);
    return;
  }

  if (type == QStringLiteral("moa.progress")) {
    const QString text = MoaProgressText(payload);
    if (!text.isEmpty()) {
      // Only the start of the fan-out replaces the previous block; later
      // counters must append or they would erase the references already shown.
      AppendMoaReasoning(
          text, payload.value(QStringLiteral("refs_done")).toInt() <= 0);
    }
    return;
  }

  if (type == QStringLiteral("moa.aggregating")) {
    AppendMoaReasoning(QStringLiteral("◇ MoA aggregating…\n"), false);
    return;
  }

  if (type == QStringLiteral("moa.phase")) {
    const QString text = MoaPhaseText(payload);
    if (!text.isEmpty()) {
      AppendMoaReasoning(text, false);
    }
    return;
  }

  if (type == QStringLiteral("reaction")) {
    // Only the affection reaction has a native surface; other kinds are
    // silent acknowledgements, matching the desktop.
    if (payload.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("vibe")) {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.reaction_kind = QStringLiteral("vibe");
      ++state_.reaction_serial;
      PublishLocked();
    }
    return;
  }

  if (type == QStringLiteral("message.reaction")) {
    const qint64 row_id =
        payload.value(QStringLiteral("row_id")).toVariant().toLongLong();
    const QString role = payload.value(QStringLiteral("role")).toString();
    const QJsonArray reactions =
        payload.value(QStringLiteral("reactions")).toArray();
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage* target = nullptr;
    for (auto it = state_.messages.rbegin(); it != state_.messages.rend();
         ++it) {
      if (row_id > 0 && it->row_id == row_id) {
        target = &(*it);
        break;
      }
      if (row_id <= 0 && target == nullptr) {
        if ((role == QStringLiteral("user") &&
             it->role == MessageRole::kUser) ||
            (role == QStringLiteral("assistant") &&
             it->role == MessageRole::kAssistant)) {
          target = &(*it);
        }
      }
    }
    if (target != nullptr) {
      target->reactions = ParseChatReactions(reactions);
      PublishLocked();
    }
    return;
  }

  if (type == QStringLiteral("review.summary")) {
    const QString text = ReviewSummaryText(payload);
    if (!text.isEmpty()) {
      Note(QStringLiteral("Review: %1").arg(text));
    }
    return;
  }

  if (type == QStringLiteral("agent.print")) {
    const QString text = AgentPrintText(payload);
    if (!text.isEmpty()) {
      Note(text);
    }
    return;
  }

  if (type == QStringLiteral("message.start")) {
    std::lock_guard<std::mutex> lock(mutex_);
    // A queue drain can start the turn a beat before the pending snapshot
    // lands. Flush the row the snapshot dropped so the user sees the prompt
    // above this assistant message.
    if (std::optional<PendingAdmission> processed =
            pending_prompts_.TakeNextAwaiting(
                QDateTime::currentMSecsSinceEpoch())) {
      AppendUserPromptLocked(processed->text);
    }
    if (!state_.messages.empty()) {
      state_.messages.back().streaming = false;
      state_.messages.back().reasoning_streaming = false;
    }
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("Hermes");
    message.timestamp = QDateTime::currentDateTime();
    message.streaming = true;
    MessageBlock paragraph;
    paragraph.kind = BlockKind::kParagraph;
    message.blocks.push_back(std::move(paragraph));
    state_.messages.push_back(std::move(message));
    NoteActivityLocked();
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("message.delta")) {
    AppendAssistantDelta(payload.value(QStringLiteral("text")).toString());
    return;
  }

  if (type == QStringLiteral("reasoning.delta") ||
      type == QStringLiteral("thinking.delta")) {
    AppendReasoningDelta(payload.value(QStringLiteral("text")).toString(),
                         true);
    return;
  }

  if (type == QStringLiteral("reasoning.available")) {
    // The gateway emits the finished reasoning once more when a run produced
    // it without streaming (providers that never send deltas). Treat it as a
    // final, already-complete body instead of appending it twice.
    AppendReasoningDelta(payload.value(QStringLiteral("text")).toString(),
                         false);
    return;
  }

  if (type == QStringLiteral("message.interim")) {
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("Hermes");
    message.timestamp = QDateTime::currentDateTime();
    message.blocks =
        ParseMarkdown(payload.value(QStringLiteral("text")).toString());
    state_.messages.push_back(std::move(message));
    NoteActivityLocked();
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("message.complete")) {
    const QString text = payload.value(QStringLiteral("text")).toString();
    const QString error = payload.value(QStringLiteral("error")).toString();
    const bool failed = payload.value(QStringLiteral("status")).toString() ==
                        QStringLiteral("error");
    CompleteAssistant(failed && !error.isEmpty() ? error : text, failed);
    return;
  }

  if (type == QStringLiteral("tool.generating")) {
    const QString text = ToolGeneratingText(payload);
    std::lock_guard<std::mutex> lock(mutex_);
    state_.status_text = text;
    NoteActivityLocked();
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("tool.progress")) {
    const QString id =
        payload.value(QStringLiteral("tool_id"))
            .toString(payload.value(QStringLiteral("id")).toString());
    const QString text = ToolProgressText(payload);
    if (text.isEmpty()) {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    UpdateToolRow(id, text);
    state_.status_text = text;
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("tool.start")) {
    const QString id =
        payload.value(QStringLiteral("id"))
            .toString(payload.value(QStringLiteral("tool_call_id")).toString());
    const QString name =
        payload.value(QStringLiteral("name")).toString(QStringLiteral("tool"));
    QString detail =
        ArgumentsPreview(payload.value(QStringLiteral("arguments")));
    if (detail.isEmpty()) {
      detail = ArgumentsPreview(payload.value(QStringLiteral("input")));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    AppendToolRow(id, name, detail);
    NoteActivityLocked();
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("tool.complete")) {
    const QString id =
        payload.value(QStringLiteral("id"))
            .toString(payload.value(QStringLiteral("tool_call_id")).toString());
    const QString name =
        payload.value(QStringLiteral("name")).toString(QStringLiteral("tool"));
    const QString error = payload.value(QStringLiteral("error")).toString();
    const QJsonValue result = payload.contains(QStringLiteral("result"))
                                  ? payload.value(QStringLiteral("result"))
                                  : payload.value(QStringLiteral("output"));
    const QString result_text = ToolValueText(result);
    const QString detail = !error.isEmpty() ? ToolOutput(error)
                                            : (result_text.trimmed().isEmpty()
                                                   ? QStringLiteral("completed")
                                                   : ToolOutput(result_text));
    std::lock_guard<std::mutex> lock(mutex_);
    UpdateToolRow(id, detail);
    if (id.isEmpty()) {
      AppendToolRow(QString(), name, detail);
    }
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("session.usage")) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.usage = MergeSessionUsage(state_.usage, payload);
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("session.info")) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool streaming = !state_.messages.empty() &&
                           (state_.messages.back().streaming ||
                            state_.messages.back().reasoning_streaming);
    const bool running = payload.value(QStringLiteral("running")).toBool();
    // A late `running:false` from the previous turn must not hide the spinner
    // while this turn is still streaming text/reasoning.
    if (running || !streaming) {
      state_.running = running;
      state_.running_session_id =
          state_.running ? state_.selected_session_id : QString();
    }
    const QString model = payload.value(QStringLiteral("model")).toString();
    if (!model.isEmpty()) {
      state_.model = model;
    }
    const QString provider =
        payload.value(QStringLiteral("provider")).toString();
    if (!provider.isEmpty()) {
      state_.provider = provider;
    }
    const QString effort =
        payload.value(QStringLiteral("reasoning_effort")).toString();
    if (!effort.isEmpty()) {
      state_.reasoning_effort = effort;
    }
    const QString stored =
        payload.value(QStringLiteral("stored_session_id"))
            .toString(payload.value(QStringLiteral("session_id")).toString());
    const QString runtime_session =
        stored.isEmpty() ? state_.selected_session_id : stored;
    for (SessionSummary& session : state_.sessions) {
      if (session.id != runtime_session) {
        continue;
      }
      if (!model.isEmpty()) {
        session.model = model;
      }
      if (!provider.isEmpty()) {
        session.provider = provider;
      }
      if (!effort.isEmpty()) {
        session.reasoning_effort = effort;
      }
      break;
    }
    if (!session_id.isEmpty() && !stored.isEmpty() &&
        stored == state_.selected_session_id) {
      // Compression can hand the same conversation a fresh runtime id; keep
      // accepting its events for the stored session the sidebar names.
      runtime_session_id_ = session_id;
    }
    if (state_.selected_session_id.isEmpty() && !stored.isEmpty()) {
      state_.selected_session_id = stored;
    }
    ApplyPendingSnapshotLocked(payload);
    const QString title = payload.value(QStringLiteral("title")).toString();
    if (!title.isEmpty()) {
      for (SessionSummary& session : state_.sessions) {
        if (session.id == state_.selected_session_id) {
          session.title = title;
          break;
        }
      }
    }
    PublishLocked();
    return;
  }

  if (type == QStringLiteral("status.update")) {
    std::lock_guard<std::mutex> lock(mutex_);
    const QString text = payload.value(QStringLiteral("text")).toString();
    if (!text.isEmpty()) {
      state_.status_text = text;
      PublishLocked();
    }
    return;
  }

  if (type == QStringLiteral("error")) {
    PublishError(payload.value(QStringLiteral("message"))
                     .toString(QStringLiteral("Hermes reported an error")));
  }
}

void ProfileWorker::OnSocketResponse(int id, const QJsonObject& result,
                                     const QString& error) {
  if (shutting_down_) {
    return;
  }
  if (const auto callback = pending_prompt_callbacks_.find(id);
      callback != pending_prompt_callbacks_.end()) {
    SteerCallback done = std::move(callback->second);
    pending_prompt_callbacks_.erase(callback);
    done(error.isEmpty(), error);
  }
  if (pending_attached_prompt_ != nullptr) {
    auto attachment = pending_attached_prompt_->request_ids.find(id);
    if (attachment != pending_attached_prompt_->request_ids.end()) {
      pending_attached_prompt_->request_ids.erase(attachment);
      if (!error.isEmpty()) {
        // The prompt text already carries the staged paths, and the gateway
        // can still read them, so a failed inline attach is a downgrade rather
        // than a failed send. Do not surface it as an attachment error.
        qWarning("profile %s: inline attachment downgraded: %s",
                 qUtf8Printable(name_), qUtf8Printable(error));
      }
      if (pending_attached_prompt_->request_ids.empty()) {
        auto pending = std::move(pending_attached_prompt_);
        SubmitPromptFrame(pending->session_id, pending->text,
                          pending->truncate_before_row_id,
                          pending->confirm_empty_truncate,
                          pending->confirm_deep_truncate, false,
                          std::move(pending->done));
      }
      return;
    }
  }
  if (const auto mutation = pending_session_mutations_.find(id);
      mutation != pending_session_mutations_.end()) {
    const PendingSessionMutation pending = mutation->second;
    pending_session_mutations_.erase(mutation);
    if (!error.isEmpty()) {
      PublishError(error);
      return;
    }
    if (pending.remove) {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = state_.sessions.begin(); it != state_.sessions.end();
             ++it) {
          if (it->id == pending.session_id) {
            state_.sessions.erase(it);
            break;
          }
        }
        PublishLocked();
      }
      FetchSessions();
    }
    return;
  }
  if (id == pending_prompt_request_id_) {
    pending_prompt_request_id_ = -1;
    if (!error.isEmpty()) {
      pending_prompt_queued_ = false;
      PublishError(error);
      return;
    }
    // prompt.submit returns an admission receipt. Keep it so a reconnect can
    // ask whether the turn started, finished, or was lost.
    admission_id_ = result.value(QStringLiteral("admission_id")).toString();
    admission_status_ = result.value(QStringLiteral("status")).toString();
    // A normal send already appended its user row optimistically. Mark its
    // admission as surfaced so the queued -> started transition does not add
    // a second bubble. Queued sends have no user row yet, so they stay
    // announceable and appear exactly when the gateway starts them.
    if (!pending_prompt_queued_ && !admission_id_.isEmpty()) {
      pending_prompts_.ClaimAnnouncement(admission_id_);
    }
    pending_prompt_queued_ = false;
    // A cancel-then-send (Inject) can interrupt the old generation and start a
    // queued one on a freshly re-bound subscription. Re-attach after every
    // submit so the stream follows the new turn and replays anything the old
    // subscription missed.
    attached_session_id_.clear();
    AttachIfForeground();
    return;
  }
  if (id == pending_receipt_id_) {
    pending_receipt_id_ = -1;
    if (!error.isEmpty()) {
      admission_id_.clear();
      admission_status_.clear();
      return;
    }
    const QString status = result.value(QStringLiteral("status")).toString();
    admission_status_ = status;
    if (status == QStringLiteral("terminal")) {
      admission_id_.clear();
    } else if (status == QStringLiteral("unknown")) {
      const QString admission = admission_id_;
      admission_id_.clear();
      Note(QStringLiteral("Turn admission %1 was not found; refreshed "
                          "history")
               .arg(admission.left(8)));
      if (message_refresh_timer_ != nullptr) {
        message_refresh_timer_->start(0);
      }
    }
    return;
  }
  if (const auto rpc = pending_rpc_.find(id); rpc != pending_rpc_.end()) {
    const quint64 token = rpc->second;
    pending_rpc_.erase(rpc);
    emit RpcFinished(token, result, error);
    return;
  }
  if (id == pending_catalog_id_) {
    pending_catalog_id_ = -1;
    if (!error.isEmpty()) {
      return;
    }
    std::vector<SlashEntry> entries;
    std::map<QString, QString> canon;
    const QJsonObject canon_object =
        result.value(QStringLiteral("canon")).toObject();
    for (auto it = canon_object.constBegin(); it != canon_object.constEnd();
         ++it) {
      canon[it.key().toLower()] = it.value().toString();
    }
    const QJsonArray categories =
        result.value(QStringLiteral("categories")).toArray();
    // One leading slash, always: skill names arrive already slashed, so
    // prepending unconditionally produced "//some-skill" in the popup.
    auto normalize = [](const QString& name) {
      QString trimmed = name;
      while (trimmed.startsWith(QLatin1Char('/'))) {
        trimmed.remove(0, 1);
      }
      return QStringLiteral("/") + trimmed;
    };
    auto add_pair = [&entries, &canon, &normalize](const QString& name,
                                                   const QString& description,
                                                   const QString& category) {
      SlashEntry entry;
      entry.name = normalize(name);
      entry.description = description;
      entry.category = category;
      for (const auto& alias : canon) {
        if (normalize(alias.second).compare(entry.name, Qt::CaseInsensitive) ==
            0) {
          entry.aliases.append(normalize(alias.first));
        }
      }
      entries.push_back(std::move(entry));
    };
    for (const QJsonValue& value : categories) {
      const QJsonObject category = value.toObject();
      const QString label = category.value(QStringLiteral("name")).toString();
      for (const QJsonValue& row :
           category.value(QStringLiteral("pairs")).toArray()) {
        const QJsonArray pair = row.toArray();
        if (pair.size() >= 2) {
          add_pair(pair.at(0).toString(), pair.at(1).toString(), label);
        }
      }
    }
    // Skills register as slash commands too (they show in the desktop
    // palette and the gateway resolves them), so list them under Skills.
    const QJsonObject skills =
        result.value(QStringLiteral("skills")).toObject();
    for (auto it = skills.constBegin(); it != skills.constEnd(); ++it) {
      const QJsonObject meta = it.value().toObject();
      QString description =
          meta.value(QStringLiteral("description")).toString();
      if (description.isEmpty()) {
        description = QStringLiteral("Skill");
      }
      const QString skill_name = normalize(it.key());
      const bool already_listed = std::any_of(
          entries.begin(), entries.end(),
          [&skill_name](const SlashEntry& entry) {
            return entry.name.compare(skill_name, Qt::CaseInsensitive) == 0;
          });
      if (!already_listed) {
        entries.push_back(
            {skill_name, description, QStringLiteral("Skills"), {}});
      }
    }
    const int command_count = static_cast<int>(entries.size());
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.slash_commands = std::move(entries);
      ++state_.slash_catalog_serial;
      PublishLocked();
    }
    if (qEnvironmentVariableIsSet("HERMES_UI_DEBUG_SLASH")) {
      int skills = 0;
      for (const SlashEntry& entry : state_.slash_commands) {
        if (entry.name.contains(QStringLiteral("//"))) {
          qInfo("double-slash: %s", qUtf8Printable(entry.name));
        }
        if (entry.category == QStringLiteral("Skills")) {
          ++skills;
        }
      }
      qInfo("slash skills=%d total=%d", skills,
            static_cast<int>(state_.slash_commands.size()));
    }
    qInfo("slash catalog profile=%s commands=%d", qUtf8Printable(name_),
          command_count);
    return;
  }
  const auto steer_it = pending_steers_.find(id);
  if (steer_it != pending_steers_.end()) {
    const QString text = steer_it->second;
    pending_steers_.erase(steer_it);
    SteerCallback callback;
    if (const auto cb = pending_steer_callbacks_.find(id);
        cb != pending_steer_callbacks_.end()) {
      callback = std::move(cb->second);
      pending_steer_callbacks_.erase(cb);
    }
    auto finish = [&callback](bool ok, const QString& message) {
      if (callback) {
        callback(ok, message);
      }
    };
    const QString status = result.value(QStringLiteral("status")).toString();
    const bool delivered =
        result.value(QStringLiteral("delivered")).toBool(true);
    const bool queued = status == QStringLiteral("queued");
    if (!error.isEmpty()) {
      // Never replay failed steer input as a fresh prompt: the gateway may
      // have delivered it despite the error, and a failure is not consent to
      // start a new turn.
      if (error.contains(QStringLiteral("stale_generation"))) {
        execution_generation_ = -1;
      }
      Note(QStringLiteral("steer failed: %1").arg(error));
      qInfo("steer result profile=%s status=%s error=%s", qUtf8Printable(name_),
            qUtf8Printable(status), qUtf8Printable(error));
      finish(false, error);
      return;
    }
    if (!queued) {
      // The claim was already gone (a turn that finished between our snapshot
      // and the request): keep the words, deliver them as the next prompt.
      execution_generation_ = -1;
      Note(
          QStringLiteral("Steer: the turn had already finished, so the text "
                         "was sent as a new prompt"));
      SendPrompt(text);
      qInfo("steer fallback profile=%s status=%s delivered=%d",
            qUtf8Printable(name_), qUtf8Printable(status), delivered ? 1 : 0);
      finish(true, QStringLiteral("sent as a new prompt"));
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      AppendUserPromptLocked(text);
      PublishLocked();
    }
    Note(
        QStringLiteral("Steer queued — the model reads it after the current "
                       "tool call"));
    qInfo("steer result profile=%s status=%s error=%s", qUtf8Printable(name_),
          qUtf8Printable(status), qUtf8Printable(error));
    qInfo("steer profile=%s text=%s", qUtf8Printable(name_),
          qUtf8Printable(text));
    finish(true, QString());
    return;
  }
  const auto command_it = pending_commands_.find(id);
  if (command_it != pending_commands_.end()) {
    const QString command = command_it->second;
    pending_commands_.erase(command_it);
    if (!error.isEmpty()) {
      const QString friendly =
          error.contains(QStringLiteral("unsupported_command"))
              ? QStringLiteral("%1 is not available for this session")
                    .arg(command)
              : QStringLiteral("%1 failed: %2").arg(command, error);
      Note(friendly);
      PublishError(friendly);
      return;
    }
    QString output = result.value(QStringLiteral("output")).toString();
    if (output.trimmed().isEmpty()) {
      output = result.value(QStringLiteral("text")).toString();
    }
    if (output.trimmed().isEmpty()) {
      output = QStringLiteral("%1: no output").arg(command);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("command");
    message.timestamp = QDateTime::currentDateTime();
    MessageBlock block;
    block.kind = BlockKind::kCode;
    block.language = QStringLiteral("text");
    block.text = output.trimmed();
    message.blocks.push_back(std::move(block));
    state_.messages.push_back(std::move(message));
    NoteActivityLocked();
    PublishLocked();
    qInfo("slash output profile=%s command=%s bytes=%d", qUtf8Printable(name_),
          qUtf8Printable(command), static_cast<int>(output.size()));
    return;
  }
  if (id == pending_create_id_) {
    pending_create_id_ = -1;
    const QString project_id =
        std::exchange(pending_create_project_id_, QString());
    if (!error.isEmpty()) {
      pending_prompt_.clear();
      auto deferred = std::move(deferred_prompt_callback_);
      if (deferred) {
        deferred(false, error);
      }
      PublishError(error);
      return;
    }
    QString session_id = result.value(QStringLiteral("session_id")).toString();
    if (session_id.isEmpty()) {
      session_id = result.value(QStringLiteral("stored_session_id")).toString();
    }
    if (session_id.isEmpty()) {
      pending_prompt_.clear();
      auto deferred = std::move(deferred_prompt_callback_);
      if (deferred) {
        deferred(false, QStringLiteral("Gateway created no session id"));
      }
      PublishError(QStringLiteral("Gateway created no session id"));
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      state_.selected_session_id = session_id;
      // A model picked in the fresh-chat composer is pinned against an empty
      // session id. Adopt the new session here so the first /api/sessions
      // refresh cannot snap the composer back to the default model.
      if (pinned_session_id_.isEmpty() && !pinned_model_.isEmpty()) {
        pinned_session_id_ = session_id;
      }
      state_.messages.clear();
      state_.todos.clear();
      state_.todo_revision = 0;
      state_.session_control_summary.clear();
      ++state_.session_control_serial;
      state_.pending_admissions.clear();
      state_.pending_authority_epoch = 0;
      state_.pending_revision = -1;
      pending_prompts_.Reset();
      pending_prompts_baseline_ready_ = false;
      ApplyPendingSnapshotLocked(result);
      admission_id_.clear();
      admission_status_.clear();
      pending_receipt_id_ = -1;
      state_.running = false;
      state_.running_session_id.clear();
      state_.error_text.clear();
      state_.status_text = QStringLiteral("ready");
      history_loaded_ = true;
      attached_session_id_ = session_id;
      runtime_session_id_.clear();
      SessionSummary placeholder;
      placeholder.id = session_id;
      placeholder.title = QStringLiteral("New chat");
      placeholder.source = QStringLiteral("local");
      placeholder.project_id = project_id;
      placeholder.model = state_.model;
      placeholder.started_at_secs = QDateTime::currentSecsSinceEpoch();
      placeholder.last_active_secs = placeholder.started_at_secs;
      state_.sessions.insert(state_.sessions.begin(), std::move(placeholder));
      ++state_.today_count;
      PublishLocked();
    }
    qInfo("new session %.12s", qUtf8Printable(session_id));
    const QString prompt = std::exchange(pending_prompt_, QString());
    auto deferred = std::move(deferred_prompt_callback_);
    if (!prompt.isEmpty()) {
      if (deferred) {
        SendPromptWithCallback(prompt, std::move(deferred));
      } else {
        SendPrompt(prompt);
      }
    } else if (deferred) {
      deferred(false, QStringLiteral("The pending prompt was cleared"));
    }
    return;
  }

  if (!error.isEmpty()) {
    PublishError(error);
    return;
  }

  // A resume answer carries the still-open requests; re-deliver them so a card
  // survives a dropped socket instead of vanishing until the agent's deadline.
  const QJsonArray open_requests =
      result.value(QStringLiteral("open_requests")).toArray();
  for (const QJsonValue& value : open_requests) {
    const QJsonObject request = value.toObject();
    AcceptServerRequest(request.value(QStringLiteral("id")),
                        request.value(QStringLiteral("method")).toString(),
                        request.value(QStringLiteral("params")).toObject());
  }
  const QJsonObject pending_approval =
      result.value(QStringLiteral("pending_approval")).toObject();
  if (!pending_approval.isEmpty()) {
    AcceptCanonicalPrompt(
        QStringLiteral("approval"),
        pending_approval.value(QStringLiteral("session_id")).toString(),
        pending_approval);
  }
  const QJsonObject pending_clarify =
      result.value(QStringLiteral("pending_clarify")).toObject();
  if (!pending_clarify.isEmpty()) {
    AcceptCanonicalPrompt(
        QStringLiteral("clarify"),
        pending_clarify.value(QStringLiteral("session_id")).toString(),
        pending_clarify);
  }
  const QJsonObject pending_connection =
      result.value(QStringLiteral("pending_connection")).toObject();
  if (!pending_connection.isEmpty()) {
    // The resume snapshot carries the operation but not an envelope, so bind
    // it to the stored session the answers must name as the owner.
    QString connection_session =
        pending_connection.value(QStringLiteral("session_id")).toString();
    if (connection_session.isEmpty()) {
      // The owner is the live UI sid, not the durable stored key.
      connection_session =
          result.value(QStringLiteral("session_id")).toString();
    }
    if (connection_session.isEmpty()) {
      connection_session =
          result.value(QStringLiteral("stored_session_id")).toString();
    }
    if (!connection_session.isEmpty()) {
      AcceptConnectionRequest(connection_session, pending_connection);
    }
  }

  // A resume answer carries the live state; fold the useful bits into the
  // snapshot without replacing the REST-loaded transcript.
  const QJsonObject info = result.value(QStringLiteral("info")).toObject();
  const QString runtime = result.value(QStringLiteral("session_id")).toString();
  const QString stored =
      result.value(QStringLiteral("stored_session_id")).toString();
  QString receipt_session;
  QString receipt_admission;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    // Replay frames are keyed by the runtime id. Adopt it before the replay
    // answer arrives, otherwise the session filter drops every replayed
    // message for a resumed conversation.
    if (!runtime.isEmpty() && !stored.isEmpty() &&
        stored == state_.selected_session_id) {
      runtime_session_id_ = runtime;
    }
    if (state_.selected_session_id.isEmpty() && !stored.isEmpty()) {
      state_.selected_session_id = stored;
    }
    ApplyPendingSnapshotLocked(result);
    if (!admission_id_.isEmpty() && socket_ != nullptr) {
      receipt_session = state_.selected_session_id;
      if (receipt_session.isEmpty()) {
        receipt_session = stored;
      }
      receipt_admission = admission_id_;
    }
    if (!info.isEmpty() && info.contains(QStringLiteral("running"))) {
      state_.running = info.value(QStringLiteral("running")).toBool();
      state_.running_session_id =
          state_.running ? state_.selected_session_id : QString();
    }
    if (!info.isEmpty()) {
      PublishLocked();
    }
  }
  if (!receipt_session.isEmpty() && !receipt_admission.isEmpty() &&
      socket_ != nullptr) {
    pending_receipt_id_ = socket_->RequestRpc(
        QStringLiteral("prompt.receipt"),
        BuildPromptReceiptParams(receipt_session, receipt_admission));
  }
}

void ProfileWorker::ApplyPendingSnapshotLocked(const QJsonObject& source) {
  if (!source.contains(QStringLiteral("pending"))) {
    return;
  }
  const int epoch = source.value(QStringLiteral("authority_epoch"))
                        .toInt(state_.pending_authority_epoch);
  const int revision = source.contains(QStringLiteral("revision"))
                           ? source.value(QStringLiteral("revision")).toInt(-1)
                           : -1;
  if (!PendingSnapshotWins(epoch, revision, state_.pending_authority_epoch,
                           state_.pending_revision)) {
    return;
  }
  std::vector<PendingAdmission> pending =
      ParsePendingAdmissions(source.value(QStringLiteral("pending")).toArray());
  if (!pending_prompts_baseline_ready_) {
    // Reconnect/resume snapshots describe history the UI already has. Seed the
    // baseline without emitting any user rows for admissions already started.
    pending_prompts_.Reset(pending);
    pending_prompts_baseline_ready_ = true;
  } else {
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    for (const PendingAdmission& admission :
         pending_prompts_.Observe(pending, now_ms)) {
      AppendUserPromptLocked(admission.text);
    }
  }
  state_.pending_admissions = std::move(pending);
  // Revisions are only comparable inside one authority epoch. When a new
  // owner starts numbering over, keep its first revision even if it is zero,
  // otherwise a later snapshot from the new epoch looks older than the old
  // epoch's last revision.
  const bool new_epoch = epoch != state_.pending_authority_epoch;
  state_.pending_authority_epoch = epoch;
  if (new_epoch || revision > 0) {
    state_.pending_revision = revision;
  }
}

ChatMessage* ProfileWorker::StreamingAssistantLocked() {
  for (auto it = state_.messages.rbegin(); it != state_.messages.rend(); ++it) {
    if (it->streaming) {
      return &(*it);
    }
    if (it->role == MessageRole::kUser) {
      break;
    }
  }
  return nullptr;
}

void ProfileWorker::ReparseStreamingLocked(ChatMessage* message) {
  if (message == nullptr) {
    return;
  }
  message->blocks = ParseMarkdown(message->raw_text);
  if (message->blocks.empty()) {
    MessageBlock paragraph;
    paragraph.kind = BlockKind::kParagraph;
    message->blocks.push_back(std::move(paragraph));
  }
}

void ProfileWorker::AppendAssistantDelta(const QString& text) {
  if (text.isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  ChatMessage* target = StreamingAssistantLocked();
  if (target == nullptr) {
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("Hermes");
    message.timestamp = QDateTime::currentDateTime();
    message.streaming = true;
    MessageBlock paragraph;
    paragraph.kind = BlockKind::kParagraph;
    message.blocks.push_back(std::move(paragraph));
    state_.messages.push_back(std::move(message));
    target = &state_.messages.back();
  }
  target->raw_text += text;
  // Re-parse while the answer is still arriving so headings, lists and fenced
  // code appear as blocks instead of landing all at once on completion.
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - last_reparse_ms_ >= 66) {
    last_reparse_ms_ = now;
    ReparseStreamingLocked(target);
  }
  MaybePublishLocked();
}

void ProfileWorker::AppendReasoningDelta(const QString& text, bool streaming) {
  if (text.isEmpty()) {
    return;
  }
  if (!streaming) {
    // `reasoning.available` repeats the whole body when a provider streamed it
    // as deltas. Appending it again doubled the panel (and its scroll height),
    // so a non-streaming update replaces text the deltas already produced.
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage* target = StreamingAssistantLocked();
    if (target != nullptr && !target->reasoning.isEmpty()) {
      target->reasoning_streaming = false;
      MaybePublishLocked();
      return;
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  ChatMessage* target = StreamingAssistantLocked();
  if (target == nullptr) {
    // Reasoning can start before the first message.delta. Keep it on the
    // answer the deltas will attach to, in the same message.
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("Hermes");
    message.timestamp = QDateTime::currentDateTime();
    message.streaming = true;
    state_.messages.push_back(std::move(message));
    target = &state_.messages.back();
  }
  target->reasoning += text;
  target->reasoning_streaming = streaming;
  MaybePublishLocked();
}

void ProfileWorker::AppendMoaReasoning(const QString& text, bool replace) {
  if (text.isEmpty()) {
    return;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  ChatMessage* target = StreamingAssistantLocked();
  if (target == nullptr) {
    ChatMessage message;
    message.role = MessageRole::kAssistant;
    message.author = QStringLiteral("Hermes");
    message.timestamp = QDateTime::currentDateTime();
    message.streaming = true;
    state_.messages.push_back(std::move(message));
    target = &state_.messages.back();
  }
  // The first MoA line replaces stale provider reasoning, and the first
  // reference replaces the aggregate progress block. Later frames append.
  if (replace) {
    target->reasoning.clear();
  }
  target->reasoning += text;
  target->reasoning_streaming = true;
  MaybePublishLocked();
}

void ProfileWorker::CompleteAssistant(const QString& text, bool error) {
  bool superseded = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ChatMessage* target = nullptr;
    // Walk backwards: a user row newer than the streaming answer means this
    // completion belongs to a turn that Inject already replaced. Cancelling a
    // started admission interrupts it, but its `message.complete` can arrive
    // after the replacement prompt was submitted; letting it clear `running`
    // and `admission_id_` here would make the new turn look dead.
    for (auto it = state_.messages.rbegin(); it != state_.messages.rend();
         ++it) {
      if (it->role == MessageRole::kUser) {
        superseded = true;
      }
      if (it->streaming) {
        target = &(*it);
        break;
      }
    }
    if (target == nullptr && superseded) {
      qInfo("ignoring completion for superseded turn session=%.12s",
            qUtf8Printable(state_.selected_session_id));
      return;
    }
    if (target == nullptr) {
      ChatMessage message;
      message.role = MessageRole::kAssistant;
      message.author = QStringLiteral("Hermes");
      message.timestamp = QDateTime::currentDateTime();
      state_.messages.push_back(std::move(message));
      target = &state_.messages.back();
    }
    if (!text.trimmed().isEmpty()) {
      target->raw_text = text;
      target->blocks = ParseMarkdown(text);
    } else if (!target->raw_text.isEmpty()) {
      target->blocks = ParseMarkdown(target->raw_text);
    }
    if (target->timestamp.isValid()) {
      const qint64 elapsed_ms =
          target->timestamp.msecsTo(QDateTime::currentDateTime());
      if (elapsed_ms >= 0) {
        target->duration_secs = static_cast<double>(elapsed_ms) / 1000.0;
      }
    }
    target->streaming = false;
    target->reasoning_streaming = false;
    if (superseded) {
      // Keep the replacement turn's running/admission bookkeeping intact.
      NoteActivityLocked();
      PublishLocked();
      qInfo("turn complete (superseded) session=%.12s chars=%lld error=%d",
            qUtf8Printable(state_.selected_session_id),
            static_cast<qint64>(text.size()), error ? 1 : 0);
      return;
    }
    state_.running = false;
    state_.running_session_id.clear();
    if (error) {
      state_.error_text = text;
      state_.status_text = QStringLiteral("error");
    } else {
      state_.error_text.clear();
      state_.status_text = QStringLiteral("ready");
    }
    NoteActivityLocked();
    state_.requests.clear();
    admission_id_.clear();
    admission_status_.clear();
    pending_receipt_id_ = -1;
    qInfo("turn complete session=%.12s chars=%lld error=%d",
          qUtf8Printable(state_.selected_session_id),
          static_cast<qint64>(text.size()), error ? 1 : 0);
    PublishLocked();
  }
  if (message_refresh_timer_ != nullptr) {
    message_refresh_timer_->start(700);
  }
  QTimer::singleShot(800, this, [this]() { FetchSessions(); });
}

void ProfileWorker::AppendToolRow(const QString& id, const QString& name,
                                  const QString& detail) {
  if (!id.isEmpty()) {
    for (ChatMessage& message : state_.messages) {
      for (MessageBlock& block : message.blocks) {
        if (block.kind == BlockKind::kTool && block.id == id) {
          block.detail = detail;
          return;
        }
      }
    }
  }
  MessageBlock block;
  block.kind = BlockKind::kTool;
  block.text = name;
  block.detail = detail;
  block.id = id;
  // Keep consecutive tool calls in one assistant message: the transcript
  // collapses that run into a single "N actions" row instead of one message
  // and one full-width row per call. A text reply ends the run because the
  // last message then holds prose, not only tool blocks.
  if (!state_.messages.empty()) {
    ChatMessage& last = state_.messages.back();
    const bool tool_only =
        !last.blocks.empty() &&
        std::all_of(last.blocks.begin(), last.blocks.end(),
                    [](const MessageBlock& existing) {
                      return existing.kind == BlockKind::kTool;
                    });
    if (last.role == MessageRole::kAssistant && tool_only) {
      last.blocks.push_back(std::move(block));
      return;
    }
  }
  ChatMessage message;
  message.id = id;
  message.role = MessageRole::kAssistant;
  message.author = QStringLiteral("Hermes");
  message.timestamp = QDateTime::currentDateTime();
  message.blocks.push_back(std::move(block));
  state_.messages.push_back(std::move(message));
}

void ProfileWorker::UpdateToolRow(const QString& id, const QString& detail) {
  if (id.isEmpty()) {
    return;
  }
  for (ChatMessage& message : state_.messages) {
    for (MessageBlock& block : message.blocks) {
      if (block.kind == BlockKind::kTool && block.id == id) {
        block.detail = detail;
        return;
      }
    }
  }
}

void ProfileWorker::RespondRequest(const QString& request_id,
                                   const QJsonObject& result) {
  QString id = request_id;
  QString method;
  QString session_id;
  bool via_rpc = false;
  int execution_generation = -1;
  std::vector<ClarifyQuestion> questions;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = state_.requests.begin(); it != state_.requests.end(); ++it) {
      if (it->id == id) {
        method = it->method;
        session_id = it->session_id;
        via_rpc = it->via_rpc;
        execution_generation = it->execution_generation;
        questions = it->questions;
        if (method == QStringLiteral("connection")) {
          // The operation stays live until a `connection.update` settles it.
          // Mark the answered rows in-flight so the card cannot double-send.
          for (const QJsonValue& value :
               result.value(QStringLiteral("targets")).toArray()) {
            const QJsonObject answer = value.toObject();
            const QString name =
                answer.value(QStringLiteral("name")).toString();
            const QString status =
                answer.value(QStringLiteral("status")).toString();
            for (ConnectionTarget& target : it->connection.targets) {
              if (target.name == name) {
                target.state = status == QStringLiteral("skipped")
                                   ? QStringLiteral("skipped")
                                   : QStringLiteral("initiated");
                break;
              }
            }
          }
          ++it->revision;
        } else {
          state_.requests.erase(it);
        }
        break;
      }
    }
    PublishLocked();
  }
  if (id.isEmpty() || socket_ == nullptr || !socket_->IsConnected()) {
    return;
  }

  if (method == QStringLiteral("connection")) {
    socket_->RespondConnection(id, session_id, result);
    qInfo("connection.respond profile=%s op=%s", qUtf8Printable(name_),
          qUtf8Printable(id));
    return;
  }

  if (via_rpc) {
    // Event-origin prompt: answer through the session RPC, not a JSON-RPC
    // response frame. Batch clarify locks each question; the last one resolves
    // the blocked tool call.
    if (method == QStringLiteral("approval")) {
      QString choice = result.value(QStringLiteral("choice")).toString();
      if (choice.isEmpty()) {
        choice = QStringLiteral("deny");
      }
      socket_->RespondPrompt(QStringLiteral("approval"), session_id,
                             execution_generation, id, choice);
    } else if (method == QStringLiteral("clarify")) {
      const QJsonObject answers =
          result.value(QStringLiteral("answers")).toObject();
      if (questions.size() <= 1) {
        QString answer;
        if (!questions.empty()) {
          answer = answers.value(questions.front().qid).toString();
        } else if (!answers.isEmpty()) {
          answer = answers.begin().value().toString();
        }
        socket_->RespondPrompt(QStringLiteral("clarify"), session_id,
                               execution_generation, id, answer);
      } else {
        for (const ClarifyQuestion& question : questions) {
          socket_->LockClarify(id, question.qid,
                               answers.value(question.qid).toString());
        }
      }
    }
    qInfo("respond-rpc profile=%s method=%s keys=%lld", qUtf8Printable(name_),
          qUtf8Printable(method), static_cast<qint64>(result.size()));
    return;
  }

  socket_->Respond(QJsonValue(id), result);
  // Log the shape, never the values: `result` can carry a secret.
  qInfo("respond profile=%s method=%s keys=%lld", qUtf8Printable(name_),
        qUtf8Printable(method), static_cast<qint64>(result.size()));
}

void ProfileWorker::PublishLocked() {
  ++state_.revision;
  published_ = std::make_shared<ProfileSnapshot>(state_);
  last_publish_ms_ = QDateTime::currentMSecsSinceEpoch();
  cache_dirty_ = true;
}

void ProfileWorker::MaybePublishLocked() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (now - last_publish_ms_ < kPublishIntervalMs) {
    return;
  }
  PublishLocked();
}

void ProfileWorker::PublishError(const QString& error) {
  if (shutting_down_) {
    return;
  }
  qWarning("profile %s: %s", qUtf8Printable(name_), qUtf8Printable(error));
  std::lock_guard<std::mutex> lock(mutex_);
  state_.error_text = error;
  state_.status_text = QStringLiteral("error");
  state_.running = false;
  PublishLocked();
}

void ProfileWorker::NoteActivityLocked() {
  if (!foreground_) {
    ++state_.unread;
  }
}

}  // namespace hermes::ui
