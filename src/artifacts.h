// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_ARTIFACTS_H_
#define HERMES_UI_ARTIFACTS_H_

#include <QString>
#include <optional>
#include <vector>

#include "src/model.h"

namespace hermes::ui {

// The three artifact shapes the desktop promotes out of the transcript: a
// substantial HTML document/fragment, a large standalone SVG, or a long code
// fence. Small snippets stay inline in the message.
enum class ArtifactKind {
  kCode,
  kHtml,
  kSvg,
};

struct ArtifactDetection {
  ArtifactKind kind = ArtifactKind::kCode;
  QString language;
  QString title;
};

// Desktop's `detectArtifact`: a pure, cheap decision over a fenced block. The
// thresholds intentionally match `apps/desktop/src/lib/artifact-detect.ts` so
// the native rail promotes the same blocks the desktop does.
std::optional<ArtifactDetection> DetectArtifact(const QString& language,
                                                const QString& code);

QString ArtifactKindName(ArtifactKind kind);
QString ArtifactSlug(const ArtifactDetection& detection);
QString ArtifactContentHash(const QString& content);
QString ArtifactDownloadName(ArtifactKind kind, const QString& language,
                             const QString& title);

struct ArtifactVersion {
  QString content;
  qint64 created_at_ms = 0;
  QString hash;
};

// One generated artifact, keyed by session + slug. Oldest → newest versions;
// the last row is current. This mirrors the desktop artifact registry without
// persisting generated content outside the transcript.
struct ArtifactRecord {
  QString id;
  QString session_id;
  QString slug;
  QString title;
  QString language;
  ArtifactKind kind = ArtifactKind::kCode;
  qint64 created_at_ms = 0;
  qint64 updated_at_ms = 0;
  std::vector<ArtifactVersion> versions;
};

// Rebuild the session's artifact registry from the transcript. Repeated
// `(kind, language, title)` fences append versions; a duplicate content hash is
// a no-op. Returned newest-updated first, ready for the rail.
std::vector<ArtifactRecord> CollectGeneratedArtifacts(
    const std::vector<ChatMessage>& messages, const QString& session_id);

}  // namespace hermes::ui

#endif  // HERMES_UI_ARTIFACTS_H_
