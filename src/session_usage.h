// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SESSION_USAGE_H_
#define HERMES_UI_SESSION_USAGE_H_

#include <QJsonObject>

#include "src/model.h"

namespace hermes::ui {

// Merge one session.usage payload into `usage`. Two shapes travel this event:
// the embedded ticker's nested `usage` view and the older top-level counters.
// The nested view wins field by field; absent fields keep their prior values.
SessionUsage MergeSessionUsage(const SessionUsage& usage,
                               const QJsonObject& payload);

}  // namespace hermes::ui

#endif  // HERMES_UI_SESSION_USAGE_H_
