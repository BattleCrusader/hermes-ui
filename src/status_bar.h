// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_STATUS_BAR_H_
#define HERMES_UI_STATUS_BAR_H_

#include <QString>
#include <QWidget>

class QTimer;

#include "src/theme.h"

namespace hermes::ui {

// 26px footer: warm-profile count, the measured switch time and the active
// gateway. The switch number is the spike's evidence line.
class StatusBar : public QWidget {
 public:
  explicit StatusBar(const Theme& theme, QWidget* parent = nullptr);

  void SetTheme(const Theme& theme);
  void SetState(const QString& left, const QString& center,
                const QString& right, bool healthy);
  // Work indicator: the status dot pulses and the left label gains a dot
  // trail while a turn is running.
  void SetBusy(bool busy);
  // One-shot message (e.g. "Generation complete · 12s") that overrides the
  // centre text for a few seconds.
  void ShowTransient(const QString& text, int milliseconds = 4000);
  // Context usage ("9.2k / 128k · 7%") lives in the footer, not the composer
  // toolbar, where it collided with the running/enter-to-send hint.
  void SetUsage(const QString& usage);
  QString Usage() const { return usage_; }

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  Theme theme_;
  QString left_;
  QString center_;
  QString right_;
  bool healthy_ = true;
  bool busy_ = false;
  QString transient_;
  QString usage_;
  QTimer* pulse_timer_ = nullptr;
  QTimer* transient_timer_ = nullptr;
  int pulse_ = 0;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_STATUS_BAR_H_
