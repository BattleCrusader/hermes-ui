// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_INPUT_CACHE_H_
#define HERMES_UI_INPUT_CACHE_H_

#include <QString>

class QWidget;

namespace hermes::ui {

// Text controls cache their last user-edited value so an app crash does not
// throw away a half-written prompt, project description or settings field.
// Values are appended to a small journal as they change (one write per user
// edit), then compacted into input-drafts.json. A process crash therefore
// leaves the latest value on disk without an fsync on every keystroke.
//
// InstallInputDraftCache() watches widgets as they are shown. Composer editors
// opt out and use the explicit key/value helpers below because their draft is
// scoped to a profile + session rather than to widget geometry.
void InstallInputDraftCache();
// Test seams: a reset behaves like a process restart for the in-memory store.
void UninstallInputDraftCacheForTest();
void ResetInputDraftCacheForTest();

QString CachedInputDraft(const QString& key);
void CacheInputDraft(const QString& key, const QString& text);
// Re-apply a widget's cached draft after application code populated it (e.g.
// an async task detail). A no-op when the widget has no cached value.
void RestoreInputDraft(QWidget* widget);
void FlushInputDraftCache();
// Drop every cached field below `root`. Dialog acceptance uses this so a
// submitted form does not come back on the next visit; cancellation keeps it.
void ClearInputDrafts(QWidget* root);

}  // namespace hermes::ui

#endif  // HERMES_UI_INPUT_CACHE_H_
