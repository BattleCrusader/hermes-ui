// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_COMMAND_PALETTE_H_
#define HERMES_UI_COMMAND_PALETTE_H_

#include <QWidget>
#include <functional>
#include <vector>

#include "src/theme.h"

class QEvent;
class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;

namespace hermes::ui {

// One palette row. The palette owns only presentation; the action closure is
// supplied by MainWindow so the palette never reaches into the supervisor.
struct PaletteCommand {
  QString title;
  QString hint;
  QString group;
  std::function<void()> run;
};

// Cmd+K overlay: a filtered, keyboard-driven list of actions, profiles and
// sessions. The desktop app uses the same gesture; keeping the entries in a
// provider callback means the list is rebuilt from the live snapshot each time
// it opens instead of going stale between profiles.
class CommandPalette : public QWidget {
public:
  explicit CommandPalette(const Theme &theme, QWidget *parent = nullptr);

  void SetTheme(const Theme &theme);
  void SetCommands(std::vector<PaletteCommand> commands);
  void Open();
  // Opens with a query already typed (e.g. "model" from Cmd+Shift+M).
  void OpenWith(const QString &query);
  void Close();
  bool IsOpen() const { return isVisible(); }

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  void Filter();
  void MoveSelection(int delta);
  void RunCurrent();
  void LayoutCard();

  Theme theme_;
  QFrame *card_ = nullptr;
  QLineEdit *search_ = nullptr;
  QListWidget *list_ = nullptr;
  QLabel *empty_ = nullptr;
  std::vector<PaletteCommand> commands_;
};

} // namespace hermes::ui

#endif // HERMES_UI_COMMAND_PALETTE_H_
