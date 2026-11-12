// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_STATUS_BAR_H_
#define HERMES_UI_STATUS_BAR_H_

#include <QString>
#include <QWidget>

class QMouseEvent;
class QTimer;

#include "src/theme.h"

namespace hermes::ui {

// Human-readable turn duration: "7s", "2m 03s", "1h 02m 03s".
QString FormatWorkElapsed(qint64 elapsed_ms);

// 26px footer: warm-profile count, the measured switch time and the active
// gateway. The switch number is the spike's evidence line.
class StatusBar : public QWidget {
  Q_OBJECT

 public:
  explicit StatusBar(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetState(const QString& left, const QString& center,
                const QString& right, bool healthy);
  // Live work state. The dot pulses and the centre label becomes
  // "Working · 2m 03s" while a turn runs; the elapsed timer repaints once a
  // second so reasoning turns never look idle.
  void SetWorkState(bool working, qint64 started_ms, int queued = 0);
  // Compatibility wrapper for callers that only need the pulse.
  void SetBusy(bool busy);
  // Voice capture/transcription status. Empty hides it; "idle" is filtered by
  // the shell so a quiet gateway does not leave a permanent label.
  void SetActivity(const QString& text);
  QString WorkText() const;
  // The centre slot's resolved text: transient > activity > work > normal.
  QString CenterTextForTest() const;
  // One-shot message (e.g. "Generation complete · 12s") that overrides the
  // centre text for a few seconds.
  void ShowTransient(const QString& text, int milliseconds = 4000);
  // Context usage ("9.2k / 128k · 7%") lives in the footer, not the composer
  // toolbar, where it collided with the running/enter-to-send hint.
  void SetUsage(const QString& usage);
  QString Usage() const { return usage_; }

 signals:
  void QueuedReviewRequested();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;

 private:
  Theme theme_;
  QString left_;
  QString center_;
  QString right_;
  bool healthy_ = true;
  bool busy_ = false;
  bool working_ = false;
  qint64 work_started_ms_ = 0;
  int work_queued_ = 0;
  QString activity_;
  QString transient_;
  QString usage_;
  QTimer* pulse_timer_ = nullptr;
  QTimer* transient_timer_ = nullptr;
  QTimer* work_timer_ = nullptr;
  int pulse_ = 0;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_STATUS_BAR_H_
