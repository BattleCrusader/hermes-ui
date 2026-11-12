// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_SRC_PET_SPRITE_H_
#define HERMES_UI_SRC_PET_SPRITE_H_

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

namespace hermes::ui {

// Decoded `pet.info` payload. The gateway owns the sheet and its revision; the
// overlay only slices frames. Kept separate from the widget so decoding can run
// on a worker thread and be unit-tested without a window.
struct PetSpriteInfo {
  bool enabled = false;
  QString slug;
  QString display_name;
  QString mime;
  QString revision;
  QImage sheet;
  int frame_w = 192;
  int frame_h = 208;
  int frames_per_state = 6;
  int loop_ms = 1100;
  double scale = 0.33;
  QStringList state_rows;
  QHash<QString, int> frames_by_state;
  QHash<QString, int> frames_by_row;

  bool HasSheet() const;
  QSize DrawSize() const;
  QSize WindowSize() const;
  QString RowNameForState(const QString& state) const;
  int RowForState(const QString& state) const;
  int FrameCountForRow(int row) const;
  int FrameCountForState(const QString& state) const;
};

// Decodes the spritesheet and geometry. This does base64 + image decode and is
// safe to call from a worker. Returns false only for an enabled pet whose
// payload is malformed; disabled pets return true with `enabled == false`.
bool DecodePetSprite(const QJsonObject& payload, PetSpriteInfo* info,
                     QString* error);

}  // namespace hermes::ui

#endif  // HERMES_UI_SRC_PET_SPRITE_H_
