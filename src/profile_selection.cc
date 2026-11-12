// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/profile_selection.h"

namespace hermes::ui {

int ProfileIndexByName(const QStringList& profiles, const QString& name) {
  if (name.isEmpty()) {
    return -1;
  }
  for (int index = 0; index < profiles.size(); ++index) {
    if (profiles.at(index) == name) {
      return index;
    }
  }
  return -1;
}

}  // namespace hermes::ui
