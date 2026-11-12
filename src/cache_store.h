// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_CACHE_STORE_H_
#define HERMES_UI_CACHE_STORE_H_

#include <QJsonObject>
#include <QString>
#include <vector>

#include "src/model.h"
#include "src/projects.h"

namespace hermes::ui {

// A lightweight, per-profile snapshot of the session list and the selected
// transcript. It is written by the profile worker, never by the UI thread, so
// a restart can paint the last workspace while the gateway is still syncing.
struct CachedSessionState {
  QString profile;
  QString selected_session_id;
  std::vector<SessionSummary> sessions;
  std::vector<ChatMessage> messages;
  qint64 saved_at_ms = 0;
};

// The project tree/list is owned by the shell rather than the worker. Its cache
// keeps the visible project scope so an expanded project reopens in the same
// place instead of flashing the flat recency list.
struct CachedProjectState {
  QString profile;
  QString scope = AllProjectsId();
  ProjectsState projects;
  qint64 saved_at_ms = 0;
};

// Cache files live under HERMES_UI_CACHE when set (tests/portable runs), else
// the platform cache directory. Profile names are hashed into the filename so
// no separator or traversal sequence can escape the cache directory.
QString UiCacheRoot();
QString SessionCachePath(const QString& profile);
QString ProjectCachePath(const QString& profile);

bool LoadSessionCache(const QString& profile, CachedSessionState* cache);
bool SaveSessionCache(const CachedSessionState& cache,
                      QString* error = nullptr);
bool LoadProjectCache(const QString& profile, CachedProjectState* cache);
bool SaveProjectCache(const CachedProjectState& cache,
                      QString* error = nullptr);

// Serialization is exposed for tests and for crossing the background-task
// boundary without re-reading the cache file on the UI thread.
QJsonObject SessionCacheToJson(const CachedSessionState& cache);
bool SessionCacheFromJson(const QJsonObject& root, CachedSessionState* cache);
QJsonObject ProjectCacheToJson(const CachedProjectState& cache);
bool ProjectCacheFromJson(const QJsonObject& root, CachedProjectState* cache);

}  // namespace hermes::ui

#endif  // HERMES_UI_CACHE_STORE_H_
