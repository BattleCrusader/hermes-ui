// Copyright 2026 The Hermes Qt Spike Authors.

#include "src/terminal_widget.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedPointer>
#include <QScrollBar>
#include <QSocketNotifier>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#if defined(Q_OS_MACOS)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace hermes::ui {
namespace {

constexpr int kScrollBarReserve = 12;
constexpr int kMaxScrollback = 5000;
constexpr int kDefaultColumns = 80;
constexpr int kDefaultRows = 24;

QByteArray ControlByteForKey(int key) {
  switch (key) {
    case Qt::Key_Space:
    case Qt::Key_At:
      return QByteArray(1, '\0');
    case Qt::Key_A:
      return QByteArray(1, '\x01');
    case Qt::Key_B:
      return QByteArray(1, '\x02');
    case Qt::Key_C:
      return QByteArray(1, '\x03');
    case Qt::Key_D:
      return QByteArray(1, '\x04');
    case Qt::Key_E:
      return QByteArray(1, '\x05');
    case Qt::Key_F:
      return QByteArray(1, '\x06');
    case Qt::Key_G:
      return QByteArray(1, '\x07');
    case Qt::Key_H:
      return QByteArray(1, '\x08');
    case Qt::Key_I:
      return QByteArray(1, '\x09');
    case Qt::Key_J:
      return QByteArray(1, '\x0a');
    case Qt::Key_K:
      return QByteArray(1, '\x0b');
    case Qt::Key_L:
      return QByteArray(1, '\x0c');
    case Qt::Key_M:
      return QByteArray(1, '\x0d');
    case Qt::Key_N:
      return QByteArray(1, '\x0e');
    case Qt::Key_O:
      return QByteArray(1, '\x0f');
    case Qt::Key_P:
      return QByteArray(1, '\x10');
    case Qt::Key_Q:
      return QByteArray(1, '\x11');
    case Qt::Key_R:
      return QByteArray(1, '\x12');
    case Qt::Key_S:
      return QByteArray(1, '\x13');
    case Qt::Key_T:
      return QByteArray(1, '\x14');
    case Qt::Key_U:
      return QByteArray(1, '\x15');
    case Qt::Key_V:
      return QByteArray(1, '\x16');
    case Qt::Key_W:
      return QByteArray(1, '\x17');
    case Qt::Key_X:
      return QByteArray(1, '\x18');
    case Qt::Key_Y:
      return QByteArray(1, '\x19');
    case Qt::Key_Z:
      return QByteArray(1, '\x1a');
    case Qt::Key_BracketLeft:
      return QByteArray(1, '\x1b');
    case Qt::Key_Backslash:
      return QByteArray(1, '\x1c');
    case Qt::Key_BracketRight:
      return QByteArray(1, '\x1d');
    case Qt::Key_AsciiCircum:
      return QByteArray(1, '\x1e');
    case Qt::Key_Underscore:
      return QByteArray(1, '\x1f');
    case Qt::Key_Question:
      return QByteArray(1, '\x7f');
    default:
      return {};
  }
}

int CodePointWidth(char32_t codepoint) {
  if (codepoint == 0) {
    return 0;
  }
  if (codepoint < 0x20 || (codepoint >= 0x7f && codepoint < 0xa0)) {
    return 0;
  }
  if (QChar::category(static_cast<uint>(codepoint)) == QChar::Mark_NonSpacing ||
      QChar::category(static_cast<uint>(codepoint)) ==
          QChar::Mark_SpacingCombining ||
      QChar::category(static_cast<uint>(codepoint)) == QChar::Mark_Enclosing) {
    return 0;
  }
  if ((codepoint >= 0x1100 && codepoint <= 0x115f) || codepoint == 0x2329 ||
      codepoint == 0x232a || (codepoint >= 0x2e80 && codepoint <= 0xa4cf) ||
      (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
      (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
      (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
      (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
      (codepoint >= 0xff00 && codepoint <= 0xff60) ||
      (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
      (codepoint >= 0x1f300 && codepoint <= 0x1faff) ||
      (codepoint >= 0x20000 && codepoint <= 0x3fffd)) {
    return 2;
  }
  return 1;
}

QString TrimLine(QString text) {
  while (!text.isEmpty() && text.endsWith(QLatin1Char(' '))) {
    text.chop(1);
  }
  return text;
}

}  // namespace

TerminalWidget::TerminalWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("rightRailTerminalOutput"));
  setAttribute(Qt::WA_OpaquePaintEvent);
  setAttribute(Qt::WA_InputMethodEnabled);
  setFocusPolicy(Qt::StrongFocus);
  setCursor(Qt::IBeamCursor);
  setMouseTracking(true);
  setMinimumHeight(180);

  scrollbar_ = new QScrollBar(Qt::Vertical, this);
  scrollbar_->setObjectName(QStringLiteral("terminalScrollBar"));
  scrollbar_->setFocusPolicy(Qt::NoFocus);
  scrollbar_->setSingleStep(1);
  scrollbar_->hide();
  QObject::connect(scrollbar_, &QScrollBar::valueChanged, this, [this](int) {
    if (updating_scrollbar_) {
      return;
    }
    const int max_value = scrollback_.size();
    scroll_offset_ = std::clamp(max_value - scrollbar_->value(), 0, max_value);
    ClearSelection();
    update();
  });

  cursor_blink_timer_ = new QTimer(this);
  cursor_blink_timer_->setInterval(530);
  QObject::connect(cursor_blink_timer_, &QTimer::timeout, this, [this]() {
    if (!hasFocus()) {
      return;
    }
    cursor_blink_on_ = !cursor_blink_on_;
    update();
  });

  primary_.rows = kDefaultRows;
  primary_.columns = kDefaultColumns;
  primary_.scroll_bottom = kDefaultRows - 1;
  ResetScreen(&primary_);
  alternate_.rows = kDefaultRows;
  alternate_.columns = kDefaultColumns;
  alternate_.scroll_bottom = kDefaultRows - 1;
  ResetScreen(&alternate_);
  SetTheme(Theme::Dark());
}

TerminalWidget::~TerminalWidget() { Stop(); }

void TerminalWidget::SetTheme(const Theme& theme) {
  theme_ = theme;
  UpdatePalette();
  UpdateMetrics();
  if (scrollbar_ != nullptr) {
    const QString handle = theme_.stroke_strong.name();
    const QString track = theme_.code_background.name();
    const QString hover = theme_.text_tertiary.name();
    scrollbar_->setStyleSheet(
        QStringLiteral(
            "QScrollBar:vertical { background: %1; width: 10px; margin: 0; "
            "border: 0; }"
            "QScrollBar::handle:vertical { background: %2; min-height: 24px; "
            "border-radius: 4px; margin: 1px; }"
            "QScrollBar::handle:vertical:hover { background: %3; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { "
            "height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { "
            "background: transparent; }")
            .arg(track, handle, hover));
  }
  if (width() > 0 && height() > 0) {
    ApplyWindowSize();
  }
  SyncScrollBar();
  update();
}

void TerminalWidget::SetWorkingDirectory(const QString& path) {
  const QString resolved = path.trimmed();
  if (resolved.isEmpty()) {
    working_directory_ = QDir::homePath();
  } else {
    working_directory_ = QFileInfo(resolved).absoluteFilePath();
  }
}

void TerminalWidget::UpdatePalette() {
  default_foreground_ = theme_.code_text;
  default_background_ = theme_.code_background;
  const QColor background = default_background_;
  const QColor foreground = default_foreground_;
  const QColor muted = Mix(background, foreground, 0.78);
  const QColor bright_muted = Mix(background, foreground, 0.88);
  palette_[0] = muted;
  palette_[1] = theme_.danger;
  palette_[2] = theme_.success;
  palette_[3] = theme_.warning;
  palette_[4] = theme_.accent;
  palette_[5] = Mix(theme_.accent, theme_.code_keyword, 0.55);
  palette_[6] = theme_.code_string;
  palette_[7] = Mix(background, foreground, 0.36);
  palette_[8] = Mix(muted, foreground, 0.35);
  palette_[9] = Mix(theme_.danger, foreground, 0.30);
  palette_[10] = Mix(theme_.success, foreground, 0.30);
  palette_[11] = Mix(theme_.warning, foreground, 0.25);
  palette_[12] = Mix(theme_.accent, foreground, 0.30);
  palette_[13] = Mix(palette_[5], foreground, 0.30);
  palette_[14] = Mix(theme_.code_string, foreground, 0.30);
  palette_[15] = bright_muted;
  primary_.foreground = QColor();
  primary_.background = QColor();
  alternate_.foreground = QColor();
  alternate_.background = QColor();
}

void TerminalWidget::UpdateMetrics() {
  font_ = theme_.MonoFont(12, QFont::Normal);
  const QFontMetricsF metrics(font_);
  cell_width_ = std::max(
      1, static_cast<int>(
             std::ceil(metrics.horizontalAdvance(QStringLiteral("M")))));
  cell_height_ = std::max(1, static_cast<int>(std::ceil(metrics.height())));
}

bool TerminalWidget::Start() {
#if defined(Q_OS_UNIX)
  return StartUnix();
#else
  emit StartFailed(QStringLiteral("Terminal sessions are not supported."));
  return false;
#endif
}

void TerminalWidget::Stop() {
#if defined(Q_OS_UNIX)
  stopping_ = true;
  CloseMasterFd();
  if (child_pid_ > 0) {
    ::kill(static_cast<pid_t>(child_pid_), SIGHUP);
    int status = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
      const pid_t result =
          ::waitpid(static_cast<pid_t>(child_pid_), &status, WNOHANG);
      if (result == child_pid_ || result < 0) {
        child_pid_ = -1;
        break;
      }
      QThread::msleep(5);
    }
    if (child_pid_ > 0) {
      ::kill(static_cast<pid_t>(child_pid_), SIGKILL);
      ::waitpid(static_cast<pid_t>(child_pid_), &status, 0);
      child_pid_ = -1;
    }
  }
  if (running_) {
    running_ = false;
    emit RunningChanged(false);
  }
  stopping_ = false;
#endif
}

void TerminalWidget::Restart() {
  Stop();
  ClearDisplay();
  Start();
}

#if defined(Q_OS_UNIX)
bool TerminalWidget::StartUnix() {
  if (running_) {
    return true;
  }
  ApplyWindowSize();
  QByteArray shell = shell_override_.toLocal8Bit();
  if (shell.isEmpty()) {
    shell = qgetenv("HERMES_UI_SHELL");
  }
  if (shell.isEmpty()) {
    shell = qgetenv("SHELL");
  }
  if (shell.isEmpty()) {
#if defined(Q_OS_MACOS)
    shell = "/bin/zsh";
#else
    shell = "/bin/bash";
#endif
  }
  const QByteArray cwd =
      (QFileInfo(working_directory_).isDir() ? working_directory_
                                             : QDir::homePath())
          .toLocal8Bit();
  struct winsize size{static_cast<uint16_t>(primary_.rows),
                      static_cast<uint16_t>(primary_.columns), 0, 0};
  int master = -1;
  const pid_t pid = ::forkpty(&master, nullptr, nullptr, &size);
  if (pid == 0) {
    ::setenv("TERM", "xterm-256color", 1);
    ::setenv("COLORTERM", "truecolor", 1);
    ::setenv("TERM_PROGRAM", "hermes-ui", 1);
    ::setenv("TERM_PROGRAM_VERSION", "1.0", 1);
    ::unsetenv("NO_COLOR");
    ::unsetenv("CLICOLOR");
    if (!cwd.isEmpty()) {
      ::chdir(cwd.constData());
    }
    ::execl(shell.constData(), shell.constData(), "-l",
            static_cast<char*>(nullptr));
    ::_exit(127);
  }
  if (pid < 0) {
    emit StartFailed(QStringLiteral("Could not start a shell: %1")
                         .arg(QString::fromLocal8Bit(std::strerror(errno))));
    return false;
  }
  ::signal(SIGPIPE, SIG_IGN);
  const int flags = ::fcntl(master, F_GETFL, 0);
  if (flags >= 0) {
    ::fcntl(master, F_SETFL, flags | O_NONBLOCK);
  }
  master_fd_ = master;
  child_pid_ = pid;
  running_ = true;
  read_notifier_ = new QSocketNotifier(master_fd_, QSocketNotifier::Read, this);
  QObject::connect(
      read_notifier_, &QSocketNotifier::activated, this,
      [this](QSocketDescriptor, QSocketNotifier::Type) { ReadFromPty(); });
  child_timer_ = new QTimer(this);
  child_timer_->setInterval(500);
  QObject::connect(child_timer_, &QTimer::timeout, this,
                   [this]() { ReapChild(true); });
  child_timer_->start();
  emit RunningChanged(true);
  return true;
}
#else
bool TerminalWidget::StartUnix() {
  emit StartFailed(QStringLiteral("Terminal sessions are not supported."));
  return false;
}
#endif

void TerminalWidget::CloseMasterFd() {
#if defined(Q_OS_UNIX)
  if (read_notifier_ != nullptr) {
    read_notifier_->disconnect();
    read_notifier_->deleteLater();
    read_notifier_ = nullptr;
  }
  if (write_notifier_ != nullptr) {
    write_notifier_->disconnect();
    write_notifier_->deleteLater();
    write_notifier_ = nullptr;
  }
  pending_write_.clear();
  if (master_fd_ >= 0) {
    ::close(master_fd_);
    master_fd_ = -1;
  }
  if (child_timer_ != nullptr) {
    child_timer_->stop();
    child_timer_->deleteLater();
    child_timer_ = nullptr;
  }
#endif
}

void TerminalWidget::ReadFromPty() {
#if defined(Q_OS_UNIX)
  if (master_fd_ < 0) {
    return;
  }
  bool saw_output = false;
  while (true) {
    char buffer[65536];
    const ssize_t read_size = ::read(master_fd_, buffer, sizeof(buffer));
    if (read_size > 0) {
      saw_output = true;
      ProcessText(
          decoder_.decode(QByteArray(buffer, static_cast<int>(read_size))));
      continue;
    }
    if (read_size == 0) {
      ReapChild(true);
      break;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    ReapChild(true);
    break;
  }
  if (saw_output) {
    cursor_blink_on_ = true;
    SyncScrollBar();
    update();
  }
#endif
}

void TerminalWidget::ReapChild(bool report_exit) {
#if defined(Q_OS_UNIX)
  if (child_pid_ <= 0) {
    return;
  }
  int status = 0;
  const pid_t result =
      ::waitpid(static_cast<pid_t>(child_pid_), &status, WNOHANG);
  if (result == 0) {
    return;
  }
  const qint64 exited_pid = child_pid_;
  child_pid_ = -1;
  CloseMasterFd();
  if (running_) {
    running_ = false;
    emit RunningChanged(false);
  }
  if (report_exit && !stopping_) {
    ProcessText(QStringLiteral("\r\n[process exited]\r\n"));
    SyncScrollBar();
    update();
  }
  if (exited_pid > 0) {
    emit ProcessExited(status);
  }
#else
  Q_UNUSED(report_exit);
#endif
}

void TerminalWidget::SendBytes(const QByteArray& bytes) {
#if defined(Q_OS_UNIX)
  if (master_fd_ < 0) {
    return;
  }
  pending_write_.append(bytes);
  FlushPendingWrites();
#else
  Q_UNUSED(bytes);
#endif
}

void TerminalWidget::FlushPendingWrites() {
#if defined(Q_OS_UNIX)
  if (master_fd_ < 0) {
    pending_write_.clear();
    return;
  }
  while (!pending_write_.isEmpty()) {
    const ssize_t written =
        ::write(master_fd_, pending_write_.constData(), pending_write_.size());
    if (written > 0) {
      pending_write_.remove(0, static_cast<int>(written));
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      if (write_notifier_ == nullptr) {
        write_notifier_ =
            new QSocketNotifier(master_fd_, QSocketNotifier::Write, this);
        QObject::connect(write_notifier_, &QSocketNotifier::activated, this,
                         [this](QSocketDescriptor, QSocketNotifier::Type) {
                           FlushPendingWrites();
                         });
      }
      write_notifier_->setEnabled(true);
      return;
    }
    pending_write_.clear();
    return;
  }
  if (write_notifier_ != nullptr) {
    write_notifier_->setEnabled(false);
  }
#endif
}

void TerminalWidget::SendText(const QString& text) {
  SendBytes(text.toUtf8());
  cursor_blink_on_ = true;
  if (cursor_blink_timer_ != nullptr && hasFocus()) {
    cursor_blink_timer_->start();
  }
}

void TerminalWidget::RunCommandForTest(const QString& command) {
  if (!running_) {
    Start();
  }
  SendText(command + QStringLiteral("\r"));
}

void TerminalWidget::FeedForTest(const QByteArray& bytes) {
  ProcessText(decoder_.decode(bytes));
  SyncScrollBar();
  update();
}

void TerminalWidget::ClearDisplay() {
  if (alternate_active_) {
    ResetScreen(&alternate_);
  } else {
    ResetScreen(&primary_);
  }
  scrollback_.clear();
  scroll_offset_ = 0;
  ClearSelection();
  SyncScrollBar();
  update();
}

void TerminalWidget::ProcessText(const QString& text) {
  for (int index = 0; index < text.size();) {
    char32_t codepoint = text.at(index).unicode();
    index += 1;
    if (QChar::isHighSurrogate(text.at(index - 1).unicode()) &&
        index < text.size() &&
        QChar::isLowSurrogate(text.at(index).unicode())) {
      codepoint = QChar::surrogateToUcs4(text.at(index - 1), text.at(index));
      index += 1;
    }
    switch (parse_state_) {
      case ParseState::kNormal:
        if (codepoint == 0x1b) {
          parse_state_ = ParseState::kEscape;
        } else if (codepoint == 0x9b) {
          sequence_.clear();
          parse_state_ = ParseState::kCsi;
        } else if (codepoint < 0x20 || codepoint == 0x7f) {
          ProcessControl(static_cast<QChar>(codepoint));
        } else {
          const QString glyph = QString::fromUcs4(&codepoint, 1);
          PutGlyph(glyph, CodePointWidth(codepoint));
        }
        break;
      case ParseState::kEscape:
        if (codepoint < 0x80) {
          ProcessEscape(static_cast<QChar>(codepoint));
        } else {
          parse_state_ = ParseState::kNormal;
        }
        break;
      case ParseState::kCsi:
        if (codepoint >= 0x40 && codepoint <= 0x7e) {
          ProcessCsi(static_cast<QChar>(codepoint));
        } else if (codepoint == 0x1b) {
          parse_state_ = ParseState::kEscape;
        } else if (sequence_.size() < 128) {
          sequence_.append(QString::fromUcs4(&codepoint, 1));
        }
        break;
      case ParseState::kOsc:
        if (codepoint == 0x07) {
          ProcessOsc();
        } else if (codepoint == 0x1b) {
          parse_state_ = ParseState::kOscEscape;
        } else if (codepoint >= 0x20 && osc_sequence_.size() < 4096) {
          osc_sequence_.append(QString::fromUcs4(&codepoint, 1));
        }
        break;
      case ParseState::kOscEscape:
        if (codepoint == '\\') {
          ProcessOsc();
        } else {
          osc_sequence_.append(QChar(0x1b));
          if (codepoint >= 0x20) {
            osc_sequence_.append(QString::fromUcs4(&codepoint, 1));
          }
          parse_state_ = ParseState::kOsc;
        }
        break;
      case ParseState::kString:
        if (codepoint == 0x1b) {
          parse_state_ = ParseState::kStringEscape;
        }
        break;
      case ParseState::kStringEscape:
        parse_state_ =
            codepoint == '\\' ? ParseState::kNormal : ParseState::kString;
        break;
      case ParseState::kEscapeCharset:
        parse_state_ = ParseState::kNormal;
        break;
    }
  }
}

void TerminalWidget::ProcessControl(QChar ch) {
  switch (ch.unicode()) {
    case 0x07:
      if (hasFocus()) {
        QApplication::beep();
      }
      break;
    case 0x08:
      Backspace();
      break;
    case 0x09:
      TabForward(1);
      break;
    case 0x0a:
      if (newline_mode_) {
        CarriageReturn();
      }
      LineFeed();
      break;
    case 0x0d:
      CarriageReturn();
      break;
    default:
      break;
  }
}

void TerminalWidget::ProcessEscape(QChar ch) {
  parse_state_ = ParseState::kNormal;
  switch (ch.unicode()) {
    case '[':
      sequence_.clear();
      parse_state_ = ParseState::kCsi;
      break;
    case ']':
      osc_sequence_.clear();
      parse_state_ = ParseState::kOsc;
      break;
    case 'P':
    case '^':
    case '_':
      parse_state_ = ParseState::kString;
      break;
    case '(':
    case ')':
    case '*':
    case '+':
      parse_state_ = ParseState::kEscapeCharset;
      break;
    case '7':
      SaveCursor();
      break;
    case '8':
      RestoreCursor();
      break;
    case 'D':
      LineFeed();
      break;
    case 'E':
      NextLine();
      break;
    case 'M':
      ReverseIndex();
      break;
    case 'c':
      ResetTerminal();
      break;
    case '=':
      app_cursor_keys_ = true;
      break;
    case '>':
      app_cursor_keys_ = false;
      break;
    default:
      break;
  }
}

void TerminalWidget::ProcessCsi(QChar final_char) {
  QString body = sequence_;
  sequence_.clear();
  parse_state_ = ParseState::kNormal;
  QChar private_prefix;
  if (!body.isEmpty() && QStringLiteral("?><!=").contains(body.front())) {
    private_prefix = body.front();
    body.remove(0, 1);
  }
  const int space = body.indexOf(QLatin1Char(' '));
  if (space >= 0) {
    body.truncate(space);
  }
  QVector<int> values;
  if (!body.isEmpty()) {
    const QStringList parts = body.split(QLatin1Char(';'), Qt::KeepEmptyParts);
    for (const QString& part : parts) {
      QString number = part;
      const int colon = number.indexOf(QLatin1Char(':'));
      if (colon >= 0) {
        number.truncate(colon);
      }
      bool ok = false;
      const int value = number.toInt(&ok);
      values.push_back(ok ? value : 0);
    }
  }
  const auto value_at = [&values](int index, int fallback) {
    if (index >= values.size() || values[index] == 0) {
      return fallback;
    }
    return values[index];
  };

  switch (final_char.unicode()) {
    case '@':
      InsertCharacters(value_at(0, 1));
      break;
    case 'A':
      MoveCursor(-value_at(0, 1), 0);
      break;
    case 'B':
      MoveCursor(value_at(0, 1), 0);
      break;
    case 'C':
    case 'a':
      MoveCursor(0, value_at(0, 1));
      break;
    case 'D':
      MoveCursor(0, -value_at(0, 1));
      break;
    case 'E':
      MoveCursor(value_at(0, 1), 0);
      CarriageReturn();
      break;
    case 'F':
      MoveCursor(-value_at(0, 1), 0);
      CarriageReturn();
      break;
    case 'G':
    case '`':
      SetCursor(CurrentScreen().row, value_at(0, 1) - 1);
      break;
    case 'H':
    case 'f': {
      const int row =
          value_at(0, 1) - 1 + (origin_mode_ ? CurrentScreen().scroll_top : 0);
      const int column = value_at(1, 1) - 1;
      SetCursor(row, column);
      break;
    }
    case 'I':
      TabForward(value_at(0, 1));
      break;
    case 'J':
      EraseDisplay(value_at(0, 0));
      break;
    case 'K':
      EraseLine(value_at(0, 0));
      break;
    case 'L':
      InsertLines(value_at(0, 1));
      break;
    case 'M':
      DeleteLines(value_at(0, 1));
      break;
    case 'P':
      DeleteCharacters(value_at(0, 1));
      break;
    case 'S':
      ScrollUp(value_at(0, 1));
      break;
    case 'T':
      ScrollDown(value_at(0, 1));
      break;
    case 'X':
      EraseRange(&LineAt(&CurrentScreen(), CurrentScreen().row),
                 CurrentScreen().column,
                 CurrentScreen().column + value_at(0, 1));
      break;
    case 'Z':
      TabForward(-value_at(0, 1));
      break;
    case 'c':
      if (private_prefix.isNull()) {
        SendBytes("\x1b[?62;22c");
      }
      break;
    case 'd':
      SetCursor(value_at(0, 1) - 1, CurrentScreen().column);
      break;
    case 'e':
      MoveCursor(value_at(0, 1), 0);
      break;
    case 'h':
    case 'l': {
      const bool enabled = final_char == QLatin1Char('h');
      for (const int mode : values) {
        if (!private_prefix.isNull()) {
          if (mode == 1) {
            app_cursor_keys_ = enabled;
          } else if (mode == 6) {
            origin_mode_ = enabled;
            SetCursor(0, 0);
          } else if (mode == 7) {
            auto_wrap_ = enabled;
          } else if (mode == 25) {
            CurrentScreen().cursor_visible = enabled;
          } else if (mode == 47 || mode == 1047) {
            if (enabled) {
              EnterAlternateScreen(false);
            } else {
              LeaveAlternateScreen();
            }
          } else if (mode == 1049) {
            if (enabled) {
              EnterAlternateScreen(true);
            } else {
              LeaveAlternateScreen();
            }
          } else if (mode == 2004) {
            bracketed_paste_ = enabled;
          }
        } else if (mode == 4) {
          insert_mode_ = enabled;
        } else if (mode == 20) {
          newline_mode_ = enabled;
        }
      }
      break;
    }
    case 'm':
      ApplySgr(body);
      break;
    case 'n':
      if (value_at(0, 0) == 6) {
        SendBytes(QStringLiteral("\x1b[%1;%2R")
                      .arg(CurrentScreen().row + 1)
                      .arg(CurrentScreen().column + 1)
                      .toUtf8());
      } else if (value_at(0, 0) == 5) {
        SendBytes("\x1b[0n");
      }
      break;
    case 'r': {
      const int top = value_at(0, 1) - 1;
      const int bottom = value_at(1, CurrentScreen().rows) - 1;
      if (top >= 0 && bottom < CurrentScreen().rows && top < bottom) {
        CurrentScreen().scroll_top = top;
        CurrentScreen().scroll_bottom = bottom;
        SetCursor(origin_mode_ ? top : 0, 0);
      }
      break;
    }
    case 's':
      SaveCursor();
      break;
    case 'u':
      RestoreCursor();
      break;
    default:
      break;
  }
}

void TerminalWidget::ProcessOsc() {
  parse_state_ = ParseState::kNormal;
  const QString value = osc_sequence_;
  osc_sequence_.clear();
  const int separator = value.indexOf(QLatin1Char(';'));
  if (separator < 0) {
    return;
  }
  const QString command = value.left(separator);
  if (command == QStringLiteral("0") || command == QStringLiteral("2")) {
    UpdateTitle(value.mid(separator + 1));
  }
}

void TerminalWidget::UpdateTitle(const QString& title) {
  if (title == title_) {
    return;
  }
  title_ = title;
  emit TitleChanged(title_);
}

void TerminalWidget::PutGlyph(const QString& glyph, int width) {
  if (width <= 0) {
    AppendCombining(glyph);
    return;
  }
  Screen& screen = CurrentScreen();
  if (screen.wrap_pending) {
    if (auto_wrap_) {
      screen.column = 0;
      LineFeed();
    } else {
      screen.wrap_pending = false;
    }
  }
  if (screen.column + width > screen.columns) {
    if (auto_wrap_) {
      screen.column = 0;
      LineFeed();
    } else {
      screen.column = std::max(0, screen.columns - width);
    }
  }
  Line& line = LineAt(&screen, screen.row);
  if (insert_mode_ && width == 1) {
    line.insert(screen.column, BlankCell());
    line.resize(screen.columns);
  }
  Cell cell;
  cell.text = glyph;
  cell.foreground = screen.foreground;
  cell.background = screen.background;
  cell.attributes = screen.attributes;
  line[screen.column] = cell;
  if (width == 2 && screen.column + 1 < line.size()) {
    Cell continuation = BlankCell();
    continuation.continuation = true;
    continuation.background = screen.background;
    line[screen.column + 1] = continuation;
  }
  screen.column += width;
  if (screen.column >= screen.columns) {
    screen.column = screen.columns - 1;
    screen.wrap_pending = auto_wrap_;
  } else {
    screen.wrap_pending = false;
  }
}

void TerminalWidget::AppendCombining(const QString& glyph) {
  Screen& screen = CurrentScreen();
  if (screen.wrap_pending) {
    Line& line = LineAt(&screen, screen.row);
    if (screen.column >= 0 && screen.column < line.size()) {
      line[screen.column].text += glyph;
    }
    return;
  }
  if (screen.column > 0) {
    Line& line = LineAt(&screen, screen.row);
    if (screen.column - 1 < line.size()) {
      line[screen.column - 1].text += glyph;
    }
    return;
  }
  if (screen.row > 0) {
    Line& line = LineAt(&screen, screen.row - 1);
    if (!line.isEmpty()) {
      line.back().text += glyph;
    }
  }
}

void TerminalWidget::CarriageReturn() {
  CurrentScreen().column = 0;
  CurrentScreen().wrap_pending = false;
}

void TerminalWidget::LineFeed() {
  Screen& screen = CurrentScreen();
  screen.wrap_pending = false;
  if (screen.row == screen.scroll_bottom) {
    ScrollUp(1);
  } else if (screen.row < screen.rows - 1) {
    ++screen.row;
  }
}

void TerminalWidget::ReverseIndex() {
  Screen& screen = CurrentScreen();
  screen.wrap_pending = false;
  if (screen.row == screen.scroll_top) {
    ScrollDown(1);
  } else if (screen.row > 0) {
    --screen.row;
  }
}

void TerminalWidget::NextLine() {
  LineFeed();
  CarriageReturn();
}

void TerminalWidget::TabForward(int count) {
  Screen& screen = CurrentScreen();
  if (count == 0) {
    return;
  }
  const int direction = count > 0 ? 1 : -1;
  for (int step = 0; step < std::abs(count); ++step) {
    if (direction > 0) {
      int next = ((screen.column / 8) + 1) * 8;
      if (next >= screen.columns) {
        next = screen.columns - 1;
      }
      screen.column = next;
    } else {
      int previous = ((std::max(0, screen.column - 1)) / 8) * 8;
      screen.column = std::clamp(previous, 0, screen.columns - 1);
    }
  }
  screen.wrap_pending = false;
}

void TerminalWidget::Backspace() {
  Screen& screen = CurrentScreen();
  if (screen.wrap_pending) {
    screen.wrap_pending = false;
    return;
  }
  if (screen.column > 0) {
    --screen.column;
  }
}

void TerminalWidget::ScrollUp(int count) {
  Screen& screen = CurrentScreen();
  const int amount =
      std::clamp(count, 1, screen.scroll_bottom - screen.scroll_top + 1);
  for (int line = 0; line < amount; ++line) {
    if (!alternate_active_ && screen.scroll_top == 0) {
      scrollback_.push_back(screen.lines.front());
      while (scrollback_.size() > kMaxScrollback) {
        scrollback_.removeFirst();
      }
    }
    for (int row = screen.scroll_top; row < screen.scroll_bottom; ++row) {
      screen.lines[row] = screen.lines[row + 1];
    }
    screen.lines[screen.scroll_bottom] = BlankLine(screen.columns);
  }
  screen.wrap_pending = false;
}

void TerminalWidget::ScrollDown(int count) {
  Screen& screen = CurrentScreen();
  const int amount =
      std::clamp(count, 1, screen.scroll_bottom - screen.scroll_top + 1);
  for (int line = 0; line < amount; ++line) {
    for (int row = screen.scroll_bottom; row > screen.scroll_top; --row) {
      screen.lines[row] = screen.lines[row - 1];
    }
    screen.lines[screen.scroll_top] = BlankLine(screen.columns);
  }
  screen.wrap_pending = false;
}

void TerminalWidget::EraseRange(Line* line, int start, int end) const {
  if (line == nullptr) {
    return;
  }
  const int line_size = static_cast<int>(line->size());
  const int from = std::clamp(start, 0, line_size);
  const int to = std::clamp(end, 0, line_size);
  for (int column = from; column < to; ++column) {
    Cell cell = BlankCell();
    cell.background = CurrentScreen().background;
    (*line)[column] = cell;
  }
}

void TerminalWidget::EraseLine(int mode) {
  Screen& screen = CurrentScreen();
  Line& line = LineAt(&screen, screen.row);
  if (mode == 0) {
    EraseRange(&line, screen.column, screen.columns);
  } else if (mode == 1) {
    EraseRange(&line, 0, screen.column + 1);
  } else if (mode == 2) {
    EraseRange(&line, 0, screen.columns);
  }
  screen.wrap_pending = false;
}

void TerminalWidget::EraseDisplay(int mode) {
  Screen& screen = CurrentScreen();
  if (mode == 0) {
    EraseRange(&LineAt(&screen, screen.row), screen.column, screen.columns);
    for (int row = screen.row + 1; row < screen.rows; ++row) {
      EraseRange(&LineAt(&screen, row), 0, screen.columns);
    }
  } else if (mode == 1) {
    for (int row = 0; row < screen.row; ++row) {
      EraseRange(&LineAt(&screen, row), 0, screen.columns);
    }
    EraseRange(&LineAt(&screen, screen.row), 0, screen.column + 1);
  } else if (mode == 2) {
    for (int row = 0; row < screen.rows; ++row) {
      EraseRange(&LineAt(&screen, row), 0, screen.columns);
    }
  } else if (mode == 3) {
    scrollback_.clear();
    scroll_offset_ = 0;
  }
}

void TerminalWidget::InsertCharacters(int count) {
  Screen& screen = CurrentScreen();
  Line& line = LineAt(&screen, screen.row);
  const int amount = std::clamp(count, 0, screen.columns - screen.column);
  if (amount <= 0) {
    return;
  }
  for (int index = 0; index < amount; ++index) {
    line.insert(screen.column, BlankCell());
  }
  line.resize(screen.columns);
  screen.wrap_pending = false;
}

void TerminalWidget::DeleteCharacters(int count) {
  Screen& screen = CurrentScreen();
  Line& line = LineAt(&screen, screen.row);
  const int amount = std::clamp(count, 0, screen.columns - screen.column);
  for (int index = 0; index < amount; ++index) {
    line.remove(screen.column);
    line.append(BlankCell());
  }
  screen.wrap_pending = false;
}

void TerminalWidget::InsertLines(int count) {
  Screen& screen = CurrentScreen();
  if (screen.row < screen.scroll_top || screen.row > screen.scroll_bottom) {
    return;
  }
  const int amount =
      std::clamp(count, 0, screen.scroll_bottom - screen.row + 1);
  for (int line = 0; line < amount; ++line) {
    for (int row = screen.scroll_bottom; row > screen.row; --row) {
      screen.lines[row] = screen.lines[row - 1];
    }
    screen.lines[screen.row] = BlankLine(screen.columns);
  }
}

void TerminalWidget::DeleteLines(int count) {
  Screen& screen = CurrentScreen();
  if (screen.row < screen.scroll_top || screen.row > screen.scroll_bottom) {
    return;
  }
  const int amount =
      std::clamp(count, 0, screen.scroll_bottom - screen.row + 1);
  for (int line = 0; line < amount; ++line) {
    for (int row = screen.row; row < screen.scroll_bottom; ++row) {
      screen.lines[row] = screen.lines[row + 1];
    }
    screen.lines[screen.scroll_bottom] = BlankLine(screen.columns);
  }
}

void TerminalWidget::SetCursor(int row, int column) {
  Screen& screen = CurrentScreen();
  screen.row = std::clamp(row, 0, screen.rows - 1);
  screen.column = std::clamp(column, 0, screen.columns - 1);
  screen.wrap_pending = false;
}

void TerminalWidget::MoveCursor(int row_delta, int column_delta) {
  Screen& screen = CurrentScreen();
  SetCursor(screen.row + row_delta, screen.column + column_delta);
}

void TerminalWidget::SaveCursor() {
  Screen& screen = CurrentScreen();
  screen.saved.row = screen.row;
  screen.saved.column = screen.column;
  screen.saved.foreground = screen.foreground;
  screen.saved.background = screen.background;
  screen.saved.attributes = screen.attributes;
  screen.saved.wrap_pending = screen.wrap_pending;
}

void TerminalWidget::RestoreCursor() {
  Screen& screen = CurrentScreen();
  screen.row = std::clamp(screen.saved.row, 0, screen.rows - 1);
  screen.column = std::clamp(screen.saved.column, 0, screen.columns - 1);
  screen.foreground = screen.saved.foreground;
  screen.background = screen.saved.background;
  screen.attributes = screen.saved.attributes;
  screen.wrap_pending = screen.saved.wrap_pending;
}

void TerminalWidget::EnterAlternateScreen(bool clear) {
  if (alternate_active_) {
    return;
  }
  ResizeScreen(&alternate_, primary_.rows, primary_.columns);
  ResetScreen(&alternate_);
  alternate_active_ = true;
  active_ = &alternate_;
  scroll_offset_ = 0;
  ClearSelection();
  if (clear) {
    EraseDisplay(2);
  }
  SyncScrollBar();
}

void TerminalWidget::LeaveAlternateScreen() {
  if (!alternate_active_) {
    return;
  }
  alternate_active_ = false;
  active_ = &primary_;
  scroll_offset_ = 0;
  ClearSelection();
  SyncScrollBar();
}

void TerminalWidget::ResetColorState() {
  Screen& screen = CurrentScreen();
  screen.foreground = QColor();
  screen.background = QColor();
  screen.attributes = 0;
}

void TerminalWidget::SetForegroundIndex(int index) {
  CurrentScreen().foreground = ColorForIndex(index);
}

void TerminalWidget::SetBackgroundIndex(int index) {
  CurrentScreen().background = ColorForIndex(index);
}

void TerminalWidget::SetForegroundRgb(const QColor& color) {
  CurrentScreen().foreground = color;
}

void TerminalWidget::SetBackgroundRgb(const QColor& color) {
  CurrentScreen().background = color;
}

QColor TerminalWidget::ColorForIndex(int index) const {
  if (index < 0) {
    return QColor();
  }
  if (index < 16) {
    return palette_[index];
  }
  if (index < 232) {
    const int value = index - 16;
    const int red = value / 36;
    const int green = (value / 6) % 6;
    const int blue = value % 6;
    const auto level = [](int component) {
      return component == 0 ? 0 : 55 + component * 40;
    };
    return QColor(level(red), level(green), level(blue));
  }
  if (index < 256) {
    const int level = 8 + (index - 232) * 10;
    return QColor(level, level, level);
  }
  return QColor();
}

void TerminalWidget::ApplySgr(const QString& params) {
  if (params.isEmpty()) {
    ResetColorState();
    return;
  }
  const QStringList tokens = params.split(QLatin1Char(';'), Qt::KeepEmptyParts);
  for (int index = 0; index < tokens.size(); ++index) {
    QString token = tokens[index];
    if (token.isEmpty()) {
      token = QStringLiteral("0");
    }
    if (token.contains(QLatin1Char(':'))) {
      const QStringList parts = token.split(QLatin1Char(':'));
      bool ok = false;
      const int code = parts.value(0).toInt(&ok);
      if (!ok) {
        continue;
      }
      if ((code == 38 || code == 48) && parts.size() >= 3) {
        const int kind = parts.value(1).toInt();
        if (kind == 5) {
          const int color = parts.value(2).toInt();
          if (code == 38) {
            SetForegroundIndex(color);
          } else {
            SetBackgroundIndex(color);
          }
        } else if (kind == 2 && parts.size() >= 5) {
          const QColor color(parts.value(parts.size() - 3).toInt(),
                             parts.value(parts.size() - 2).toInt(),
                             parts.value(parts.size() - 1).toInt());
          if (code == 38) {
            SetForegroundRgb(color);
          } else {
            SetBackgroundRgb(color);
          }
        }
      } else if (code == 4) {
        CurrentScreen().attributes |= kUnderline;
      }
      continue;
    }
    bool ok = false;
    const int code = token.toInt(&ok);
    if (!ok) {
      continue;
    }
    if (code == 0) {
      ResetColorState();
    } else if (code == 1) {
      CurrentScreen().attributes |= kBold;
    } else if (code == 2) {
      CurrentScreen().attributes |= kDim;
    } else if (code == 3) {
      CurrentScreen().attributes |= kItalic;
    } else if (code == 4 || code == 21) {
      CurrentScreen().attributes |= kUnderline;
    } else if (code == 5 || code == 6) {
      CurrentScreen().attributes |= kBlink;
    } else if (code == 7) {
      CurrentScreen().attributes |= kReverse;
    } else if (code == 8) {
      CurrentScreen().attributes |= kHidden;
    } else if (code == 9) {
      CurrentScreen().attributes |= kStrike;
    } else if (code == 22) {
      CurrentScreen().attributes &= static_cast<quint16>(~(kBold | kDim));
    } else if (code == 23) {
      CurrentScreen().attributes &= static_cast<quint16>(~kItalic);
    } else if (code == 24) {
      CurrentScreen().attributes &= static_cast<quint16>(~kUnderline);
    } else if (code == 25) {
      CurrentScreen().attributes &= static_cast<quint16>(~kBlink);
    } else if (code == 27) {
      CurrentScreen().attributes &= static_cast<quint16>(~kReverse);
    } else if (code == 28) {
      CurrentScreen().attributes &= static_cast<quint16>(~kHidden);
    } else if (code == 29) {
      CurrentScreen().attributes &= static_cast<quint16>(~kStrike);
    } else if (code >= 30 && code <= 37) {
      SetForegroundIndex(code - 30);
    } else if (code == 39) {
      CurrentScreen().foreground = QColor();
    } else if (code >= 40 && code <= 47) {
      SetBackgroundIndex(code - 40);
    } else if (code == 49) {
      CurrentScreen().background = QColor();
    } else if (code >= 90 && code <= 97) {
      SetForegroundIndex(code - 90 + 8);
    } else if (code >= 100 && code <= 107) {
      SetBackgroundIndex(code - 100 + 8);
    } else if (code == 38 || code == 48) {
      if (index + 1 >= tokens.size()) {
        break;
      }
      const int kind = tokens[index + 1].toInt();
      if (kind == 5 && index + 2 < tokens.size()) {
        const int color = tokens[index + 2].toInt();
        if (code == 38) {
          SetForegroundIndex(color);
        } else {
          SetBackgroundIndex(color);
        }
        index += 2;
      } else if (kind == 2 && index + 4 < tokens.size()) {
        const QColor color(tokens[index + 2].toInt(), tokens[index + 3].toInt(),
                           tokens[index + 4].toInt());
        if (code == 38) {
          SetForegroundRgb(color);
        } else {
          SetBackgroundRgb(color);
        }
        index += 4;
      }
    }
  }
}

void TerminalWidget::ResizeScreen(Screen* screen, int rows, int columns) const {
  if (screen == nullptr) {
    return;
  }
  rows = std::max(2, rows);
  columns = std::max(2, columns);
  if (screen->rows != rows) {
    for (int row = screen->rows; row < rows; ++row) {
      screen->lines.append(BlankLine(columns));
    }
    screen->lines.resize(rows);
  }
  for (Line& line : screen->lines) {
    line.resize(columns);
  }
  screen->rows = rows;
  screen->columns = columns;
  screen->scroll_top = 0;
  screen->scroll_bottom = rows - 1;
  screen->row = std::clamp(screen->row, 0, rows - 1);
  screen->column = std::clamp(screen->column, 0, columns - 1);
  screen->wrap_pending = false;
}

void TerminalWidget::ApplyWindowSize() {
  const int columns =
      std::max(2, (width() - 2 * padding_ - kScrollBarReserve) / cell_width_);
  const int rows = std::max(2, (height() - 2 * padding_) / cell_height_);
  if (columns == primary_.columns && rows == primary_.rows) {
    return;
  }
  ResizeScreen(&primary_, rows, columns);
  ResizeScreen(&alternate_, rows, columns);
  if (alternate_active_) {
    active_ = &alternate_;
  } else {
    active_ = &primary_;
  }
  scroll_offset_ =
      std::clamp(scroll_offset_, 0, static_cast<int>(scrollback_.size()));
#if defined(Q_OS_UNIX)
  if (master_fd_ >= 0) {
    struct winsize size{static_cast<uint16_t>(rows),
                        static_cast<uint16_t>(columns), 0, 0};
    ::ioctl(master_fd_, TIOCSWINSZ, &size);
  }
#endif
  SyncScrollBar();
}

TerminalWidget::Screen& TerminalWidget::CurrentScreen() { return *active_; }

const TerminalWidget::Screen& TerminalWidget::CurrentScreen() const {
  return *active_;
}

TerminalWidget::Cell TerminalWidget::BlankCell() { return Cell{}; }

TerminalWidget::Line TerminalWidget::BlankLine(int columns) {
  Line line;
  line.resize(std::max(1, columns));
  return line;
}

TerminalWidget::Line& TerminalWidget::LineAt(Screen* screen, int row) {
  row = std::clamp(row, 0, screen->rows - 1);
  if (screen->lines.size() < screen->rows) {
    screen->lines.resize(screen->rows);
  }
  Line& line = screen->lines[row];
  if (line.size() != screen->columns) {
    line.resize(screen->columns);
  }
  return line;
}

const TerminalWidget::Line* TerminalWidget::VisibleLine(
    int viewport_row) const {
  if (alternate_active_) {
    if (viewport_row < 0 || viewport_row >= alternate_.rows) {
      return nullptr;
    }
    return &alternate_.lines[viewport_row];
  }
  const int total = static_cast<int>(scrollback_.size()) + primary_.rows;
  const int start = std::max(0, total - primary_.rows - scroll_offset_);
  const int index = start + viewport_row;
  if (index < 0 || index >= total) {
    return nullptr;
  }
  if (index < scrollback_.size()) {
    return &scrollback_[index];
  }
  const int row = index - scrollback_.size();
  if (row < 0 || row >= primary_.rows) {
    return nullptr;
  }
  return &primary_.lines[row];
}

void TerminalWidget::ResetScreen(Screen* screen) {
  if (screen == nullptr) {
    return;
  }
  screen->lines.clear();
  screen->lines.resize(screen->rows);
  for (Line& line : screen->lines) {
    line = BlankLine(screen->columns);
  }
  screen->row = 0;
  screen->column = 0;
  screen->scroll_top = 0;
  screen->scroll_bottom = screen->rows - 1;
  screen->foreground = QColor();
  screen->background = QColor();
  screen->attributes = 0;
  screen->wrap_pending = false;
  screen->cursor_visible = true;
  screen->saved = SavedCursor{};
}

void TerminalWidget::ResetTerminal() {
  ResetScreen(&primary_);
  ResetScreen(&alternate_);
  alternate_active_ = false;
  active_ = &primary_;
  scrollback_.clear();
  scroll_offset_ = 0;
  app_cursor_keys_ = false;
  bracketed_paste_ = false;
  newline_mode_ = false;
  origin_mode_ = false;
  insert_mode_ = false;
  auto_wrap_ = true;
  ClearSelection();
  SyncScrollBar();
  update();
}

void TerminalWidget::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), default_background_);
  painter.setRenderHint(QPainter::TextAntialiasing, true);
  const int visible_rows =
      std::max(1, (height() - 2 * padding_) / cell_height_);
  const int visible_columns =
      std::max(1, (width() - 2 * padding_ - kScrollBarReserve) / cell_width_);
  for (int row = 0; row < visible_rows; ++row) {
    const Line* line = VisibleLine(row);
    if (line == nullptr) {
      continue;
    }
    const int y = padding_ + row * cell_height_;
    for (int column = 0; column < visible_columns && column < line->size();
         ++column) {
      const Cell& cell = line->at(column);
      if (cell.continuation) {
        continue;
      }
      QColor foreground = ResolvedForeground(cell, default_foreground_);
      QColor background = ResolvedBackground(cell, default_background_);
      if (cell.attributes & kReverse) {
        std::swap(foreground, background);
      }
      if (cell.attributes & kDim) {
        foreground.setAlphaF(0.65);
      }
      if (cell.attributes & kHidden) {
        foreground = background;
      }
      const bool selected = IsSelected(row, column);
      if (selected) {
        background = Mix(background, theme_.accent, 0.34);
      }
      const int x = padding_ + column * cell_width_;
      const int span =
          (column + 1 < line->size() && line->at(column + 1).continuation) ? 2
                                                                           : 1;
      if (background != default_background_ || selected) {
        painter.fillRect(QRect(x, y, cell_width_ * span, cell_height_),
                         background);
      }
      if (cell.text.isEmpty() || cell.text == QStringLiteral(" ")) {
        continue;
      }
      QFont font = font_;
      if (cell.attributes & kBold) {
        font.setWeight(QFont::Bold);
      }
      if (cell.attributes & kItalic) {
        font.setItalic(true);
      }
      if (cell.attributes & kUnderline) {
        font.setUnderline(true);
      }
      if (cell.attributes & kStrike) {
        font.setStrikeOut(true);
      }
      painter.setFont(font);
      painter.setPen(foreground);
      painter.drawText(QRectF(x, y, cell_width_ * span, cell_height_),
                       Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                       cell.text);
    }
  }

  const Screen& screen = CurrentScreen();
  if (!screen.cursor_visible) {
    return;
  }
  const int cursor_row = screen.row;
  if (!alternate_active_ && scroll_offset_ != 0) {
    return;
  }
  if (cursor_row < 0 || cursor_row >= visible_rows) {
    return;
  }
  const int cursor_column = std::min(screen.column, screen.columns - 1);
  const QRect cursor_rect(padding_ + cursor_column * cell_width_,
                          padding_ + cursor_row * cell_height_, cell_width_,
                          cell_height_);
  if (!hasFocus()) {
    painter.setPen(QPen(theme_.text_secondary, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(cursor_rect.adjusted(0, 0, -1, -1));
    return;
  }
  if (!cursor_blink_on_) {
    return;
  }
  const Line* cursor_line = VisibleLine(cursor_row);
  QString glyph = QStringLiteral(" ");
  if (cursor_line != nullptr && cursor_column < cursor_line->size()) {
    glyph = cursor_line->at(cursor_column).text;
  }
  painter.fillRect(cursor_rect, default_foreground_);
  painter.setPen(default_background_);
  painter.setFont(font_);
  painter.drawText(cursor_rect,
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine,
                   glyph);
}

QColor TerminalWidget::ResolvedForeground(const Cell& cell,
                                          const QColor& default_color) const {
  return cell.foreground.isValid() ? cell.foreground : default_color;
}

QColor TerminalWidget::ResolvedBackground(const Cell& cell,
                                          const QColor& default_color) const {
  return cell.background.isValid() ? cell.background : default_color;
}

QString TerminalWidget::LineText(const Line& line, int start, int end) const {
  const int line_size = static_cast<int>(line.size());
  const int from = std::clamp(start, 0, line_size);
  const int to = std::clamp(end < 0 ? line_size : end, 0, line_size);
  QString text;
  for (int column = from; column < to; ++column) {
    const Cell& cell = line.at(column);
    if (cell.continuation) {
      continue;
    }
    text += cell.text;
  }
  return TrimLine(text);
}

QStringList TerminalWidget::ScreenLinesForTest() const {
  QStringList result;
  for (const Line& line : CurrentScreen().lines) {
    result.append(LineText(line));
  }
  return result;
}

QStringList TerminalWidget::ScrollbackForTest() const {
  QStringList result;
  for (const Line& line : scrollback_) {
    result.append(LineText(line));
  }
  return result;
}

QString TerminalWidget::PlainTextForTest() const {
  QStringList lines;
  for (const Line& line : scrollback_) {
    lines.append(LineText(line));
  }
  for (const Line& line : CurrentScreen().lines) {
    lines.append(LineText(line));
  }
  while (!lines.isEmpty() && lines.last().isEmpty()) {
    lines.removeLast();
  }
  return lines.join(QLatin1Char('\n'));
}

int TerminalWidget::CursorRowForTest() const { return CurrentScreen().row; }

int TerminalWidget::CursorColumnForTest() const {
  return CurrentScreen().column;
}

QColor TerminalWidget::CellForegroundForTest(int row, int column) const {
  if (row < 0 || row >= CurrentScreen().lines.size() || column < 0 ||
      column >= CurrentScreen().lines[row].size()) {
    return QColor();
  }
  const Cell& cell = CurrentScreen().lines[row][column];
  return ResolvedForeground(cell, default_foreground_);
}

QColor TerminalWidget::CellBackgroundForTest(int row, int column) const {
  if (row < 0 || row >= CurrentScreen().lines.size() || column < 0 ||
      column >= CurrentScreen().lines[row].size()) {
    return QColor();
  }
  const Cell& cell = CurrentScreen().lines[row][column];
  return ResolvedBackground(cell, default_background_);
}

bool TerminalWidget::IsSelected(int viewport_row, int column) const {
  if (selection_anchor_row_ < 0 || selection_row_ < 0) {
    return false;
  }
  const int first_row = std::min(selection_anchor_row_, selection_row_);
  const int last_row = std::max(selection_anchor_row_, selection_row_);
  if (viewport_row < first_row || viewport_row > last_row) {
    return false;
  }
  if (first_row == last_row) {
    return column >= std::min(selection_anchor_column_, selection_column_) &&
           column <= std::max(selection_anchor_column_, selection_column_);
  }
  if (viewport_row == first_row) {
    const int start = selection_anchor_row_ == first_row
                          ? selection_anchor_column_
                          : selection_column_;
    return column >= start;
  }
  if (viewport_row == last_row) {
    const int end = selection_anchor_row_ == last_row ? selection_anchor_column_
                                                      : selection_column_;
    return column <= end;
  }
  return true;
}

QString TerminalWidget::SelectedText() const {
  if (selection_anchor_row_ < 0 || selection_row_ < 0) {
    return QString();
  }
  const int first_row = std::min(selection_anchor_row_, selection_row_);
  const int last_row = std::max(selection_anchor_row_, selection_row_);
  QStringList lines;
  for (int row = first_row; row <= last_row; ++row) {
    const Line* line = VisibleLine(row);
    if (line == nullptr) {
      continue;
    }
    int start = 0;
    int end = line->size();
    if (first_row == last_row) {
      start = std::min(selection_anchor_column_, selection_column_);
      end = std::max(selection_anchor_column_, selection_column_) + 1;
    } else if (row == first_row) {
      start = selection_anchor_row_ == first_row ? selection_anchor_column_
                                                 : selection_column_;
    } else if (row == last_row) {
      end = (selection_anchor_row_ == last_row ? selection_anchor_column_
                                               : selection_column_) +
            1;
    }
    lines.append(LineText(*line, start, end));
  }
  return lines.join(QLatin1Char('\n'));
}

bool TerminalWidget::HasSelection() const {
  return selection_anchor_row_ >= 0 && selection_row_ >= 0;
}

void TerminalWidget::ClearSelection() {
  selection_anchor_row_ = -1;
  selection_anchor_column_ = -1;
  selection_row_ = -1;
  selection_column_ = -1;
  selecting_ = false;
  update();
}

void TerminalWidget::SelectAll() {
  const Screen& screen = CurrentScreen();
  if (screen.lines.isEmpty()) {
    return;
  }
  selection_anchor_row_ = 0;
  selection_anchor_column_ = 0;
  selection_row_ = screen.rows - 1;
  selection_column_ = screen.columns - 1;
  selecting_ = false;
  update();
}

void TerminalWidget::SelectRangeForTest(int anchor_row, int anchor_column,
                                        int end_row, int end_column) {
  const Screen& screen = CurrentScreen();
  selection_anchor_row_ = std::clamp(anchor_row, 0, screen.rows - 1);
  selection_anchor_column_ = std::clamp(anchor_column, 0, screen.columns - 1);
  selection_row_ = std::clamp(end_row, 0, screen.rows - 1);
  selection_column_ = std::clamp(end_column, 0, screen.columns - 1);
  selecting_ = false;
  update();
}

void TerminalWidget::CopySelection() {
  const QString text = SelectedText();
  if (!text.isEmpty()) {
    QGuiApplication::clipboard()->setText(text);
  }
}

void TerminalWidget::PasteClipboard() {
  const QString text = QGuiApplication::clipboard()->text();
  if (!text.isEmpty()) {
    ProcessPaste(text);
  }
}

void TerminalWidget::ProcessPaste(const QString& text) {
  QString normalized = text;
  normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\r"));
  normalized.replace(QLatin1Char('\n'), QLatin1Char('\r'));
  if (bracketed_paste_) {
    SendBytes("\x1b[200~");
    SendBytes(normalized.toUtf8());
    SendBytes("\x1b[201~");
  } else {
    SendBytes(normalized.toUtf8());
  }
}

void TerminalWidget::SendKeySequence(const QByteArray& bytes) {
  SendBytes(bytes);
  cursor_blink_on_ = true;
  update();
}

void TerminalWidget::keyPressEvent(QKeyEvent* event) {
  if (event == nullptr) {
    return;
  }
  const Qt::KeyboardModifiers modifiers = event->modifiers();
#if defined(Q_OS_MACOS)
  const bool command = modifiers.testFlag(Qt::ControlModifier);
  const bool control = modifiers.testFlag(Qt::MetaModifier);
#else
  const bool command = false;
  const bool control = modifiers.testFlag(Qt::ControlModifier);
#endif
  const bool shift = modifiers.testFlag(Qt::ShiftModifier);
  const bool alt = modifiers.testFlag(Qt::AltModifier);
  const int key = event->key();

  if ((command && key == Qt::Key_C) || (control && shift && key == Qt::Key_C)) {
    CopySelection();
    event->accept();
    return;
  }
  if ((command && key == Qt::Key_V) || (control && shift && key == Qt::Key_V) ||
      (shift && key == Qt::Key_Insert)) {
    PasteClipboard();
    event->accept();
    return;
  }
  if ((command && key == Qt::Key_A) || (control && shift && key == Qt::Key_A)) {
    SelectAll();
    event->accept();
    return;
  }
  if (command) {
    event->accept();
    return;
  }
  if (shift && key == Qt::Key_PageUp) {
    ScrollBy(std::max(1, CurrentScreen().rows));
    event->accept();
    return;
  }
  if (shift && key == Qt::Key_PageDown) {
    ScrollBy(-std::max(1, CurrentScreen().rows));
    event->accept();
    return;
  }
  if (control) {
    if (key == Qt::Key_L && shift) {
      ClearDisplay();
      event->accept();
      return;
    }
    const QByteArray bytes = ControlByteForKey(key);
    if (!bytes.isEmpty()) {
      SendKeySequence(bytes);
      event->accept();
      return;
    }
  }

  QByteArray sequence;
  switch (key) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
      sequence = "\r";
      break;
    case Qt::Key_Backspace:
      sequence = "\x7f";
      break;
    case Qt::Key_Tab:
      sequence = "\t";
      break;
    case Qt::Key_Backtab:
      sequence = "\x1b[Z";
      break;
    case Qt::Key_Escape:
      sequence = "\x1b";
      break;
    case Qt::Key_Up:
      sequence = app_cursor_keys_ ? "\x1bOA" : "\x1b[A";
      break;
    case Qt::Key_Down:
      sequence = app_cursor_keys_ ? "\x1bOB" : "\x1b[B";
      break;
    case Qt::Key_Right:
      sequence = app_cursor_keys_ ? "\x1bOC" : "\x1b[C";
      break;
    case Qt::Key_Left:
      sequence = app_cursor_keys_ ? "\x1bOD" : "\x1b[D";
      break;
    case Qt::Key_Home:
      sequence = "\x1b[H";
      break;
    case Qt::Key_End:
      sequence = "\x1b[F";
      break;
    case Qt::Key_Insert:
      sequence = "\x1b[2~";
      break;
    case Qt::Key_Delete:
      sequence = "\x1b[3~";
      break;
    case Qt::Key_PageUp:
      sequence = "\x1b[5~";
      break;
    case Qt::Key_PageDown:
      sequence = "\x1b[6~";
      break;
    case Qt::Key_F1:
      sequence = "\x1bOP";
      break;
    case Qt::Key_F2:
      sequence = "\x1bOQ";
      break;
    case Qt::Key_F3:
      sequence = "\x1bOR";
      break;
    case Qt::Key_F4:
      sequence = "\x1bOS";
      break;
    case Qt::Key_F5:
      sequence = "\x1b[15~";
      break;
    case Qt::Key_F6:
      sequence = "\x1b[17~";
      break;
    case Qt::Key_F7:
      sequence = "\x1b[18~";
      break;
    case Qt::Key_F8:
      sequence = "\x1b[19~";
      break;
    case Qt::Key_F9:
      sequence = "\x1b[20~";
      break;
    case Qt::Key_F10:
      sequence = "\x1b[21~";
      break;
    case Qt::Key_F11:
      sequence = "\x1b[23~";
      break;
    case Qt::Key_F12:
      sequence = "\x1b[24~";
      break;
    default:
      break;
  }
  if (!sequence.isEmpty()) {
    if (alt && sequence.size() == 1) {
      sequence.prepend('\x1b');
    }
    SendKeySequence(sequence);
    event->accept();
    return;
  }
  const QString text = event->text();
  if (!text.isEmpty()) {
    QByteArray bytes = text.toUtf8();
    if (alt) {
      bytes.prepend('\x1b');
    }
    SendKeySequence(bytes);
    event->accept();
    return;
  }
  event->ignore();
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent* event) {
  if (event == nullptr) {
    return;
  }
  if (!event->commitString().isEmpty()) {
    SendText(event->commitString());
  }
  event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle) {
    return QRect(padding_ + CurrentScreen().column * cell_width_,
                 padding_ + CurrentScreen().row * cell_height_, cell_width_,
                 cell_height_);
  }
  if (query == Qt::ImEnabled || query == Qt::ImReadOnly) {
    return query == Qt::ImEnabled;
  }
  return QVariant();
}

void TerminalWidget::focusInEvent(QFocusEvent* event) {
  QWidget::focusInEvent(event);
  cursor_blink_on_ = true;
  cursor_blink_timer_->start();
  update();
}

void TerminalWidget::focusOutEvent(QFocusEvent* event) {
  QWidget::focusOutEvent(event);
  cursor_blink_timer_->stop();
  cursor_blink_on_ = true;
  update();
}

void TerminalWidget::mousePressEvent(QMouseEvent* event) {
  if (event == nullptr) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  if (event->button() == Qt::LeftButton) {
    const int row = std::clamp(
        static_cast<int>((event->position().y() - padding_) / cell_height_), 0,
        CurrentScreen().rows - 1);
    const int column = std::clamp(
        static_cast<int>((event->position().x() - padding_) / cell_width_), 0,
        CurrentScreen().columns - 1);
    selection_anchor_row_ = row;
    selection_anchor_column_ = column;
    selection_row_ = -1;
    selection_column_ = -1;
    selecting_ = true;
    update();
    event->accept();
    return;
  }
  QWidget::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent* event) {
  if (!selecting_ || event == nullptr) {
    QWidget::mouseMoveEvent(event);
    return;
  }
  const int row = std::clamp(
      static_cast<int>((event->position().y() - padding_) / cell_height_), 0,
      CurrentScreen().rows - 1);
  const int column = std::clamp(
      static_cast<int>((event->position().x() - padding_) / cell_width_), 0,
      CurrentScreen().columns - 1);
  selection_row_ = row;
  selection_column_ = column;
  update();
  event->accept();
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent* event) {
  if (selecting_ && event != nullptr && event->button() == Qt::LeftButton) {
    selecting_ = false;
    event->accept();
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

QMenu* TerminalWidget::CreateContextMenuForTest(QWidget* parent) {
  QMenu* menu = new QMenu(parent != nullptr ? parent : this);
  menu->setObjectName(QStringLiteral("terminalContextMenu"));
  QAction* copy = menu->addAction(QStringLiteral("Copy"));
  copy->setObjectName(QStringLiteral("terminalContextCopy"));
  copy->setEnabled(HasSelection());
  QAction* paste = menu->addAction(QStringLiteral("Paste"));
  paste->setObjectName(QStringLiteral("terminalContextPaste"));
  paste->setEnabled(!QGuiApplication::clipboard()->text().isEmpty());
  QAction* select_all = menu->addAction(QStringLiteral("Select All"));
  select_all->setObjectName(QStringLiteral("terminalContextSelectAll"));
  menu->addSeparator();
  QAction* clear = menu->addAction(QStringLiteral("Clear"));
  clear->setObjectName(QStringLiteral("terminalContextClear"));
  QObject::connect(copy, &QAction::triggered, this,
                   &TerminalWidget::CopySelection);
  QObject::connect(paste, &QAction::triggered, this,
                   &TerminalWidget::PasteClipboard);
  QObject::connect(select_all, &QAction::triggered, this,
                   &TerminalWidget::SelectAll);
  QObject::connect(clear, &QAction::triggered, this,
                   &TerminalWidget::ClearDisplay);
  return menu;
}

void TerminalWidget::contextMenuEvent(QContextMenuEvent* event) {
  if (event == nullptr) {
    return;
  }
  setFocus(Qt::MouseFocusReason);
  QScopedPointer<QMenu> menu(CreateContextMenuForTest(this));
  menu->exec(event->globalPos());
  event->accept();
}

void TerminalWidget::wheelEvent(QWheelEvent* event) {
  if (event == nullptr) {
    return;
  }
  const QPoint delta = event->angleDelta();
  if (delta.y() == 0) {
    event->ignore();
    return;
  }
  ScrollBy((delta.y() / 120) * 3);
  event->accept();
}

void TerminalWidget::ScrollBy(int lines) {
  if (alternate_active_ || scrollback_.isEmpty()) {
    return;
  }
  scroll_offset_ = std::clamp(scroll_offset_ + lines, 0,
                              static_cast<int>(scrollback_.size()));
  ClearSelection();
  SyncScrollBar();
  update();
}

void TerminalWidget::SyncScrollBar() {
  if (scrollbar_ == nullptr) {
    return;
  }
  const int max_value =
      alternate_active_ ? 0 : static_cast<int>(scrollback_.size());
  updating_scrollbar_ = true;
  scrollbar_->setRange(0, max_value);
  scrollbar_->setPageStep(std::max(1, primary_.rows));
  scrollbar_->setValue(std::clamp(max_value - scroll_offset_, 0, max_value));
  scrollbar_->setVisible(max_value > 0);
  updating_scrollbar_ = false;
}

void TerminalWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ApplyWindowSize();
  if (scrollbar_ != nullptr) {
    scrollbar_->setGeometry(width() - kScrollBarReserve + 1, padding_,
                            kScrollBarReserve - 2,
                            std::max(0, height() - 2 * padding_));
  }
}

QSize TerminalWidget::sizeHint() const { return QSize(640, 360); }

QSize TerminalWidget::minimumSizeHint() const {
  return QSize(cell_width_ * 20 + 2 * padding_ + kScrollBarReserve,
               cell_height_ * 4 + 2 * padding_);
}

}  // namespace hermes::ui
