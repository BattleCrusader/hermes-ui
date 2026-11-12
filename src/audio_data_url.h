// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_AUDIO_DATA_URL_H_
#define HERMES_UI_AUDIO_DATA_URL_H_

#include <QByteArray>
#include <QString>

namespace hermes::ui {

// Decodes the gateway's `data:audio/...;base64,...` speech payload. Returns
// false with a human-readable error for a malformed or empty data URL so the
// caller can surface the failure instead of playing silence.
bool DecodeAudioDataUrl(const QString &data_url, QByteArray *bytes,
                        QString *mime_type, QString *error);

// File suffix for the temporary playback file, since QMediaPlayer relies on
// the container extension to pick a backend.
QString AudioFileSuffixForMime(const QString &mime_type);

// Dictation uploads use the same data-URL shape the gateway decodes. The MIME
// type is derived from the picked recording's extension; an unknown extension
// returns an empty string so the caller can reject it before uploading.
QString AudioMimeForFilePath(const QString &path);
QString EncodeAudioDataUrl(const QByteArray &bytes, const QString &mime_type);

} // namespace hermes::ui

#endif // HERMES_UI_AUDIO_DATA_URL_H_
