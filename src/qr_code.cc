// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/qr_code.h"

#include <QColor>
#include <QImage>
#include <QString>
#include <algorithm>
#include <string>

#include "third_party/qrcodegen/qrcodegen.hpp"

namespace hermes::ui {

QImage RenderQrImage(const QString& payload, int target_pixels,
                     int border_modules) {
  if (payload.isEmpty() || target_pixels <= 0) {
    return QImage();
  }
  const std::string utf8 = payload.toStdString();
  qrcodegen::QrCode code = qrcodegen::QrCode::encodeText(
      utf8.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
  const int border = std::max(0, border_modules);
  const int modules = code.getSize() + border * 2;
  const int module_pixels = std::max(1, target_pixels / modules);
  const int pixels = modules * module_pixels;
  QImage image(pixels, pixels, QImage::Format_RGB32);
  image.fill(Qt::white);
  for (int y = 0; y < code.getSize(); ++y) {
    for (int x = 0; x < code.getSize(); ++x) {
      if (!code.getModule(x, y)) {
        continue;
      }
      const int left = (x + border) * module_pixels;
      const int top = (y + border) * module_pixels;
      for (int py = 0; py < module_pixels; ++py) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(top + py));
        std::fill(line + left, line + left + module_pixels, qRgb(0, 0, 0));
      }
    }
  }
  return image;
}

}  // namespace hermes::ui
