// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_MODEL_H_
#define HERMES_UI_MODEL_H_

#include <QColor>
#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

#include "src/subagents.h"
#include "src/todo_state.h"

namespace hermes::ui {

enum class MessageRole {
  kUser,
  kAssistant,
  // Client-side notice (slash-command feedback, queue/steer confirmations).
  // Rendered as a muted centred pill, never sent to the model.
  kSystem,
};

enum class BlockKind {
  kParagraph,
  kHeading,
  kBullet,
  kCode,
  kTool,
  kQuote,
  // A horizontal rule. `text` is empty; the transcript paints it as a hairline.
  kRule,
  // A markdown table. `text` keeps the source; `detail` holds rendered HTML.
  kTable,
  // A standalone image. `text` is the alt text, `detail` the source URL/path,
  // `language` the optional title.
  kImage,
  // Fenced `diff` blocks keep syntax colouring but get their own kind so the
  // transcript can add add/remove gutters.
  kDiff,
  // Display math (```math or $$...$$). `text` is the TeX source.
  kMath,
  // Mermaid source. `text` is the diagram definition; the native transcript
  // renders it as a labelled code card until a diagram engine is available.
  kMermaid,
  // An artifact embed (`:::artifact path`) with a caption in `text` and the
  // artifact source in `detail`.
  kArtifact,
  // A footnote definition (`[^id]: text`); `detail` carries the footnote id.
  kFootnote,
};

// One renderable unit inside a message. Markdown is parsed into these in the
// worker thread so painting never re-parses text.
struct MessageBlock {
  BlockKind kind = BlockKind::kParagraph;
  QString text;
  QString language;
  QString detail;
  // Tool rows carry the gateway tool-call id so tool.complete can update the
  // right row when several calls share one grouped message.
  QString id;
};

// One slash command as the gateway's catalog reports it (commands.catalog).
struct SlashEntry {
  QString name;
  QString description;
  QString category;
  QStringList aliases;
};

// One emoji reaction hydrated from a transcript row's `display_metadata` or a
// `message.reaction` event. The native transcript paints a compact footer
// instead of a full reaction picker.
struct ChatReaction {
  QString emoji;
  QString author;
};

// One agent notice from `notification.show`. `key` is stable, so an escalating
// credits notice replaces its previous line instead of stacking.
struct GatewayNotice {
  QString key;
  QString text;
  QString level;
  QString kind;
  QString detail;
  int ttl_ms = 0;
  quint64 serial = 0;
};

struct ChatMessage {
  // Stable identity for tool rows; empty for ordinary messages.
  QString id;
  // Durable message row id from GET /api/sessions/<id>/messages. Rewind/edit/
  // regenerate address history by this row, never by transcript ordinal.
  qint64 row_id = 0;
  MessageRole role = MessageRole::kAssistant;
  QString author;
  QDateTime timestamp;
  std::vector<MessageBlock> blocks;
  bool streaming = false;
  // Model reasoning ("thinking") streamed ahead of the answer. The gateway
  // sends it as its own reasoning.delta events, never inside the message text,
  // so it is kept beside the blocks and rendered as a collapsible section.
  QString reasoning;
  bool reasoning_streaming = false;
  // Source markdown for a streaming answer. Blocks are re-parsed from this on
  // a timer so tables/code/lists appear while the answer is still arriving.
  QString raw_text;
  // Wall-clock seconds for a completed live turn, mirroring the desktop's
  // one-shot `durationS` metadata. Hydrated history rows leave this at -1.
  double duration_secs = -1;
  // Emoji reactions shown under the message footer.
  std::vector<ChatReaction> reactions;
};

// One row of /api/sessions. Times are Unix seconds, matching the gateway.
struct SessionSummary {
  QString id;
  QString title;
  QString preview;
  QString source;
  // Project that requested a local `session.create`. The gateway writes the
  // session row (and `projects.tree` membership) asynchronously, so this lets
  // the sidebar keep a fresh new chat under the project that started it
  // without treating every selected local chat as a member of every project.
  QString project_id;
  QString model;
  QString provider;
  // Empty when the gateway list does not carry it yet. The cache and live
  // session.info events fill this in so the composer reflects the session's
  // own reasoning effort after a switch or restart.
  QString reasoning_effort;
  qint64 started_at_secs = 0;
  qint64 last_active_secs = 0;
  int message_count = 0;
  bool pinned = false;
  bool archived = false;
  bool unread = false;
  // Genuine /branch forks carry `_branched_from`; /new and idle/daily resets
  // carry `_reset_from`. Both also expose `parent_session_id`, so keep the
  // specific edge when the backend sends it and fall back to the parent.
  QString parent_session_id;
  QString branched_from;
  QString reset_from;
  // `compression` when this row continues a sealed segment of the same
  // conversation rather than being a user-created fork.
  QString continuation_kind;
  // Runtime revision the sidebar mutation fence expects (PATCH /api/sessions
  // refuses an unfenced write with client_upgrade_required).
  int revision = 0;
};

// One question in a batch clarify request. The gateway sends 1-5 of these in
// a single blocking server request; `choices` is empty for a free-text answer.
struct ClarifyQuestion {
  QString qid;
  QString question;
  QStringList choices;
  bool multi_select = false;
};

// One credential an MCP install is still waiting for. The card renders a
// field per entry; values only travel back in a connection answer.
struct ConnectionEnvField {
  QString name;
  QString prompt;
  QString default_value;
  bool required = false;
  bool secret = false;
};

// One target of a connection operation. State is backend-owned: the card may
// ask, skip or open a link, but only `connection.update` settles a row.
struct ConnectionTarget {
  QString name;
  QString kind;    // connector, mcp, plugin, skill
  QString action;  // authorize, connect, enable, install, reconnect
  QString state;   // pending, initiated, connected, skipped, failed, expired...
  QString detail;
  QString connect_url;
  QString instructions;
  QString discovery_error;
  QString connection_id;
  QString alias;
  QStringList tools;
  std::vector<ConnectionEnvField> required_env;
  bool resolved = false;
};

// `connection.request` / `connection.update` payload. Every update carries
// the full target snapshot plus a monotonic `seq`; an older frame must not
// move a row backwards.
struct ConnectionOperation {
  QString op_id;
  QString tool_call_id;
  // `session` operations park a card in the transcript; `account` operations
  // belong to the Capabilities connector page and can outlive a session.
  QString owner_type;
  int seq = 0;
  double deadline_at = 0;
  bool settled = false;
  QString settled_by;
  QStringList suggested;  // display operations: pinned connector slugs
  std::vector<ConnectionTarget> targets;
};

inline bool AccountOperationIncludes(const ConnectionOperation& operation,
                                     const QString& connector) {
  for (const ConnectionTarget& target : operation.targets) {
    if (target.name == connector) {
      return true;
    }
  }
  return false;
}

// Desktop `accountOperationFor`: prefer the open operation for this connector,
// otherwise the newest settled one. Order is insertion order; a replaced
// operation keeps its slot.
inline const ConnectionOperation* AccountOperationFor(
    const std::vector<ConnectionOperation>& operations,
    const QString& connector) {
  for (const ConnectionOperation& operation : operations) {
    if (!operation.settled &&
        operation.owner_type == QStringLiteral("account") &&
        AccountOperationIncludes(operation, connector)) {
      return &operation;
    }
  }
  for (auto it = operations.rbegin(); it != operations.rend(); ++it) {
    if (it->owner_type == QStringLiteral("account") &&
        AccountOperationIncludes(*it, connector)) {
      return &(*it);
    }
  }
  return nullptr;
}

// A backend -> client JSON-RPC request. The gateway blocks the agent until the
// client answers with the same string id, so leaving one unanswered stalls the
// turn for the request's deadline (300 s for clarify). Values for secret/sudo
// are never stored in this struct or in the snapshot; only the prompt is.
struct PendingRequest {
  QString id;
  QString method;  // approval, clarify, sudo, secret, vault.*
  QString session_id;
  QString title;
  QString detail;
  QString placeholder;
  QStringList choices;  // approval choices
  std::vector<ClarifyQuestion> questions;
  bool secret = false;
  // Gateway API sessions publish approval.request / clarify.request events
  // and answer them with an RPC (approval.respond / clarify.respond). The TUI
  // stdio transport instead sends a JSON-RPC request frame and expects a
  // response frame with the same id. Same card, different reply channel.
  bool via_rpc = false;
  // Canonical prompt RPCs are generation-fenced: the event's
  // execution_generation must travel back with the answer.
  int execution_generation = -1;
  // Set for method == "connection". The operation's op id is also `id`, but
  // the full snapshot drives the target rows and must survive updates.
  ConnectionOperation connection;
  // Bumped on every authoritative connection snapshot so a card rebuilds
  // without destroying typed credential fields on unrelated polls.
  int revision = 0;
};

// Live token accounting for the active session, merged from session.usage
// ticks. `context_used`/`context_max` drive the composer meter; the counters
// are kept so later ticks can fill fields the gateway omitted.
struct SessionUsage {
  qint64 context_used = 0;
  qint64 context_max = 0;
  int context_percent = 0;
  bool context_estimated = false;
  qint64 input = 0;
  qint64 output = 0;
  qint64 total = 0;
  int calls = 0;
  int compressions = 0;
};

// One row of /api/model/options. `models` are picker ids, not display names.
struct ModelProvider {
  QString name;
  QString slug;
  QStringList models;
};

// One lazily loaded Settings data page. The worker fills `data` from the REST
// routes the desktop page reads; `data` is keyed by short source names
// ("env", "endpoints", "status") rather than raw paths so the view does not
// re-implement the route table.
struct PageState {
  bool loading = false;
  bool loaded = false;
  QString error;
  QString notice;
  QJsonObject data;
};

// One admission still owned by the gateway FIFO. The desktop queue panel is
// driven by `session.info.pending` / `session.resume.pending`, not by
// optimistic client state: an admission can be queued, transfer to a running
// turn as a steer, or disappear when it is cancelled/settled.
struct PendingAdmission {
  QString admission_id;
  QString input_id;
  QString text;
  QString status;   // queued, started, unknown, terminal
  QString outcome;  // owner outcome once settled
  QStringList controls;
  bool steering = false;
  int sequence = 0;
  int authority_epoch = 0;
  int execution_generation = -1;
};

// Immutable snapshot published by a ProfileWorker. The UI holds a shared_ptr
// and never blocks on the worker's socket, HTTP or state mutex.
struct ProfileSnapshot {
  QString name;
  QString label;
  QString home;
  QColor color;
  bool is_default = false;
  bool connected = false;
  bool gateway_ready = false;
  bool running = false;
  // Session whose turn is actually running on this profile. A profile worker
  // can keep working while the UI views another chat; the composer only shows
  // "generating" when this matches the selected session.
  QString running_session_id;
  int unread = 0;
  int today_count = 0;
  int revision = 0;
  QString selected_session_id;
  QString status_text;
  QString error_text;
  QString gateway_version;
  QString model;
  QString provider;
  QString reasoning_effort = QStringLiteral("medium");
  // Artifacts view: the directory the file browser is showing and the file the
  // preview pane last asked for. Both live on the worker so the view never
  // touches the network.
  QString artifacts_cwd;
  QString artifacts_preview;
  // Files uploaded through the composer's attach button. Each entry is a
  // gateway-visible path; the serial lets the shell hand only the new ones to
  // the composer after a poll.
  QStringList attachments;
  int attachment_serial = 0;
  // Full slash-command catalog from the gateway; empty until it lands, in
  // which case the shell falls back to its built-in list.
  std::vector<SlashEntry> slash_commands;
  int slash_catalog_serial = 0;
  std::vector<ModelProvider> model_providers;
  SessionUsage usage;
  // Settings route: the full config record and its schema, loaded on demand
  // from GET /api/config and /api/config/schema. QJsonObject is implicitly
  // shared, so copying the snapshot on every poll is cheap.
  bool config_loaded = false;
  bool config_saving = false;
  QString config_error;
  QJsonObject config;
  QJsonObject config_schema;
  QJsonArray config_category_order;
  // Oldest first. The UI shows the first request matching the selected
  // session; the rest stay queued so a second card cannot clobber the first.
  std::vector<PendingRequest> requests;
  // Latest account-owned connector operations, keyed by op id. The
  // Capabilities page merges these into connector rows; account operations do
  // not have a session card.
  std::vector<ConnectionOperation> account_operations;
  // Gateway-owned pending admissions for the selected session, in FIFO order.
  // The dialog uses these as the authoritative queue; `queued_prompts_` is
  // only legacy local history for gateways that predate prompt.cancel.
  std::vector<PendingAdmission> pending_admissions;
  int pending_authority_epoch = 0;
  int pending_revision = -1;
  std::vector<SessionSummary> sessions;
  std::vector<ChatMessage> messages;
  // Live delegated children reported by subagent.* events. Kept beside the
  // transcript because the Agents panel reads them without another route.
  std::vector<SubagentProgress> subagents;
  // Latest full todo snapshot from `todo.updated`. List order is priority.
  std::vector<TodoItem> todos;
  int todo_revision = 0;
  // Gateway microphone capture. The profile worker owns these because
  // voice.status/partial/transcript are transport-wide and arrive without a
  // session id; the shell copies the newest transcript into the composer once
  // per `voice_transcript_serial`.
  bool voice_recording = false;
  QString voice_partial;
  QString voice_status;
  QString voice_transcript;
  quint64 voice_transcript_serial = 0;
  // Session control snapshot from `session.control.update` (goal / loop /
  // heartbeat). Rendered as one summary line in the right rail.
  QString session_control_summary;
  quint64 session_control_serial = 0;
  // Account-wide agent notices; the newest serial drives the status bar toast.
  std::vector<GatewayNotice> gateway_notices;
  quint64 gateway_notice_serial = 0;
  // `reaction` (kind "vibe") is a transient acknowledgement, not transcript
  // state. The shell flashes it once per serial.
  QString reaction_kind;
  quint64 reaction_serial = 0;
  // Settings data pages, fetched on demand by the Settings route.
  QHash<QString, PageState> pages;
};

// True when the selected chat should show a live turn. A streaming message is
// authoritative even if a late `session.info {running:false}` cleared the flag;
// otherwise the spinner vanished while reasoning/text was still arriving.
inline bool SelectedTurnRunning(const ProfileSnapshot& snapshot) {
  const bool streaming = !snapshot.messages.empty() &&
                         (snapshot.messages.back().streaming ||
                          snapshot.messages.back().reasoning_streaming);
  return (snapshot.running || streaming) &&
         (snapshot.running_session_id.isEmpty() ||
          snapshot.running_session_id == snapshot.selected_session_id);
}

}  // namespace hermes::ui

#endif  // HERMES_UI_MODEL_H_
