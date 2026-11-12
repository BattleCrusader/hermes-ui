// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PROFILE_SELECTION_H_
#define HERMES_UI_PROFILE_SELECTION_H_

#include <QString>
#include <QStringList>

namespace hermes::ui {

// The index of `name` in `profiles`, or -1. Profile order is gateway-defined,
// so both the Ctrl+N shortcuts and startup restore resolve by name rather than
// assuming a saved numeric slot still points at the same profile.
int ProfileIndexByName(const QStringList& profiles, const QString& name);

}  // namespace hermes::ui

#endif  // HERMES_UI_PROFILE_SELECTION_H_
