// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/audio_data_url.h"

#include <QFileInfo>

namespace hermes::ui {

bool DecodeAudioDataUrl(const QString& data_url, QByteArray* bytes,
                        QString* mime_type, QString* error) {
  const QString payload = data_url.trimmed();
  if (!payload.startsWith(QStringLiteral("data:"))) {
    if (error != nullptr) {
      *error = QStringLiteral("Speech response was not a data URL.");
    }
    return false;
  }
  const int comma = payload.indexOf(QLatin1Char(','));
  if (comma <= 5) {
    if (error != nullptr) {
      *error = QStringLiteral("Speech response is missing its payload.");
    }
    return false;
  }
  const QString header = payload.mid(5, comma - 5);
  if (!header.endsWith(QStringLiteral(";base64"), Qt::CaseInsensitive)) {
    if (error != nullptr) {
      *error = QStringLiteral("Speech payload is not base64 encoded.");
    }
    return false;
  }
  const QString mime =
      header.left(header.size() - 7).section(QLatin1Char(';'), 0, 0).trimmed();
  if (!mime.startsWith(QStringLiteral("audio/"), Qt::CaseInsensitive)) {
    if (error != nullptr) {
      *error = QStringLiteral("Speech response is not an audio type.");
    }
    return false;
  }
  const QByteArray decoded =
      QByteArray::fromBase64(payload.mid(comma + 1).trimmed().toLatin1());
  if (decoded.isEmpty()) {
    if (error != nullptr) {
      *error = QStringLiteral("Speech response contained no audio.");
    }
    return false;
  }
  if (bytes != nullptr) {
    *bytes = decoded;
  }
  if (mime_type != nullptr) {
    *mime_type = mime.toLower();
  }
  return true;
}

QString AudioFileSuffixForMime(const QString& mime_type) {
  const QString mime = mime_type.trimmed().toLower();
  if (mime == QStringLiteral("audio/wav") ||
      mime == QStringLiteral("audio/x-wav") ||
      mime == QStringLiteral("audio/wave")) {
    return QStringLiteral(".wav");
  }
  if (mime == QStringLiteral("audio/ogg") ||
      mime == QStringLiteral("audio/opus")) {
    return QStringLiteral(".ogg");
  }
  if (mime == QStringLiteral("audio/flac")) {
    return QStringLiteral(".flac");
  }
  if (mime == QStringLiteral("audio/mp4") ||
      mime == QStringLiteral("audio/aac") ||
      mime == QStringLiteral("audio/m4a")) {
    return QStringLiteral(".m4a");
  }
  return QStringLiteral(".mp3");
}

QString AudioMimeForFilePath(const QString& path) {
  const QString suffix = QFileInfo(path).suffix().toLower();
  if (suffix == QStringLiteral("wav")) {
    return QStringLiteral("audio/wav");
  }
  if (suffix == QStringLiteral("ogg") || suffix == QStringLiteral("opus")) {
    return QStringLiteral("audio/ogg");
  }
  if (suffix == QStringLiteral("flac")) {
    return QStringLiteral("audio/flac");
  }
  if (suffix == QStringLiteral("m4a") || suffix == QStringLiteral("mp4") ||
      suffix == QStringLiteral("aac")) {
    return QStringLiteral("audio/mp4");
  }
  if (suffix == QStringLiteral("mp3")) {
    return QStringLiteral("audio/mpeg");
  }
  if (suffix == QStringLiteral("webm")) {
    return QStringLiteral("audio/webm");
  }
  return QString();
}

QString EncodeAudioDataUrl(const QByteArray& bytes, const QString& mime_type) {
  if (bytes.isEmpty() || mime_type.trimmed().isEmpty()) {
    return QString();
  }
  return QStringLiteral("data:%1;base64,%2")
      .arg(mime_type.trimmed(), QString::fromLatin1(bytes.toBase64()));
}

}  // namespace hermes::ui
