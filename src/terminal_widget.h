// Copyright 2026 The Hermes Qt Spike Authors.

#ifndef HERMES_UI_TERMINAL_WIDGET_H_
#define HERMES_UI_TERMINAL_WIDGET_H_

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QString>
#include <QStringConverter>
#include <QStringList>
#include <QVariant>
#include <QVector>
#include <QWidget>

#include "src/theme.h"

class QContextMenuEvent;
class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QScrollBar;
class QSocketNotifier;
class QTimer;
class QWheelEvent;

namespace hermes::ui {

// A small native VT100/xterm-compatible terminal. It owns a forkpty-backed
// shell, parses the common ANSI/CSI/OSC subset emitted by zsh, bash and CLI
// tools, and paints a selectable grid with the app's theme. This replaces the
// old read-only QPlainTextEdit command runner: keystrokes go straight to the
// pty, including control keys, arrows, paste and resize.
class TerminalWidget : public QWidget {
  Q_OBJECT

 public:
  explicit TerminalWidget(QWidget* parent = nullptr);
  ~TerminalWidget() override;

  void SetTheme(const Theme& theme);
  const Theme& theme() const { return theme_; }

  void SetWorkingDirectory(const QString& path);
  QString working_directory() const { return working_directory_; }

  // Starts a shell in a pty. Returns false if the pty or shell could not be
  // created; the error is reported through StartFailed.
  bool Start();
  void Stop();
  void Restart();
  bool IsRunning() const { return running_; }

  // Writes text to the child exactly as if it had been typed. Newlines are not
  // translated here; callers that mean "press return" pass "\r".
  void SendText(const QString& text);
  void RunCommandForTest(const QString& command);
  void ClearDisplay();

  // Selection/clipboard. CopySelection leaves the selection highlighted;
  // PasteClipboard converts text to terminal line endings and honours
  // bracketed-paste mode. SelectAll selects the visible screen.
  bool HasSelection() const;
  void ClearSelection();
  void SelectAll();
  void CopySelection();
  void PasteClipboard();

  // Test hooks. FeedForTest bypasses the pty and exercises the parser; the
  // accessors read the live screen without exposing parser internals.
  void SetShellForTest(const QString& shell) { shell_override_ = shell; }
  void FeedForTest(const QByteArray& bytes);
  QStringList ScreenLinesForTest() const;
  QStringList ScrollbackForTest() const;
  QString PlainTextForTest() const;
  QString SelectedText() const;
  QSize CellSizeForTest() const { return QSize(cell_width_, cell_height_); }
  int PaddingForTest() const { return padding_; }
  void SelectRangeForTest(int anchor_row, int anchor_column, int end_row,
                          int end_column);
  QMenu* CreateContextMenuForTest(QWidget* parent = nullptr);
  int CursorRowForTest() const;
  int CursorColumnForTest() const;
  QColor CellForegroundForTest(int row, int column) const;
  QColor CellBackgroundForTest(int row, int column) const;
  QString Title() const { return title_; }

 signals:
  void RunningChanged(bool running);
  void TitleChanged(const QString& title);
  void ProcessExited(int status);
  void StartFailed(const QString& error);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
  void focusInEvent(QFocusEvent* event) override;
  void focusOutEvent(QFocusEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

 private:
  struct Cell {
    QString text = QStringLiteral(" ");
    QColor foreground;
    QColor background;
    quint16 attributes = 0;
    bool continuation = false;
  };
  using Line = QVector<Cell>;

  struct SavedCursor {
    int row = 0;
    int column = 0;
    QColor foreground;
    QColor background;
    quint16 attributes = 0;
    bool wrap_pending = false;
  };

  struct Screen {
    int rows = 24;
    int columns = 80;
    QVector<Line> lines;
    int row = 0;
    int column = 0;
    int scroll_top = 0;
    int scroll_bottom = 23;
    QColor foreground;
    QColor background;
    quint16 attributes = 0;
    bool wrap_pending = false;
    bool cursor_visible = true;
    SavedCursor saved;
  };

  enum Attribute : quint16 {
    kBold = 1 << 0,
    kDim = 1 << 1,
    kItalic = 1 << 2,
    kUnderline = 1 << 3,
    kBlink = 1 << 4,
    kReverse = 1 << 5,
    kHidden = 1 << 6,
    kStrike = 1 << 7,
  };

  enum class ParseState {
    kNormal,
    kEscape,
    kCsi,
    kOsc,
    kOscEscape,
    kString,
    kStringEscape,
    kEscapeCharset,
  };

  // pty lifecycle.
  bool StartUnix();
  void ReadFromPty();
  void ReapChild(bool report_exit);
  void CloseMasterFd();
  void SendBytes(const QByteArray& bytes);
  void FlushPendingWrites();
  void ApplyWindowSize();
  void ResizeScreen(Screen* screen, int rows, int columns) const;

  // Screen model.
  Screen& CurrentScreen();
  const Screen& CurrentScreen() const;
  static Cell BlankCell();
  static Line BlankLine(int columns);
  Line& LineAt(Screen* screen, int row);
  const Line* VisibleLine(int viewport_row) const;
  void ResetScreen(Screen* screen);
  void ResetTerminal();
  void PutGlyph(const QString& glyph, int width);
  void AppendCombining(const QString& glyph);
  void CarriageReturn();
  void LineFeed();
  void ReverseIndex();
  void NextLine();
  void TabForward(int count);
  void Backspace();
  void ScrollUp(int count);
  void ScrollDown(int count);
  void EraseRange(Line* line, int start, int end) const;
  void EraseLine(int mode);
  void EraseDisplay(int mode);
  void InsertCharacters(int count);
  void DeleteCharacters(int count);
  void InsertLines(int count);
  void DeleteLines(int count);
  void SetCursor(int row, int column);
  void MoveCursor(int row_delta, int column_delta);
  void SaveCursor();
  void RestoreCursor();
  void EnterAlternateScreen(bool clear);
  void LeaveAlternateScreen();
  void ApplySgr(const QString& params);
  void SetForegroundIndex(int index);
  void SetBackgroundIndex(int index);
  void SetForegroundRgb(const QColor& color);
  void SetBackgroundRgb(const QColor& color);
  QColor ColorForIndex(int index) const;

  // Parser.
  void ProcessText(const QString& text);
  void ProcessEscape(QChar ch);
  void ProcessCsi(QChar final_char);
  void ProcessOsc();
  void ProcessControl(QChar ch);
  void ProcessPaste(const QString& text);
  void UpdateTitle(const QString& title);
  void ResetColorState();
  void UpdatePalette();

  // Rendering/input.
  void UpdateMetrics();
  void SyncScrollBar();
  void ScrollBy(int lines);
  void SendKeySequence(const QByteArray& bytes);
  bool IsSelected(int viewport_row, int column) const;
  QString LineText(const Line& line, int start = 0, int end = -1) const;
  QColor ResolvedForeground(const Cell& cell,
                            const QColor& default_color) const;
  QColor ResolvedBackground(const Cell& cell,
                            const QColor& default_color) const;

  Theme theme_;
  QFont font_;
  int cell_width_ = 8;
  int cell_height_ = 16;
  int padding_ = 8;
  QColor default_foreground_;
  QColor default_background_;
  QColor palette_[16];
  QScrollBar* scrollbar_ = nullptr;

  Screen primary_;
  Screen alternate_;
  Screen* active_ = &primary_;
  bool alternate_active_ = false;
  QVector<Line> scrollback_;
  int scroll_offset_ = 0;

  QString title_;
  QString working_directory_;
  QString shell_override_;
  int master_fd_ = -1;
  qint64 child_pid_ = -1;
  QSocketNotifier* read_notifier_ = nullptr;
  QSocketNotifier* write_notifier_ = nullptr;
  QByteArray pending_write_;
  QTimer* child_timer_ = nullptr;
  bool running_ = false;
  bool stopping_ = false;

  ParseState parse_state_ = ParseState::kNormal;
  QString sequence_;
  QString osc_sequence_;
  QStringDecoder decoder_{QStringDecoder::Utf8};

  bool app_cursor_keys_ = false;
  bool bracketed_paste_ = false;
  bool newline_mode_ = false;
  bool origin_mode_ = false;
  bool insert_mode_ = false;
  bool auto_wrap_ = true;
  bool cursor_blink_on_ = true;
  QTimer* cursor_blink_timer_ = nullptr;

  int selection_anchor_row_ = -1;
  int selection_anchor_column_ = -1;
  int selection_row_ = -1;
  int selection_column_ = -1;
  bool selecting_ = false;

  bool updating_scrollbar_ = false;
};

}  // namespace hermes::ui

#endif  // HERMES_UI_TERMINAL_WIDGET_H_
