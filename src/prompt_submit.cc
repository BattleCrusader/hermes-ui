// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/prompt_submit.h"

#include <QFileInfo>
#include <QSet>
#include <vector>

namespace hermes::ui {

QJsonObject BuildPromptSubmitParams(const QString& session_id,
                                    const QString& text, const QString& profile,
                                    const QString& submission_id,
                                    qint64 truncate_before_row_id,
                                    bool confirm_empty_truncate,
                                    bool confirm_deep_truncate, bool queued) {
  QJsonObject params;
  params.insert(QStringLiteral("session_id"), session_id);
  params.insert(QStringLiteral("text"), text);
  params.insert(QStringLiteral("submission_id"), submission_id);
  if (queued) {
    params.insert(QStringLiteral("queued"), true);
  }
  if (!profile.isEmpty()) {
    params.insert(QStringLiteral("profile"), profile);
  }
  if (truncate_before_row_id > 0) {
    params.insert(QStringLiteral("confirm_truncate"), true);
    params.insert(QStringLiteral("truncate_before_row_id"),
                  static_cast<double>(truncate_before_row_id));
    if (confirm_empty_truncate) {
      params.insert(QStringLiteral("confirm_empty_truncate"), true);
    }
    if (confirm_deep_truncate) {
      params.insert(QStringLiteral("confirm_deep_truncate"), true);
    }
  }
  return params;
}

PromptTruncation ComputePromptTruncation(
    const std::vector<qint64>& user_row_ids, qint64 target_row_id) {
  bool saw_before = false;
  bool saw_target = false;
  bool saw_after = false;
  for (qint64 row_id : user_row_ids) {
    saw_before = saw_before || row_id < target_row_id;
    saw_target = saw_target || row_id == target_row_id;
    saw_after = saw_after || row_id > target_row_id;
  }
  PromptTruncation truncation;
  truncation.confirm_empty_truncate = saw_target && !saw_before;
  truncation.confirm_deep_truncate = saw_target && saw_after;
  return truncation;
}

QString AttachmentAttachMethod(const QString& path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == QStringLiteral("pdf")) {
    return QStringLiteral("pdf.attach");
  }
  static const QSet<QString> kImages = {
      QStringLiteral("png"),  QStringLiteral("jpg"),  QStringLiteral("jpeg"),
      QStringLiteral("gif"),  QStringLiteral("webp"), QStringLiteral("bmp"),
      QStringLiteral("heic"), QStringLiteral("tif"),  QStringLiteral("tiff")};
  return kImages.contains(suffix) ? QStringLiteral("image.attach")
                                  : QStringLiteral("file.attach");
}

QJsonObject BuildAttachmentAttachParams(const QString& path,
                                        const QString& session_id) {
  QJsonObject params{
      {QStringLiteral("session_id"), session_id},
      {QStringLiteral("path"), path},
  };
  if (AttachmentAttachMethod(path) == QStringLiteral("file.attach")) {
    params.insert(QStringLiteral("name"), QFileInfo(path).fileName());
  }
  return params;
}

QString ComposePinnedPrompt(const QStringList& pinned, const QString& text) {
  QStringList bodies;
  for (const QString& body : pinned) {
    const QString trimmed = body.trimmed();
    if (!trimmed.isEmpty()) {
      bodies.append(trimmed);
    }
  }
  if (bodies.isEmpty()) {
    return text;
  }
  QString context = QStringLiteral("Pinned context from earlier replies:");
  for (const QString& body : bodies) {
    context += QStringLiteral("\n\n---\n") + body;
  }
  return context + QStringLiteral("\n\n---\n\n") + text;
}

}  // namespace hermes::ui
