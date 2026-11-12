// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/paint_utils.h"

#include <QFontMetrics>
#include <QPainterPath>
#include <QString>
#include <algorithm>

namespace hermes::ui {

void FillRounded(QPainter &painter, const QRectF &rect, qreal radius,
                 const QColor &color) {
  painter.save();
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);
  painter.drawRoundedRect(rect, radius, radius);
  painter.restore();
}

void StrokeRounded(QPainter &painter, const QRectF &rect, qreal radius,
                   const QColor &color, qreal width) {
  painter.save();
  painter.setBrush(Qt::NoBrush);
  QPen pen(color);
  pen.setWidthF(width);
  painter.setPen(pen);
  const qreal inset = width / 2.0;
  painter.drawRoundedRect(rect.adjusted(inset, inset, -inset, -inset), radius,
                          radius);
  painter.restore();
}

void DrawCodicon(QPainter &painter, const QRectF &box, ushort codepoint,
                 const QColor &color, int pixel_size) {
  QFont font(QStringLiteral("codicon"));
  font.setPixelSize(pixel_size);
  painter.save();
  painter.setFont(font);
  painter.setPen(color);
  painter.drawText(box, Qt::AlignCenter, QString(QChar(codepoint)));
  painter.restore();
}

void DrawElidedText(QPainter &painter, const QRectF &rect, int flags,
                    const QString &text, const QColor &color,
                    const QFont &font) {
  painter.save();
  painter.setFont(font);
  painter.setPen(color);
  const QFontMetrics metrics(font);
  const QString elided =
      metrics.elidedText(text, Qt::ElideRight, static_cast<int>(rect.width()));
  painter.drawText(rect, flags, elided);
  painter.restore();
}

void DrawSoftShadow(QPainter &painter, const QRectF &rect, qreal radius,
                    int alpha, int spread) {
  painter.save();
  painter.setPen(Qt::NoPen);
  for (int layer = spread; layer >= 1; --layer) {
    const int layer_alpha =
        std::max(1, alpha / (spread + 1) * (spread - layer + 1) / spread);
    QColor color(0, 0, 0, layer_alpha);
    painter.setBrush(color);
    painter.drawRoundedRect(rect.adjusted(-layer, -layer, layer, layer),
                            radius + layer, radius + layer);
  }
  painter.restore();
}

} // namespace hermes::ui
