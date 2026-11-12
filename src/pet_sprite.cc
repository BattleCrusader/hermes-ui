// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/pet_sprite.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>
#include <algorithm>

namespace hermes::ui {
namespace {

const QStringList& DefaultRows() {
  static const QStringList rows = {
      QStringLiteral("idle"),         QStringLiteral("running-right"),
      QStringLiteral("running-left"), QStringLiteral("waving"),
      QStringLiteral("jumping"),      QStringLiteral("failed"),
      QStringLiteral("waiting"),      QStringLiteral("running"),
      QStringLiteral("review")};
  return rows;
}

QString CanonicalState(const QString& state) {
  const QString s = state.toLower();
  if (s == QStringLiteral("wave") || s == QStringLiteral("waving")) {
    return QStringLiteral("waving");
  }
  if (s == QStringLiteral("jump") || s == QStringLiteral("jumping")) {
    return QStringLiteral("jumping");
  }
  if (s == QStringLiteral("run") || s == QStringLiteral("running") ||
      s == QStringLiteral("running-left") ||
      s == QStringLiteral("running-right")) {
    return QStringLiteral("running");
  }
  if (s == QStringLiteral("failed")) {
    return QStringLiteral("failed");
  }
  if (s == QStringLiteral("review")) {
    return QStringLiteral("review");
  }
  if (s == QStringLiteral("waiting")) {
    return QStringLiteral("waiting");
  }
  return QStringLiteral("idle");
}

int StateFrameFallback(const PetSpriteInfo& info, const QString& state) {
  const QString canonical = CanonicalState(state);
  const int direct = info.frames_by_state.value(canonical, 0);
  if (direct > 0) {
    return direct;
  }
  const int raw = info.frames_by_state.value(state, 0);
  return raw > 0 ? raw : 0;
}

}  // namespace

bool PetSpriteInfo::HasSheet() const {
  return enabled && !sheet.isNull() && frame_w > 0 && frame_h > 0;
}

QSize PetSpriteInfo::DrawSize() const {
  const double safe_scale = scale > 0.01 ? scale : 0.33;
  return QSize(std::max(1, qRound(frame_w * safe_scale)),
               std::max(1, qRound(frame_h * safe_scale)));
}

QSize PetSpriteInfo::WindowSize() const {
  const QSize draw = DrawSize();
  // Matches the desktop overlay proportions: room above for the speech bubble
  // and a small transparent margin for dragging.
  return QSize(std::max(220, draw.width() + 96),
               std::max(260, draw.height() + 116));
}

QString PetSpriteInfo::RowNameForState(const QString& state) const {
  const QString canonical = CanonicalState(state);
  const QStringList rows = state_rows.isEmpty() ? DefaultRows() : state_rows;
  const QStringList aliases =
      canonical == QStringLiteral("waving")
          ? QStringList{QStringLiteral("waving"), QStringLiteral("wave")}
      : canonical == QStringLiteral("jumping")
          ? QStringList{QStringLiteral("jumping"), QStringLiteral("jump")}
      : canonical == QStringLiteral("running")
          ? QStringList{QStringLiteral("running"), QStringLiteral("run"),
                        QStringLiteral("running-left"),
                        QStringLiteral("running-right")}
          : QStringList{canonical};
  for (const QString& alias : aliases) {
    if (rows.contains(alias)) {
      return alias;
    }
  }
  return rows.value(0, QStringLiteral("idle"));
}

int PetSpriteInfo::RowForState(const QString& state) const {
  const QStringList rows = state_rows.isEmpty() ? DefaultRows() : state_rows;
  const int index = rows.indexOf(RowNameForState(state));
  return index < 0 ? 0 : index;
}

int PetSpriteInfo::FrameCountForRow(int row) const {
  const QStringList rows = state_rows.isEmpty() ? DefaultRows() : state_rows;
  if (row < 0 || row >= rows.size()) {
    return FrameCountForState(QStringLiteral("idle"));
  }
  const QString name = rows.at(row);
  const int by_row = frames_by_row.value(name, 0);
  if (by_row > 0) {
    return by_row;
  }
  const int by_state = frames_by_state.value(name, 0);
  if (by_state > 0) {
    return by_state;
  }
  const int canonical = StateFrameFallback(*this, name);
  if (canonical > 0) {
    return canonical;
  }
  return std::max(1, frames_per_state);
}

int PetSpriteInfo::FrameCountForState(const QString& state) const {
  const int frames = StateFrameFallback(*this, state);
  if (frames > 0) {
    return frames;
  }
  const int idle = StateFrameFallback(*this, QStringLiteral("idle"));
  return idle > 0 ? idle : std::max(1, frames_per_state);
}

bool DecodePetSprite(const QJsonObject& payload, PetSpriteInfo* info,
                     QString* error) {
  if (info == nullptr) {
    if (error != nullptr) {
      *error = QStringLiteral("No pet sprite target");
    }
    return false;
  }
  *info = PetSpriteInfo();
  info->enabled = payload.value(QStringLiteral("enabled")).toBool(false);
  if (!info->enabled) {
    return true;
  }
  info->slug = payload.value(QStringLiteral("slug")).toString();
  info->display_name = payload.value(QStringLiteral("displayName")).toString();
  info->mime = payload.value(QStringLiteral("mime")).toString();
  info->revision =
      payload.value(QStringLiteral("spritesheetRevision")).toString();
  info->frame_w =
      std::max(1, payload.value(QStringLiteral("frameW")).toInt(192));
  info->frame_h =
      std::max(1, payload.value(QStringLiteral("frameH")).toInt(208));
  info->frames_per_state =
      std::max(1, payload.value(QStringLiteral("framesPerState")).toInt(6));
  info->loop_ms =
      std::max(80, payload.value(QStringLiteral("loopMs")).toInt(1100));
  info->scale = payload.value(QStringLiteral("scale")).toDouble(0.33);
  if (info->scale <= 0.01) {
    info->scale = 0.33;
  }
  const QJsonArray rows = payload.value(QStringLiteral("stateRows")).toArray();
  for (const QJsonValue& row : rows) {
    if (row.isString() && !row.toString().isEmpty()) {
      info->state_rows.push_back(row.toString());
    }
  }
  if (info->state_rows.isEmpty()) {
    info->state_rows = DefaultRows();
  }
  const QJsonObject frames_by_state =
      payload.value(QStringLiteral("framesByState")).toObject();
  for (auto it = frames_by_state.begin(); it != frames_by_state.end(); ++it) {
    const int count = it.value().toInt(0);
    if (count > 0) {
      info->frames_by_state.insert(it.key(), count);
    }
  }
  const QJsonObject frames_by_row =
      payload.value(QStringLiteral("framesByRow")).toObject();
  for (auto it = frames_by_row.begin(); it != frames_by_row.end(); ++it) {
    const int count = it.value().toInt(0);
    if (count > 0) {
      info->frames_by_row.insert(it.key(), count);
    }
  }
  const QString encoded =
      payload.value(QStringLiteral("spritesheetBase64")).toString();
  if (encoded.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Pet payload did not include a spritesheet");
    }
    return false;
  }
  const QByteArray bytes = QByteArray::fromBase64(encoded.toLatin1());
  if (!info->sheet.loadFromData(bytes)) {
    if (error != nullptr) {
      *error = QStringLiteral("Could not decode the pet spritesheet");
    }
    return false;
  }
  return true;
}

}  // namespace hermes::ui
