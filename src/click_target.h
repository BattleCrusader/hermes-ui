// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_CLICK_TARGET_H_
#define HERMES_UI_CLICK_TARGET_H_

#include <QRectF>
#include <QString>
#include <vector>

namespace hermes::ui {

// One clickable region of a custom-painted surface. The surfaces paint their
// own controls, so hit geometry is part of the contract: tests walk these
// targets and click every one instead of guessing pixel positions.
struct ClickTarget {
  QString name;
  QRectF rect;
  // Modal targets open a native dialog/menu; an automated sweep records them
  // but does not click them (that would block the event loop).
  bool modal = false;
};

} // namespace hermes::ui

#endif // HERMES_UI_CLICK_TARGET_H_
