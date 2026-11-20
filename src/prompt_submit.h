// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROMPT_SUBMIT_H_
#define HERMES_UI_PROMPT_SUBMIT_H_

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace hermes::ui {

// Builds `prompt.submit` params. A rewind/edit/regenerate submit must say so:
// `confirm_truncate` is the gateway's permission to drop history, and the row
// id names the user turn to truncate BEFORE (target turn and everything after).
// Empty-history and deep (archiving later user turns) cuts each carry their
// own confirmation bit, so a stray row id can never delete more than the user
// confirmed from the transcript UI.
QJsonObject BuildPromptSubmitParams(const QString& session_id,
                                    const QString& text, const QString& profile,
                                    const QString& submission_id,
                                    qint64 truncate_before_row_id = 0,
                                    bool confirm_empty_truncate = false,
                                    bool confirm_deep_truncate = false,
                                    bool queued = false);

// The gateway queues one attachment verb at a time: images and PDFs have
// dedicated handlers (image.attach / pdf.attach), every other file goes
// through file.attach. The returned params are the RPC params for that verb.
QString AttachmentAttachMethod(const QString& path);
QJsonObject BuildAttachmentAttachParams(const QString& path,
                                        const QString& session_id);

// `file.attach` stages a copy in the session workspace and returns the
// `@file:` ref the gateway expands on the next submit. Images and PDFs queue
// their own tickets, so only file.attach contributes a prompt ref.
QString AttachmentRefFromResult(const QJsonObject& result);

// A PDF renderer can be unavailable on the gateway (pdftoppm is an optional
// system dependency). Retry that one media verb through file.attach so the
// document still reaches the model as an attached text ref. Every other
// attachment failure is reported, never silently retried as a different kind.
QString AttachmentRetryMethod(const QString& method);

// `attached:false` is a failure even when the gateway did not also raise an
// RPC error. Images/PDFs return attached=true once queued.
bool AttachmentResultSucceeded(const QJsonObject& result,
                               const QString& error);

// Composer order wins over gateway response order. The refs are transport
// metadata: each line is prepended ahead of the authored prompt, as the
// desktop does, so every attached file expands in the session workspace.
QString ComposePromptWithAttachmentRefs(const QString& text,
                                        const QStringList& refs);

struct PromptTruncation {
  bool confirm_empty_truncate = false;
  bool confirm_deep_truncate = false;
};

// Classifies a rewind against the session's user-row order. Dropping the first
// user turn empties history; dropping one with later user turns archives a deep
// tail. Both need explicit confirmation in `prompt.submit`.
PromptTruncation ComputePromptTruncation(
    const std::vector<qint64>& user_row_ids, qint64 target_row_id);

// The gateway has no RPC for pinning a transcript message into model context.
// The native shell keeps pinned assistant replies client-side and prefixes
// them to prompt.submit, mirroring the desktop's visible-context approach.
QString ComposePinnedPrompt(const QStringList& pinned, const QString& text);

}  // namespace hermes::ui

#endif  // HERMES_UI_PROMPT_SUBMIT_H_
