// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_PAINT_UTILS_H_
#define HERMES_UI_PAINT_UTILS_H_

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QRectF>
#include <QString>

namespace hermes::ui {

void FillRounded(QPainter &painter, const QRectF &rect, qreal radius,
                 const QColor &color);

void StrokeRounded(QPainter &painter, const QRectF &rect, qreal radius,
                   const QColor &color, qreal width = 1.0);

// Draws one codicon glyph centered inside `box`. `codepoint` is the UTF-16
// value from @vscode/codicons (for example 0xEB06 is `home`).
void DrawCodicon(QPainter &painter, const QRectF &box, ushort codepoint,
                 const QColor &color, int pixel_size);

void DrawElidedText(QPainter &painter, const QRectF &rect, int flags,
                    const QString &text, const QColor &color,
                    const QFont &font);

// Cheap layered shadow: a few rounded rects with falling alpha. The real app
// uses a CSS multi-layer box-shadow; this reproduces its soft falloff closely
// enough for a screen diff.
void DrawSoftShadow(QPainter &painter, const QRectF &rect, qreal radius,
                    int alpha, int spread = 10);

} // namespace hermes::ui

#endif // HERMES_UI_PAINT_UTILS_H_
