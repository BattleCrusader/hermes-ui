// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_QR_CODE_H_
#define HERMES_UI_QR_CODE_H_

#include <QImage>
#include <QString>

namespace hermes::ui {

// Renders `payload` as a square, high-contrast QR image. Returns a null image
// when the payload is empty or too large for a QR symbol. The function is pure
// and thread-safe; callers may run it off the UI thread.
QImage RenderQrImage(const QString& payload, int target_pixels,
                     int border_modules = 4);

}  // namespace hermes::ui

#endif  // HERMES_UI_QR_CODE_H_
